/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2016 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  See LICENSE file for license information.
 */
#ifndef _PASSENGER_UPDATE_CHECKER_H_
#define _PASSENGER_UPDATE_CHECKER_H_

#include <string>
#include <oxt/thread.hpp>
#include <oxt/backtrace.hpp>

#include <Crypto.h>
#include <Utils/Curl.h>
#include <modp_b64.h>

#if BOOST_OS_MACOS
#include <sys/syslimits.h>
#include <unistd.h>
#endif

namespace Passenger {

using namespace std;
using namespace oxt;

#define CHECK_HOST_DEFAULT "securitycheck.phusionpassenger.com"

#define CHECK_URL_DEFAULT "https://" CHECK_HOST_DEFAULT ":443/v1/check.json"
#define MIN_CHECK_BACKOFF_SEC 12 * 60 * 60
#define MAX_CHECK_BACKOFF_SEC 7 * 24 * 60 * 60

// Password for the .p12 client certificate (because .p12 is required to be pwd protected on some
// implementations). We're OK with hardcoding because the certs are not secret anyway, and they're not used
// for client id/auth (just to easily deflect unrelated probes from the server endpoint).
#define CLIENT_CERT_PWD "p6PBhK8KtorrhMxHnH855MvF"
#define CLIENT_CERT_LABEL "Phusion Passenger Open Source"

#define POSSIBLE_MITM_RESOLUTION "(if this error persists check your connection security or try upgrading " SHORT_PROGRAM_NAME ")"
/**
 * If started, this class periodically (default: daily, immediate start) checks whether there are any important
 * security updates available (updates that don't fix security issues are not reported). The result is logged
 * (level 3:notice if no update, level 1:error otherwise), and all further action is left to the user (there is
 * no auto-update mechanism).
 */
class SecurityUpdateChecker {

private:
	oxt::thread *updateCheckThread;
	long checkIntervalSec;
	string clientCertPath; // client cert (PKCS#12), checked by server
	string serverPubKeyPath; // for checking signature
	string proxyAddress;
	string serverIntegration;
	string serverVersion;
	CurlProxyInfo proxyInfo;
	Crypto *crypto;
#if BOOST_OS_MACOS
	SecKeychainRef defaultKeychain;
	SecKeychainRef keychain;
	bool usingPassengerKeychain;
#endif

	void threadMain() {
		TRACE_POINT();
		// Sleep for a short while to allow interruption during the Apache integration double startup procedure, this prevents running the update check twice
		boost::this_thread::sleep_for(boost::chrono::seconds(2));
		while (!this_thread::interruption_requested()) {
			UPDATE_TRACE_POINT();
			int backoffMin = 0;
			try {
				backoffMin = checkAndLogSecurityUpdate();
			} catch (const tracable_exception &e) {
				P_ERROR(e.what() << "\n" << e.backtrace());
			}
			UPDATE_TRACE_POINT();
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


	void logUpdateFailCurl(CURLcode code) {
		// At this point anything could be wrong, from unloadable certificates to server not found, etc.
		// Let's try to enrich the log message in case there are known solutions or workarounds (e.g. "use proxy").
		string error = curl_easy_strerror(code);

		switch (code) {
			case CURLE_SSL_CERTPROBLEM:
				error.append(" at " + clientCertPath + " (try upgrading or reinstalling " SHORT_PROGRAM_NAME ")");
				break;

			case CURLE_COULDNT_RESOLVE_HOST:
				error.append(" while connecting to " CHECK_HOST_DEFAULT " (check your DNS)");
				break;

			case CURLE_COULDNT_CONNECT:
				if (proxyAddress.empty()) {
					error.append(" for " CHECK_URL_DEFAULT " " POSSIBLE_MITM_RESOLUTION);
				} else {
					error.append(" for " CHECK_URL_DEFAULT " using proxy " + proxyAddress +
						" (if this error persists check your firewall and/or proxy settings)");
				}
				break;

			case CURLE_COULDNT_RESOLVE_PROXY:
				error.append(" for proxy address " + proxyAddress);
				break;

			case CURLE_SSL_CACERT:
				// Peer certificate cannot be authenticated with given / known CA certificates. This would happen
				// for MITM but could also be a truststore issue.
			case CURLE_PEER_FAILED_VERIFICATION:
				// The remote server's SSL certificate or SSH md5 fingerprint was deemed not OK.
				error.append(" while connecting to " CHECK_HOST_DEFAULT "; check that your connection is secure and that the "
						"truststore is valid. If the problem persists, you can also try upgrading or reinstalling " SHORT_PROGRAM_NAME);
				break;

			case CURLE_SSL_CACERT_BADFILE:
				error.append(" while connecting to " CHECK_URL_DEFAULT " " +
						(proxyAddress.empty() ? "" : "using proxy " + proxyAddress) + "; this might happen if the nss backend "
						"is installed for libcurl instead of GnuTLS or OpenSSL. If the problem persists, you can also try upgrading "
						"or reinstalling " SHORT_PROGRAM_NAME);
				break;

			// Fallthroughs to default:
			case CURLE_SSL_CONNECT_ERROR:
				// A problem occurred somewhere in the SSL/TLS handshake. Not sure what's up, but in this case the
				// error buffer (printed in DEBUG) should pinpoint the problem slightly more.
			case CURLE_OPERATION_TIMEDOUT:
				// This is not a normal connect timeout, there are some refs to it occuring while downloading large
				// files, but we don't do that so fall through to default.
			default:
				error.append(" while connecting to " CHECK_URL_DEFAULT " " +
						(proxyAddress.empty() ? "" : "using proxy " + proxyAddress) + " " POSSIBLE_MITM_RESOLUTION);
				break;
		}

		logUpdateFail(error);

#if !BOOST_OS_MACOS
		unsigned long cryptoErrorCode = ERR_get_error();
		if (cryptoErrorCode == 0) {
			logUpdateFailAdditional("CURLcode" + to_string(code));
		} else {
			char buf[500];
			ERR_error_string(cryptoErrorCode, buf);
			logUpdateFailAdditional("CURLcode: " + to_string(code) + ", Crypto: " + to_string(cryptoErrorCode) + " " + buf);
		}
#endif
	}

	void logUpdateFailHttp(int httpCode) {
		string error;

		switch (httpCode) {
			case 404:
				error.append("url not found: " CHECK_URL_DEFAULT " " POSSIBLE_MITM_RESOLUTION);
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
				error = "HTTP " + to_string(httpCode) + " while connecting to " CHECK_URL_DEFAULT " " POSSIBLE_MITM_RESOLUTION;
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
	CURLcode prepareCurlPOST(CURL *curl, string &bodyJsonString, string *responseData, struct curl_slist **chunk) {
		CURLcode code;

		// Hint for advanced debugging: curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1))) {
			return code;
		}
		if (CURLE_OK != (code = curl_easy_setopt(curl, CURLOPT_URL, CHECK_URL_DEFAULT))) {
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

#if BOOST_OS_MACOS
		// if not using a private keychain, preauth the security update check key in the user's keychain (this is for libcurl's benefit because they don't bother to authorize themselves to use the keys they import)
		if (!usingPassengerKeychain && !crypto->preAuthKey(clientCertPath.c_str(), CLIENT_CERT_PWD, CLIENT_CERT_LABEL)) {
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
		if (CURLE_OK != (code = setCurlProxy(curl, proxyInfo))) {
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

public:

	/**
	 * proxy is optional and should be in the form: scheme://user:password@proxy_host:proxy_port
	 *
	 * serverIntegration should be one of { nginx, apache, standalone nginx, standalone builtin }, whereby
	 * serverVersion is the version of Nginx or Apache, if relevant (otherwise empty)
	 */
	SecurityUpdateChecker(const ResourceLocator &locator, const string &proxy, const string &serverIntegration, const string &serverVersion, const string &instancePath) {
		crypto = new Crypto();
		updateCheckThread = NULL;
		checkIntervalSec = 0;
#if BOOST_OS_MACOS
		clientCertPath = locator.getResourcesDir() + "/update_check_client_cert.p12";
		// Used to keep track of which approach we are using, false means we are preauthing the key in the running user's own keychain; true means we create a private keychain and set it as the default
		usingPassengerKeychain = false;
		defaultKeychain = NULL;
		keychain = NULL;
		OSStatus status = 0;
		char pathName [PATH_MAX];
		UInt32 length = PATH_MAX;
		memset(pathName, 0, PATH_MAX);

		status = SecKeychainCopyDefault(&defaultKeychain);
		if (status) {
			CFStringRef str = SecCopyErrorMessageString(status, NULL);
			P_ERROR(string("Getting default keychain failed: ") +
					CFStringGetCStringPtr(str, kCFStringEncodingUTF8) +
					" Passenger will not attempt to create a private keychain.");
			CFRelease(str);
		} else {
			status = SecKeychainGetPath(defaultKeychain, &length, pathName);
			P_DEBUG(string("username is: ") + getProcessUsername());
			if (status) {
				CFStringRef str = SecCopyErrorMessageString(status, NULL);
				P_ERROR(string("Checking default keychain path failed: ") +
						CFStringGetCStringPtr(str, kCFStringEncodingUTF8) +
						" Passenger may use system keychain.");
				CFRelease(str);
				pathName[0] = 0; // ensure the pathName compares cleanly
			} else {
				P_DEBUG(string("Old default keychain is: ") + pathName);
			}
		}
		// we don't care so much about which user we are, what we care about is is they have their own keychain, if the default keychain is the system keychain, then we need to try and create our own to avoid permissions issues
		if (strcmp(pathName, "/Library/Keychains/System.keychain") == 0) {
			usingPassengerKeychain = true;
			const uint size = 512;
			uint8_t keychainPassword[size];
			if (!crypto->generateRandomChars(keychainPassword, size)) {
				P_CRITICAL("Creating password for Passenger default keychain failed.");
				usingPassengerKeychain = false;
			} else {
				string keychainDir = instancePath;
				if (instancePath.length() == 0) {
					char currentPath[PATH_MAX];
					if (!getcwd(currentPath, PATH_MAX)) {
						P_ERROR(string("Failed to get cwd: ") + strerror(errno) + " Attempting to use relative path '.'");
						keychainDir = ".";
					} else {
						keychainDir = string(currentPath);
					}
				}
				// create keychain with long random password, then discard password after creation. We receive the keychain unlocked, and no-one else needs to access the keychain.
				status = SecKeychainCreate((keychainDir + "/passenger.keychain").c_str(), size, keychainPassword, false, NULL, &keychain);
				memset(keychainPassword, 0, size);
				if (status) {
					CFStringRef str = SecCopyErrorMessageString(status, NULL);
					P_ERROR(string("Creating Passenger default keychain failed: ") +
							CFStringGetCStringPtr(str, kCFStringEncodingUTF8) +
							" Passenger may fail to access system keychain.");
					CFRelease(str);
					usingPassengerKeychain = false;
				} else {
					// set keychain as default so libcurl uses it.
					status = SecKeychainSetDefault(keychain);
					if (status) {
						CFStringRef str = SecCopyErrorMessageString(status, NULL);
						P_ERROR(string("Setting Passenger default keychain failed: ") +
								CFStringGetCStringPtr(str, kCFStringEncodingUTF8) +
								" Passenger may fail to access system keychain.");
						CFRelease(str);
						usingPassengerKeychain = false;
					} else if (!crypto->preAuthKey(clientCertPath.c_str(), CLIENT_CERT_PWD, CLIENT_CERT_LABEL)) {
						P_ERROR("Failed to preauthorize Passenger Client Cert, you may experience popups from the Keychain.");
				 /* } else {
						we have loaded the security update check key into the private keychain with the correct permissions, so libcurl should be able to use it. */
					}
				}
			}
		}
#else
		clientCertPath = locator.getResourcesDir() + "/update_check_client_cert.pem";
#endif
		serverPubKeyPath = locator.getResourcesDir() + "/update_check_server_pubkey.pem";
		proxyAddress = proxy;
		this->serverIntegration = serverIntegration;
		this->serverVersion = serverVersion;
		try {
			proxyInfo = prepareCurlProxy(proxyAddress);
		} catch (const ArgumentException &e) {
			assert(!proxyInfo.valid);
			proxyAddress = "Invalid proxy address for security update check: \"" +
					proxyAddress + "\": " + e.what();
		}
	}

	virtual ~SecurityUpdateChecker() {
		if (updateCheckThread != NULL) {
			updateCheckThread->interrupt_and_join();
			delete updateCheckThread;
			updateCheckThread = NULL;
		}
		if (crypto) {
			delete crypto;
		}
#if BOOST_OS_MACOS
		// if using a private keychain, cleanup keychain on shutdown
		if (usingPassengerKeychain) {
			OSStatus status = 0;
			if (defaultKeychain) {
				status = SecKeychainSetDefault(defaultKeychain);
				if (status) {
					CFStringRef str = SecCopyErrorMessageString(status, NULL);
					P_ERROR(string("Restoring default keychain failed: ") +
							CFStringGetCStringPtr(str, kCFStringEncodingUTF8));
					CFRelease(str);
				}
				CFRelease(defaultKeychain);
			}
			if (keychain) {
				status = SecKeychainDelete(keychain);
				if (status) {
					CFStringRef str = SecCopyErrorMessageString(status, NULL);
					P_ERROR(string("Deleting Passenger private keychain failed: ") +
							CFStringGetCStringPtr(str, kCFStringEncodingUTF8));
					CFRelease(str);
				}
				CFRelease(keychain);
			}
		}
#endif
	}

	/**
	 * Starts a periodic check at every checkIntervalSec. For each check, the server may increase/decrease
	 * (within limits) the period until the next check (using the backoff parameter in the response).
	 *
	 * Assumes curl_global_init() was already performed.
	 */
	void start(long checkIntervalSec) {
		this->checkIntervalSec = checkIntervalSec;

		assert(checkIntervalSec >= MIN_CHECK_BACKOFF_SEC && checkIntervalSec <= MAX_CHECK_BACKOFF_SEC);
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
		P_ERROR("Security update check failed: " << error << " (next check in " << (checkIntervalSec / (60*60)) << " hours)");
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
		return crypto->generateAndAppendNonce(nonce);
	}

	/**
	 * Sends POST to CHECK_URL_DEFAULT (via SSL, with client cert) containing:
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

		// 1. Assemble data to send
		Json::Value bodyJson;

		bodyJson["passenger_version"] = PASSENGER_VERSION;

		bodyJson["server_integration"] = serverIntegration;
		bodyJson["server_version"] = serverVersion;
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
				logUpdateFailCurl(code);
				break;
			}

			// string localApprovedCert = "/your/ca.crt"; // for testing against a local server
			// curl_easy_setopt(curl, CURLOPT_CAINFO, localApprovedCert.c_str());

			if (!proxyInfo.valid) {
				// special case: delayed error in proxyAddress
				logUpdateFail(proxyAddress);
				break;
			}

			string bodyJsonString = bodyJson.toStyledString();
			if (CURLE_OK != (code = prepareCurlPOST(curl, bodyJsonString, &responseData, &chunk))) {
				logUpdateFailCurl(code);
				break;
			}

			P_DEBUG("sending: " << bodyJsonString);
			if (CURLE_OK != (code = sendAndReceive(curl, &responseData, &responseCode))) {
				logUpdateFailCurl(code);
				break;
			}

			// 3a. Verify response: HTTP code
			if (responseCode != 200) {
				logUpdateFailHttp((int) responseCode);
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

			signatureChars = (char *)malloc(signature64.length() + 1);
			dataChars = (char *)malloc(signature64.length() + 1);
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

			if (!crypto->verifySignature(serverPubKeyPath, signatureChars, signatureLen, data64)) {
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
				logUpdateSuccess(update, "Security update check: no update found (next check in " + toString(checkIntervalSec / (60*60)) + " hours)");
			} else {
				logUpdateSuccess(update, "A security update is available for your version (" PASSENGER_VERSION
					") of Passenger, we strongly recommend upgrading to version " +
					responseDataJson["version"].asString() + ".");
			}

			// 5. Shown independently of whether there is an update so that the server can provide general warnings
			// (e.g. about server-side detected MITM attack)
			if (responseDataJson["log"].isString()) {
				string additional = responseDataJson["log"].asString();
				if (additional.length() > 0) {
					logUpdateSuccessAdditional(" Additional information: " + additional);
				}
			}
		} while (0);

#if BOOST_OS_MACOS
		// if not using a private keychain remove the security update check key from the user's keychain so that if we are stopped/crash and are upgraded or reinstalled before restarting we don't have permission problems
		if (!usingPassengerKeychain) {
			crypto->killKey(CLIENT_CERT_LABEL);
		}
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

	static size_t receiveResponseBytes(void *buffer, size_t size, size_t nmemb, void *userData) {
		string *responseData = (string *) userData;
		responseData->append((const char *) buffer, size * nmemb);
		return size * nmemb;
	}

};
}

#endif /* _PASSENGER_UPDATE_CHECKER_H_ */
