/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017-2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_SECURITY_UPDATE_CHECKER_H_
#define _PASSENGER_SECURITY_UPDATE_CHECKER_H_

#include <string>
#include <cassert>
#include <boost/config.hpp>
#include <boost/scoped_ptr.hpp>
#include <oxt/thread.hpp>
#include <oxt/backtrace.hpp>

#include <SecurityKit/Crypto.h>
#include <ResourceLocator.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <ConfigKit/ConfigKit.h>
#include <Utils/Curl.h>
#include <modp_b64.h>

#if BOOST_OS_MACOS
	#include <sys/syslimits.h>
	#include <unistd.h>
	#include <Availability.h>
	#ifndef __MAC_10_13
		#define __MAC_10_13 101300
	#endif
	#define PRE_HIGH_SIERRA (__MAC_OS_X_VERSION_MIN_REQUIRED < __MAC_10_13)
	#if !PRE_HIGH_SIERRA
		#include <openssl/err.h>
	#endif
#endif

namespace Passenger {

using namespace std;
using namespace oxt;


#define MIN_CHECK_BACKOFF_SEC (12 * 60 * 60)
#define MAX_CHECK_BACKOFF_SEC (7 * 24 * 60 * 60)

#if PRE_HIGH_SIERRA
// Password for the .p12 client certificate (because .p12 is required to be pwd protected on some
// implementations). We're OK with hardcoding because the certs are not secret anyway, and they're not used
// for client id/auth (just to easily deflect unrelated probes from the server endpoint).
#define CLIENT_CERT_PWD "p6PBhK8KtorrhMxHnH855MvF"
#define CLIENT_CERT_LABEL "Phusion Passenger Open Source"
#endif

#define POSSIBLE_MITM_RESOLUTION "(if this error persists check your connection security or try upgrading " SHORT_PROGRAM_NAME ")"

/**
 * If started, this class periodically (default: daily, immediate start) checks whether there are any important
 * security updates available (updates that don't fix security issues are not reported). The result is logged
 * (level 3:notice if no update, level 1:error otherwise), and all further action is left to the user (there is
 * no auto-update mechanism).
 */
class SecurityUpdateChecker {
public:
	/*
	 * BEGIN ConfigKit schema: Passenger::SecurityUpdateChecker::Schema
	 * (do not edit: following text is automatically generated
	 * by 'rake configkit_schemas_inline_comments')
	 *
	 *   certificate_path     string             -          -
	 *   disabled             boolean            -          default(false)
	 *   interval             unsigned integer   -          default(86400)
	 *   proxy_url            string             -          -
	 *   server_identifier    string             required   -
	 *   url                  string             -          default("https://securitycheck.phusionpassenger.com/v1/check.json")
	 *   web_server_version   string             -          -
	 *
	 * END
	 */
	class Schema: public ConfigKit::Schema {
	private:
		static void validateInterval(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
			unsigned int interval = config["interval"].asUInt();
			if (interval < MIN_CHECK_BACKOFF_SEC || interval > MAX_CHECK_BACKOFF_SEC) {
				errors.push_back(ConfigKit::Error("'{{interval}}' must be between " +
					toString(MIN_CHECK_BACKOFF_SEC) + " and " + toString(MAX_CHECK_BACKOFF_SEC)));
			}
		}

		static void validateProxyUrl(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
			if (config["proxy_url"].isNull()) {
				return;
			}
			if (config["proxy_url"].asString().empty()) {
				errors.push_back(ConfigKit::Error("'{{proxy_url}}', if specified, may not be empty"));
				return;
			}

			try {
				prepareCurlProxy(config["proxy_url"].asString());
			} catch (const ArgumentException &e) {
				errors.push_back(ConfigKit::Error(
					P_STATIC_STRING("'{{proxy_url}}': ")
					+ e.what()));
			}
		}

	public:
		Schema() {
			using namespace ConfigKit;

			add("disabled", BOOL_TYPE, OPTIONAL, false);
			add("url", STRING_TYPE, OPTIONAL, "https://securitycheck.phusionpassenger.com/v1/check.json");
			// Should be in the form: scheme://user:password@proxy_host:proxy_port
			add("proxy_url", STRING_TYPE, OPTIONAL);
			add("certificate_path", STRING_TYPE, OPTIONAL);
			add("interval", UINT_TYPE, OPTIONAL, 24 * 60 * 60);
			// Should be one of { nginx, apache, standalone nginx, standalone builtin }
			add("server_identifier", STRING_TYPE, REQUIRED);
			// The version of Nginx or Apache, if relevant (otherwise empty)
			add("web_server_version", STRING_TYPE, OPTIONAL);

			addValidator(validateInterval);
			addValidator(validateProxyUrl);

			finalize();
		}
	};

	struct ConfigRealization {
		CurlProxyInfo proxyInfo;
		string url;
		string certificatePath;

		ConfigRealization(const ConfigKit::Store &config)
			: proxyInfo(prepareCurlProxy(config["proxy_url"].asString())),
			  url(config["url"].asString()),
			  certificatePath(config["certificate_path"].asString())
			{ }

		void swap(ConfigRealization &other) BOOST_NOEXCEPT_OR_NOTHROW {
			proxyInfo.swap(other.proxyInfo);
			url.swap(other.url);
			certificatePath.swap(other.certificatePath);
		}
	};

	struct ConfigChangeRequest {
		boost::scoped_ptr<ConfigKit::Store> config;
		boost::scoped_ptr<ConfigRealization> configRlz;
	};

private:
	/*
	 * Since the security update checker runs in a separate thread,
	 * and the configuration can change while the checker is active,
	 * we make a copy of the current configuration at the beginning
	 * of each check.
	 */
	struct SessionState {
		ConfigKit::Store config;
		ConfigRealization configRlz;

		SessionState(const ConfigKit::Store &currentConfig,
			const ConfigRealization &currentConfigRlz)
			: config(currentConfig),
			  configRlz(currentConfigRlz)
			{ }
	};

	mutable boost::mutex configSyncher;
	ConfigKit::Store config;
	ConfigRealization configRlz;

	oxt::thread *updateCheckThread;
	string clientCertPath; // client cert (PKCS#12), checked by server
	string serverPubKeyPath; // for checking signature
	Crypto crypto;

	void threadMain() {
		TRACE_POINT();
		// Sleep for a short while to allow interruption during the Apache integration
		// double startup procedure, this prevents running the update check twice
		boost::this_thread::sleep_for(boost::chrono::seconds(2));
		while (!boost::this_thread::interruption_requested()) {
			UPDATE_TRACE_POINT();
			int backoffMin = 0;
			try {
				backoffMin = checkAndLogSecurityUpdate();
			} catch (const tracable_exception &e) {
				P_ERROR(e.what() << "\n" << e.backtrace());
			}

			UPDATE_TRACE_POINT();
			unsigned int checkIntervalSec;
			{
				boost::lock_guard<boost::mutex> l(configSyncher);
				checkIntervalSec = config["interval"].asUInt();
			}
			long backoffSec = checkIntervalSec + (backoffMin * 60);
			if (backoffSec < MIN_CHECK_BACKOFF_SEC) {
				backoffSec = MIN_CHECK_BACKOFF_SEC;
			}
			if (backoffSec > MAX_CHECK_BACKOFF_SEC) {
				backoffSec = MAX_CHECK_BACKOFF_SEC;
			}
			boost::this_thread::sleep_for(boost::chrono::seconds(backoffSec));
		}
	}


	void logUpdateFailCurl(const SessionState &sessionState, CURLcode code) {
		// At this point anything could be wrong, from unloadable certificates to server not found, etc.
		// Let's try to enrich the log message in case there are known solutions or workarounds (e.g. "use proxy").
		string error = curl_easy_strerror(code);

		switch (code) {
			case CURLE_SSL_CERTPROBLEM:
				error.append(" at " + clientCertPath + " (try upgrading or reinstalling " SHORT_PROGRAM_NAME ")");
				break;

			case CURLE_COULDNT_RESOLVE_HOST:
				error.append(" while connecting to " + sessionState.configRlz.url + " (check your DNS)");
				break;

			case CURLE_COULDNT_CONNECT:
				if (sessionState.config["proxy_url"].isNull()) {
					error.append(" for " + sessionState.configRlz.url + " " POSSIBLE_MITM_RESOLUTION);
				} else {
					error.append(" for " + sessionState.configRlz.url + " using proxy "
						+ sessionState.config["proxy_url"].asString() +
						" (if this error persists check your firewall and/or proxy settings)");
				}
				break;

			case CURLE_COULDNT_RESOLVE_PROXY:
				error.append(" for proxy address " + sessionState.config["proxy_url"].asString());
				break;

#if LIBCURL_VERSION_NUM < 0x073e00
			case CURLE_SSL_CACERT:
				// Peer certificate cannot be authenticated with given / known CA certificates. This would happen
				// for MITM but could also be a truststore issue.
#endif
			case CURLE_PEER_FAILED_VERIFICATION:
				// The remote server's SSL certificate or SSH md5 fingerprint was deemed not OK.
				error.append(" while connecting to " + sessionState.configRlz.url
					+ "; check that your connection is secure and that the"
					" truststore is valid. If the problem persists, you can also try upgrading"
					" or reinstalling " PROGRAM_NAME);
				break;

			case CURLE_SSL_CACERT_BADFILE:
				error.append(" while connecting to " + sessionState.configRlz.url + " ");
				if (!sessionState.config["proxy_url"].isNull()) {
					error.append("using proxy ");
					error.append(sessionState.config["proxy_url"].asString());
					error.append(" ");
				}
				error.append("; this might happen if the nss backend is installed for"
					" libcurl instead of GnuTLS or OpenSSL. If the problem persists, you can also try upgrading"
					" or reinstalling " PROGRAM_NAME);
				break;

			// Fallthroughs to default:
			case CURLE_SSL_CONNECT_ERROR:
				// A problem occurred somewhere in the SSL/TLS handshake. Not sure what's up, but in this case the
				// error buffer (printed in DEBUG) should pinpoint the problem slightly more.
			case CURLE_OPERATION_TIMEDOUT:
				// This is not a normal connect timeout, there are some refs to it occurring while downloading large
				// files, but we don't do that so fall through to default.
			default:
				error.append(" while connecting to " + sessionState.configRlz.url + " ");
				if (!sessionState.config["proxy_url"].isNull()) {
					error.append("using proxy ");
					error.append(sessionState.config["proxy_url"].asString());
					error.append(" ");
				}
				error.append(POSSIBLE_MITM_RESOLUTION);
				break;
		}

		logUpdateFail(error);

#if !(BOOST_OS_MACOS && PRE_HIGH_SIERRA)
		unsigned long cryptoErrorCode = ERR_get_error();
		if (cryptoErrorCode == 0) {
			logUpdateFailAdditional("CURLcode" + toString(code));
		} else {
			char buf[500];
			ERR_error_string(cryptoErrorCode, buf);
			logUpdateFailAdditional("CURLcode: " + toString(code) + ", Crypto: " + toString(cryptoErrorCode)
				+ " " + buf);
		}
#endif
	}

	void logUpdateFailHttp(const SessionState &sessionState, int httpCode) {
		string error;

		switch (httpCode) {
			case 404:
				error.append("url not found: " + sessionState.configRlz.url + " " POSSIBLE_MITM_RESOLUTION);
				break;
			case 403:
				error.append("connection denied by server " POSSIBLE_MITM_RESOLUTION);
				break;
			case 503:
				error.append("server temporarily unavailable, try again later");
				break;
			case 429:
				error.append("rate limit hit for your IP, try again later");
				break;
			case 400:
				error.append("request corrupted or not understood " POSSIBLE_MITM_RESOLUTION);
				break;
			case 422:
				error.append("request content was corrupted or not understood " POSSIBLE_MITM_RESOLUTION);
				break;
			default:
				error = "HTTP " + toString(httpCode) + " while connecting to " + sessionState.configRlz.url
					+ " " POSSIBLE_MITM_RESOLUTION;
			break;
		}
		logUpdateFail(error);
	}

	void logUpdateFailResponse(string error, string responseData) {
		logUpdateFail("error in server response (" + error +
				"). If this error persists, check your connection security and try upgrading " SHORT_PROGRAM_NAME);
		logUpdateFailAdditional(responseData);
	}


	/**
	 * POST a bodyJsonString using a client certificate, and receive the response in responseData.
	 *
	 * May allocate chunk data for setting Content-Type, receiver should deallocate with curl_slist_free_all().
	 */
	CURLcode prepareCurlPOST(CURL *curl, SessionState &sessionState, const string &bodyJsonString,
		string *responseData, struct curl_slist **chunk)
	{
		CURLcode code;

		// Hint for advanced debugging: curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1))) {
			return code;
		}
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_URL, sessionState.configRlz.url.c_str()))) {
			return code;
		}
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_HTTPGET, 0))) {
			return code;
		}
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyJsonString.c_str()))) {
			return code;
		}
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, bodyJsonString.length()))) {
			return code;
		}
		*chunk = curl_slist_append(NULL, "Content-Type: application/json");
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *chunk))) {
			return code;
		}

#if BOOST_OS_MACOS && PRE_HIGH_SIERRA
		// preauth the security update check key in the user's keychain (this is for libcurl's benefit because they don't bother to authorize themselves to use the keys they import)
		if (!crypto.preAuthKey(clientCertPath.c_str(), CLIENT_CERT_PWD, CLIENT_CERT_LABEL)) {
			return CURLE_SSL_CERTPROBLEM;
		}
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "P12"))) {
			return code;
		}
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_SSLCERTPASSWD, CLIENT_CERT_PWD))) {
		 	return code;
		}
#else
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM"))) {
			return code;
		}
#endif

		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_SSLCERT, clientCertPath.c_str()))) {
			return code;
		}

		// These should be on by default, but make sure.
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L))) {
			return code;
		}
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L))) {
			return code;
		}

		// Technically we could use CURLOPT_SSL_VERIFYSTATUS to check for server cert revocation, but
		// we want to support older versions. We don't trust the server purely based on the server cert
		// anyway (it needs to prove by signature later on).

		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receiveResponseBytes))) {
			return code;
		}
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, responseData))) {
			return code;
		}
		if (CURLE_OK != (code = setCurlProxy(curl, sessionState.configRlz.proxyInfo))) {
			return code;
		}

		// setopt failure(s) below don't abort the check.
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180);

		return CURLE_OK;
	}

	bool verifyFileReadable(char *filename) {
		FILE *fhnd = fopen(filename, "rb");
		if (fhnd == NULL) {
			return false;
		}
		fclose(fhnd);
		return true;
	}

	static size_t receiveResponseBytes(void *buffer, size_t size, size_t nmemb, void *userData) {
		string *responseData = (string *) userData;
		responseData->append((const char *) buffer, size * nmemb);
		return size * nmemb;
	}

public:
	// Dependencies
	ResourceLocator *resourceLocator;

	SecurityUpdateChecker(const Schema &schema, const Json::Value &initialConfig,
		const ConfigKit::Translator &translator = ConfigKit::DummyTranslator())
		: config(schema, initialConfig, translator),
		  configRlz(config),
		  updateCheckThread(NULL),
		  resourceLocator(NULL)
		{ }

	virtual ~SecurityUpdateChecker() {
		if (updateCheckThread != NULL) {
			updateCheckThread->interrupt_and_join();
			delete updateCheckThread;
		}
	}

	void initialize() {
		if (resourceLocator == NULL) {
			throw RuntimeException("resourceLocator must be non-NULL");
		}

		#if BOOST_OS_MACOS && PRE_HIGH_SIERRA
			clientCertPath = resourceLocator->getResourcesDir() + "/update_check_client_cert.p12";
		#else
			clientCertPath = resourceLocator->getResourcesDir() + "/update_check_client_cert.pem";
		#endif
		serverPubKeyPath = resourceLocator->getResourcesDir() + "/update_check_server_pubkey.pem";
	}

	/**
	 * Starts a periodic check, as dictated by the "interval" config option. For each check, the
	 * server may increase/decrease (within limits) the period until the next check (using the
	 * backoff parameter in the response).
	 *
	 * Assumes curl_global_init() was already performed.
	 */
	void start() {
		updateCheckThread = new oxt::thread(
			boost::bind(&SecurityUpdateChecker::threadMain, this),
			"Security update checker",
			1024 * 512
		);
	}

	/**
	 * All error log methods eventually lead here, except for the additional below.
	 */
	virtual void logUpdateFail(string error) {
		unsigned int checkIntervalSec;
		{
			boost::lock_guard<boost::mutex> l(configSyncher);
			checkIntervalSec = config["interval"].asUInt();
		}
		P_ERROR("Security update check failed: " << error << " (next check in "
			<< (checkIntervalSec / (60*60)) << " hours)");
	}

	/**
	 * Logs additional information at a lower loglevel so that it only spams when explicitely requested via loglevel.
	 */
	virtual void logUpdateFailAdditional(string additional) {
		P_DEBUG(additional);
	}

	virtual void logUpdateSuccess(int update, string success) {
		if (update == 0) {
			P_NOTICE(success);
		} else {
			P_ERROR(success);
		}
	}

	virtual void logUpdateSuccessAdditional(string additional) {
		P_ERROR(additional);
	}

	virtual CURLcode sendAndReceive(CURL *curl, string *responseData, long *responseCode) {
		CURLcode code;
		if (CURLE_OK != (code = curl_easy_perform(curl))) {
			return code;
		}
		return curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, responseCode);
	}

	virtual bool fillNonce(string &nonce) {
		return crypto.generateAndAppendNonce(nonce);
	}

	/**
	 * Sends POST to the configured URL (via SSL, with client cert) containing:
	 * {"version":"<passenger version>", "nonce":"<random nonce>"}
	 * The response will be:
	 * {"data":base64(data), "signature":base64(signature)}, where:
	 * - signature should be from server we trust and match base64(data),
	 * - data is {"nonce":"<reflected>", "update":0 or 1, "version":"<version>", "log": "<log msg>", "backoff":"<backoff>"}
	 * - reflected nonce should match what we POSTed
	 * - if update is 1 then <version> is logged as the recommended version to upgrade to
	 * - <log msg> (if present) is written to the log
	 * - <backoff> (minutes) is added to our default next check time
	 */
	int checkAndLogSecurityUpdate() {
		int backoffMin = 0;

		// 0. Copy current configuration
		boost::unique_lock<boost::mutex> l(configSyncher);
		SessionState sessionState(config, configRlz);
		l.unlock();

		if (sessionState.config["disabled"].asBool()) {
			P_INFO("Security update checking disabled; skipping check");
			return backoffMin;
		}

		// 1. Assemble data to send
		Json::Value bodyJson;

		bodyJson["passenger_version"] = PASSENGER_VERSION;

		bodyJson["server_integration"] = sessionState.config["server_identifier"];
		bodyJson["server_version"] = sessionState.config["web_server_version"];
		bodyJson["curl_static"] = isCurlStaticallyLinked();

		string nonce;
		if (!fillNonce(nonce)) {
			logUpdateFail("fillNonce() error");
			return backoffMin;
		}
		bodyJson["nonce"] = nonce; // against replay attacks

		// 2. Send and get response
		CURL *curl = curl_easy_init();
		if (curl == NULL) {
			logUpdateFail("curl_easy_init() error");
			return backoffMin;
		}

		struct curl_slist *chunk = NULL;
		char *signatureChars = NULL;
		char *dataChars = NULL;
		do { // for resource cleanup
			string responseData;
			long responseCode;
			CURLcode code;

			if (!verifyFileReadable((char *) clientCertPath.c_str())) {
				logUpdateFail("File not readable: " + clientCertPath);
				break;
			}

			if (CURLE_OK != (code = setCurlDefaultCaInfo(curl))) {
				logUpdateFailCurl(sessionState, code);
				break;
			}

			if (!sessionState.configRlz.certificatePath.empty()) {
				curl_easy_setopt(curl, CURLOPT_CAINFO, sessionState.configRlz.certificatePath.c_str());
			}

			string bodyJsonString = bodyJson.toStyledString();
			if (CURLE_OK != (code = prepareCurlPOST(curl, sessionState, bodyJsonString,
				&responseData, &chunk)))
			{
				logUpdateFailCurl(sessionState, code);
				break;
			}

			P_DEBUG("sending: " << bodyJsonString);
			if (CURLE_OK != (code = sendAndReceive(curl, &responseData, &responseCode))) {
				logUpdateFailCurl(sessionState, code);
				break;
			}

			// 3a. Verify response: HTTP code
			if (responseCode != 200) {
				logUpdateFailHttp(sessionState, (int) responseCode);
				break;
			}

			Json::Reader reader;
			Json::Value responseJson;
			if (!reader.parse(responseData, responseJson, false)) {
				logUpdateFailResponse("json parse", responseData);
				break;
			}
			P_DEBUG("received: " << responseData);

			// 3b. Verify response: signature
			if (!responseJson.isObject() || !responseJson["data"].isString() || !responseJson["signature"].isString()) {
				logUpdateFailResponse("missing response fields", responseData);
				break;
			}

			string signature64 = responseJson["signature"].asString();
			string data64 = responseJson["data"].asString();

			signatureChars = (char *)malloc(modp_b64_decode_len(signature64.length()));
			dataChars = (char *)malloc(modp_b64_decode_len(data64.length()));
			if (signatureChars == NULL || dataChars == NULL) {
				logUpdateFailResponse("out of memory", responseData);
				break;
			}
			int signatureLen;
			signatureLen = modp_b64_decode(signatureChars, signature64.c_str(), signature64.length());
			if (signatureLen <= 0) {
				logUpdateFailResponse("corrupted signature", responseData);
				break;
			}

			if (!crypto.verifySignature(serverPubKeyPath, signatureChars, signatureLen, data64)) {
				logUpdateFailResponse("untrusted or forged signature", responseData);
				break;
			}

			// 3c. Verify response: check required fields, nonce
			int dataLen;
			dataLen = modp_b64_decode(dataChars, data64.c_str(), data64.length());
			if (dataLen <= 0) {
				logUpdateFailResponse("corrupted data", responseData);
				break;
			}
			dataChars[dataLen] = '\0';

			Json::Value responseDataJson;
			if (!reader.parse(dataChars, responseDataJson, false)) {
				logUpdateFailResponse("unparseable data", responseData);
				break;
			}
			P_DEBUG("data content (signature OK): " << responseDataJson.toStyledString());

			if (!responseDataJson.isObject() || !responseDataJson["update"].isInt() || !responseDataJson["nonce"].isString()) {
				logUpdateFailResponse("missing data fields", responseData);
				break;
			}

			if (nonce != responseDataJson["nonce"].asString()) {
				logUpdateFailResponse("nonce mismatch, possible replay attack", responseData);
				break;
			}

			// 4. The main point: is there an update, and when is the next check?
			int update = responseDataJson["update"].asInt();

			if (responseDataJson["backoff"].isInt()) {
				backoffMin = responseDataJson["backoff"].asInt();
			}

			if (update == 1 && !responseDataJson["version"].isString()) {
				logUpdateFailResponse("update available, but version field missing", responseData);
				break;
			}

			if (update == 0) {
				unsigned int checkIntervalSec;
				{
					boost::lock_guard<boost::mutex> l(configSyncher);
					checkIntervalSec = config["interval"].asUInt();
				}
				logUpdateSuccess(update, "Security update check: no update found (next check in "
					+ toString(checkIntervalSec / (60*60)) + " hours)");
			} else {
				logUpdateSuccess(update, "A security update is available for your version (" PASSENGER_VERSION
					") of " PROGRAM_NAME ". We strongly recommend upgrading to version " +
					responseDataJson["version"].asString() + ".");
			}

			// 5. Shown independently of whether there is an update so that the server can provide general warnings
			// (e.g. about server-side detected MITM attack)
			if (responseDataJson["log"].isString()) {
				string additional = responseDataJson["log"].asString();
				if (additional.length() > 0) {
					logUpdateSuccessAdditional("Additional security update check information: " + additional);
				}
			}
		} while (false);

#if BOOST_OS_MACOS && PRE_HIGH_SIERRA
		// remove the security update check key from the user's keychain so that if we are stopped/crash and are upgraded or reinstalled before restarting we don't have permission problems
		crypto.killKey(CLIENT_CERT_LABEL);
#endif

		if (signatureChars) {
			free(signatureChars);
		}
		if (dataChars) {
			free(dataChars);
		}
		curl_slist_free_all(chunk);
		curl_easy_cleanup(curl);

		return backoffMin;
	}

	bool prepareConfigChange(const Json::Value &updates,
		vector<ConfigKit::Error> &errors, ConfigChangeRequest &req)
	{
		{
			boost::lock_guard<boost::mutex> l(configSyncher);
			req.config.reset(new ConfigKit::Store(config, updates, errors));
		}
		if (errors.empty()) {
			req.configRlz.reset(new ConfigRealization(*req.config));
		}
		return errors.empty();
	}

	void commitConfigChange(ConfigChangeRequest &req) BOOST_NOEXCEPT_OR_NOTHROW {
		boost::lock_guard<boost::mutex> l(configSyncher);
		config.swap(*req.config);
		configRlz.swap(*req.configRlz);
	}

	Json::Value inspectConfig() const {
		boost::lock_guard<boost::mutex> l(configSyncher);
		return config.inspect();
	}
};

} // namespace Passenger

#endif /* _PASSENGER_SECURITY_UPDATE_CHECKER_H_ */
