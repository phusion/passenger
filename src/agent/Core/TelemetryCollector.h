/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_TELEMETRY_COLLECTOR_H_
#define _PASSENGER_TELEMETRY_COLLECTOR_H_

#include <string>
#include <vector>
#include <limits>
#include <cstddef>
#include <cstdlib>
#include <cassert>

#include <boost/cstdint.hpp>
#include <boost/bind.hpp>
#include <oxt/thread.hpp>
#include <oxt/backtrace.hpp>

#include <curl/curl.h>

#include <Constants.h>
#include <Exceptions.h>
#include <Core/Controller.h>
#include <LoggingKit/LoggingKit.h>
#include <ConfigKit/ConfigKit.h>
#include <Utils/Curl.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {
namespace Core {

using namespace std;


class TelemetryCollector {
public:
	/*
	 * BEGIN ConfigKit schema: Passenger::Core::TelemetryCollector::Schema
	 * (do not edit: following text is automatically generated
	 * by 'rake configkit_schemas_inline_comments')
	 *
	 *   ca_certificate_path   string             -   -
	 *   debug_curl            boolean            -   default(false)
	 *   disabled              boolean            -   default(false)
	 *   final_run_timeout     unsigned integer   -   default(5)
	 *   first_interval        unsigned integer   -   default(7200)
	 *   interval              unsigned integer   -   default(21600)
	 *   interval_jitter       unsigned integer   -   default(7200)
	 *   proxy_url             string             -   -
	 *   timeout               unsigned integer   -   default(180)
	 *   url                   string             -   default("https://anontelemetry.phusionpassenger.com/v1/collect.json")
	 *   verify_server         boolean            -   default(true)
	 *
	 * END
	 */
	class Schema: public ConfigKit::Schema {
	private:
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
			add("url", STRING_TYPE, OPTIONAL, "https://anontelemetry.phusionpassenger.com/v1/collect.json");
			// Should be in the form: scheme://user:password@proxy_host:proxy_port
			add("proxy_url", STRING_TYPE, OPTIONAL);
			add("ca_certificate_path", STRING_TYPE, OPTIONAL);
			add("verify_server", BOOL_TYPE, OPTIONAL, true);
			add("first_interval", UINT_TYPE, OPTIONAL, 2 * 60 * 60);
			add("interval", UINT_TYPE, OPTIONAL, 6 * 60 * 60);
			add("interval_jitter", UINT_TYPE, OPTIONAL, 2 * 60 * 60);
			add("debug_curl", BOOL_TYPE, OPTIONAL, false);
			add("timeout", UINT_TYPE, OPTIONAL, 180);
			add("final_run_timeout", UINT_TYPE, OPTIONAL, 5);

			addValidator(validateProxyUrl);

			finalize();
		}
	};

	struct ConfigRealization {
		CurlProxyInfo proxyInfo;
		string url;
		string caCertificatePath;

		ConfigRealization(const ConfigKit::Store &config)
			: proxyInfo(prepareCurlProxy(config["proxy_url"].asString())),
			  url(config["url"].asString()),
			  caCertificatePath(config["ca_certificate_path"].asString())
			{ }

		void swap(ConfigRealization &other) BOOST_NOEXCEPT_OR_NOTHROW {
			proxyInfo.swap(other.proxyInfo);
			url.swap(other.url);
			caCertificatePath.swap(other.caCertificatePath);
		}
	};

	struct ConfigChangeRequest {
		boost::scoped_ptr<ConfigKit::Store> config;
		boost::scoped_ptr<ConfigRealization> configRlz;
	};

	struct TelemetryData {
		vector<boost::uint64_t> requestsHandled;
		MonotonicTimeUsec timestamp;
	};

private:
	/*
	 * Since the telemetry collector runs in a separate thread,
	 * and the configuration can change while the collector is active,
	 * we make a copy of the current configuration at the beginning
	 * of each collection cycle.
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
	TelemetryData lastTelemetryData;
	oxt::thread *collectorThread;

	void threadMain() {
		TRACE_POINT();

		{
			// Sleep for a short while to allow interruption during the Apache integration
			// double startup procedure, this prevents running the update check twice
			boost::unique_lock<boost::mutex> l(configSyncher);
			ConfigKit::Store config(this->config);
			l.unlock();

			unsigned int backoffSec = config["first_interval"].asUInt()
				+ calculateIntervalJitter(config);
			P_DEBUG("Next anonymous telemetry collection in " <<
				distanceOfTimeInWords(SystemTime::get() + backoffSec));
			boost::this_thread::sleep_for(boost::chrono::seconds(backoffSec));
		}

		while (!boost::this_thread::interruption_requested()) {
			UPDATE_TRACE_POINT();
			unsigned int backoffSec = 0;
			try {
				backoffSec = runOneCycle();
			} catch (const oxt::tracable_exception &e) {
				P_ERROR(e.what() << "\n" << e.backtrace());
			}

			if (backoffSec == 0) {
				boost::unique_lock<boost::mutex> l(configSyncher);
				backoffSec = config["interval"].asUInt()
					+ calculateIntervalJitter(config);
			}

			UPDATE_TRACE_POINT();
			P_DEBUG("Next anonymous telemetry collection in "
				<< distanceOfTimeInWords(SystemTime::get() + backoffSec));
			boost::this_thread::sleep_for(boost::chrono::seconds(backoffSec));
		}
	}

	static unsigned int calculateIntervalJitter(const ConfigKit::Store &config) {
		unsigned int jitter = config["interval_jitter"].asUInt();
		if (jitter == 0) {
			return 0;
		} else {
			return std::rand() % jitter;
		}
	}

	// Virtual to allow mocking in unit tests.
	virtual TelemetryData collectTelemetryData(bool isFinalRun) const {
		TRACE_POINT();
		TelemetryData tmData;
		unsigned int counter = 0;
		boost::mutex syncher;
		boost::condition_variable cond;

		tmData.requestsHandled.resize(controllers.size(), 0);

		UPDATE_TRACE_POINT();
		for (unsigned int i = 0; i < controllers.size(); i++) {
			if (isFinalRun) {
				inspectController(&tmData, controllers[i], i, &counter,
					&syncher, &cond);
			} else {
				controllers[i]->getContext()->libev->runLater(boost::bind(
					&TelemetryCollector::inspectController, this, &tmData,
					controllers[i], i, &counter, &syncher, &cond));
			}
		}

		UPDATE_TRACE_POINT();
		{
			boost::unique_lock<boost::mutex> l(syncher);
			while (counter != controllers.size()) {
				cond.wait(l);
			}
		}

		tmData.timestamp = SystemTime::getMonotonicUsecWithGranularity
			<SystemTime::GRAN_1SEC>();
		return tmData;
	}

	void inspectController(TelemetryData *tmData, Controller *controller,
		unsigned int index, unsigned int *counter, boost::mutex *syncher,
		boost::condition_variable *cond) const
	{
		boost::unique_lock<boost::mutex> l(*syncher);
		tmData->requestsHandled[index] = controller->totalRequestsBegun;
		(*counter)++;
		cond->notify_one();
	}

	string createRequestBody(const TelemetryData &tmData) const {
		Json::Value doc;
		boost::uint64_t totalRequestsHandled = 0;

		P_ASSERT_EQ(tmData.requestsHandled.size(),
			lastTelemetryData.requestsHandled.size());

		for (unsigned int i = 0; i < tmData.requestsHandled.size(); i++) {
			if (tmData.requestsHandled[i] >= lastTelemetryData.requestsHandled[i]) {
				totalRequestsHandled += tmData.requestsHandled[i]
					- lastTelemetryData.requestsHandled[i];
			} else {
				// Counter overflowed
				totalRequestsHandled += std::numeric_limits<boost::uint64_t>::max()
					- lastTelemetryData.requestsHandled[i]
					+ 1
					+ tmData.requestsHandled[i];
			}
		}

		doc["requests_handled"] = (Json::UInt64) totalRequestsHandled;
		doc["begin_time"] = (Json::UInt64) monoTimeToRealTime(
			lastTelemetryData.timestamp);
		doc["end_time"] = (Json::UInt64) monoTimeToRealTime(
			tmData.timestamp);
		#ifdef PASSENGER_IS_ENTERPRISE
			doc["edition"] = "enterprise";
		#else
			doc["edition"] = "oss";
		#endif

		return doc.toStyledString();
	}

	static time_t monoTimeToRealTime(MonotonicTimeUsec monoTime) {
		MonotonicTimeUsec monoNow = SystemTime::getMonotonicUsecWithGranularity
			<SystemTime::GRAN_1SEC>();
		unsigned long long realNow = SystemTime::getUsec();
		MonotonicTimeUsec diff;

		if (monoNow >= monoTime) {
			diff = monoNow - monoTime;
			return (realNow - diff) / 1000000;
		} else {
			diff = monoTime - monoNow;
			return (realNow + diff) / 1000000;
		}
	}

	static CURL *prepareCurlRequest(SessionState &sessionState, bool isFinalRun,
		struct curl_slist **headers, char *lastErrorMessage,
		const string &requestBody, string &responseData)
	{
		CURL *curl;
		CURLcode code;

		curl = curl_easy_init();
		if (curl == NULL) {
			P_ERROR("Error initializing libcurl");
			return NULL;
		}

		code = curl_easy_setopt(curl, CURLOPT_VERBOSE,
			sessionState.config["debug_curl"].asBool() ? 1L : 0L);
		if (code != CURLE_OK) {
			goto error;
		}

		code = setCurlDefaultCaInfo(curl);
		if (code != CURLE_OK) {
			goto error;
		}

		code = setCurlProxy(curl, sessionState.configRlz.proxyInfo);
		if (code != CURLE_OK) {
			goto error;
		}

		code = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
		if (code != CURLE_OK) {
			goto error;
		}

		code = curl_easy_setopt(curl, CURLOPT_URL,
			sessionState.configRlz.url.c_str());
		if (code != CURLE_OK) {
			goto error;
		}

		code = curl_easy_setopt(curl, CURLOPT_HTTPGET, 0);
		if (code != CURLE_OK) {
			goto error;
		}

		code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
		if (code != CURLE_OK) {
			goto error;
		}

		code = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, requestBody.length());
		if (code != CURLE_OK) {
			goto error;
		}

		*headers = curl_slist_append(NULL, "Content-Type: application/json");
		code = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *headers);
		if (code != CURLE_OK) {
			goto error;
		}

		if (!sessionState.configRlz.caCertificatePath.empty()) {
			code = curl_easy_setopt(curl, CURLOPT_CAINFO,
				sessionState.configRlz.caCertificatePath.c_str());
			if (code != CURLE_OK) {
				goto error;
			}
		}

		if (sessionState.config["verify_server"].asBool()) {
			// These should be on by default, but make sure.
			code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
			if (code != CURLE_OK) {
				goto error;
			}
			code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
			if (code != CURLE_OK) {
				goto error;
			}
		} else {
			code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			if (code != CURLE_OK) {
				goto error;
			}
			code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
			if (code != CURLE_OK) {
				goto error;
			}
		}

		code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, lastErrorMessage);
		if (code != CURLE_OK) {
			goto error;
		}
		code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receiveResponseBytes);
		if (code != CURLE_OK) {
			goto error;
		}
		code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
		if (code != CURLE_OK) {
			goto error;
		}

		// setopt failure(s) below don't abort the check.
		if (isFinalRun) {
			curl_easy_setopt(curl, CURLOPT_TIMEOUT,
				sessionState.config["final_run_timeout"].asUInt());
		} else {
			curl_easy_setopt(curl, CURLOPT_TIMEOUT,
				sessionState.config["timeout"].asUInt());
		}

		return curl;

		error:
		curl_easy_cleanup(curl);
		curl_slist_free_all(*headers);
		P_ERROR("Error setting libcurl handle parameters: " << curl_easy_strerror(code));
		return NULL;
	}

	static size_t receiveResponseBytes(void *buffer, size_t size,
		size_t nmemb, void *userData)
	{
		string *responseData = (string *) userData;
		responseData->append((const char *) buffer, size * nmemb);
		return size * nmemb;
	}

	// Virtual to allow mocking in unit tests.
	virtual CURLcode performCurlAction(CURL *curl, const char *lastErrorMessage,
		const string &_requestBody, // only used by unit tests
		string &_responseData, // only used by unit tests
		long &responseCode)
	{
		TRACE_POINT();
		CURLcode code = curl_easy_perform(curl);
		if (code != CURLE_OK) {
			P_ERROR("Error contacting anonymous telemetry server: "
				<< lastErrorMessage);
			return code;
		}

		code = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
		if (code != CURLE_OK) {
			P_ERROR("Error querying libcurl handle for HTTP response code: "
				<< curl_easy_strerror(code));
			return code;
		}

		return CURLE_OK;
	}

	static bool responseCodeSupported(long code) {
		return code == 200 || code == 400 || code == 422 || code == 500;
	}

	static bool parseResponseBody(const string &responseData, Json::Value &jsonBody) {
		Json::Reader reader;
		if (reader.parse(responseData, jsonBody, false)) {
			return true;
		} else {
			P_ERROR("Error in anonymous telemetry server response:"
				" JSON response parse error: " << reader.getFormattedErrorMessages()
				<< "; data: \"" << cEscapeString(responseData) << "\"");
			return false;
		}
	}

	static bool validateResponseBody(const Json::Value &jsonBody) {
		if (!jsonBody.isObject()) {
			P_ERROR("Error in anonymous telemetry server response:"
				" JSON response is not an object (data: "
				<< stringifyJson(jsonBody) << ")");
			return false;
		}
		if (!jsonBody.isMember("data_processed")) {
			P_ERROR("Error in anonymous telemetry server response:"
				" JSON response must contain a 'data_processed' field (data: "
				<< stringifyJson(jsonBody) << ")");
			return false;
		}
		if (!jsonBody["data_processed"].isBool()) {
			P_ERROR("Error in anonymous telemetry server response:"
				" 'data_processed' field must be a boolean (data: "
				<< stringifyJson(jsonBody) << ")");
			return false;
		}
		if (jsonBody.isMember("backoff") && !jsonBody["backoff"].isUInt()) {
			P_ERROR("Error in anonymous telemetry server response:"
				" 'backoff' field must be an unsigned integer (data: "
				<< stringifyJson(jsonBody) << ")");
			return false;
		}
		if (jsonBody.isMember("log_message") && !jsonBody["log_message"].isString()) {
			P_ERROR("Error in anonymous telemetry server response:"
				" 'log_message' field must be a string (data: "
				<< stringifyJson(jsonBody) << ")");
			return false;
		}
		return true;
	}

	unsigned int handleResponseBody(const TelemetryData &tmData,
		const Json::Value &jsonBody)
	{
		unsigned int backoffSec = 0;

		if (jsonBody["data_processed"].asBool()) {
			lastTelemetryData = tmData;
		}
		if (jsonBody.isMember("backoff")) {
			backoffSec = jsonBody["backoff"].asUInt();
		}
		if (jsonBody.isMember("log_message")) {
			P_NOTICE("Message from " PROGRAM_AUTHOR ": " << jsonBody["log_message"].asString());
		}

		return backoffSec;
	}

public:
	// Dependencies
	vector<Controller *> controllers;

	TelemetryCollector(const Schema &schema,
		const Json::Value &initialConfig = Json::Value(),
		const ConfigKit::Translator &translator = ConfigKit::DummyTranslator())
		: config(schema, initialConfig, translator),
		  configRlz(config),
		  collectorThread(NULL)
		{ }

	virtual ~TelemetryCollector() {
		stop();
	}

	void initialize() {
		if (controllers.empty()) {
			throw RuntimeException("controllers must be initialized");
		}
		lastTelemetryData.requestsHandled.resize(controllers.size(), 0);
		lastTelemetryData.timestamp =
			SystemTime::getMonotonicUsecWithGranularity<SystemTime::GRAN_1SEC>();
	}

	void start() {
		assert(!lastTelemetryData.requestsHandled.empty());
		collectorThread = new oxt::thread(
			boost::bind(&TelemetryCollector::threadMain, this),
			"Telemetry collector",
			1024 * 512
		);
	}

	void stop() {
		if (collectorThread != NULL) {
			collectorThread->interrupt_and_join();
			delete collectorThread;
			collectorThread = NULL;
		}
	}

	unsigned int runOneCycle(bool isFinalRun = false) {
		TRACE_POINT();
		boost::unique_lock<boost::mutex> l(configSyncher);
		SessionState sessionState(config, configRlz);
		l.unlock();

		if (sessionState.config["disabled"].asBool()) {
			P_DEBUG("Telemetry collector disabled; not sending anonymous telemetry data");
			return 0;
		}

		UPDATE_TRACE_POINT();
		TelemetryData tmData = collectTelemetryData(isFinalRun);

		UPDATE_TRACE_POINT();
		CURL *curl = NULL;
		CURLcode code;
		struct curl_slist *headers = NULL;
		string requestBody = createRequestBody(tmData);
		string responseData;
		char lastErrorMessage[CURL_ERROR_SIZE] = "unknown error";
		Json::Value jsonBody;

		curl = prepareCurlRequest(sessionState, isFinalRun, &headers,
			lastErrorMessage, requestBody, responseData);
		if (curl == NULL) {
			// Error message already printed
			goto error;
		}

		P_INFO("Sending anonymous telemetry data to " PROGRAM_AUTHOR);
		P_DEBUG("Telemetry server URL is: " << sessionState.configRlz.url);
		P_DEBUG("Telemetry data to be sent is: " << requestBody);

		UPDATE_TRACE_POINT();
		long responseCode;
		code = performCurlAction(curl, lastErrorMessage, requestBody,
			responseData, responseCode);
		if (code != CURLE_OK) {
			// Error message already printed
			goto error;
		}

		UPDATE_TRACE_POINT();
		P_DEBUG("Response from telemetry server: status=" << responseCode
			<< ", body=" << responseData);

		if (!responseCodeSupported(responseCode)) {
			P_ERROR("Error from anonymous telemetry server:"
				" response status not supported: " << responseCode);
			goto error;
		}

		if (!parseResponseBody(responseData, jsonBody)
		 || !validateResponseBody(jsonBody))
		{
			// Error message already printed
			goto error;
		}

		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);

		return handleResponseBody(tmData, jsonBody);

		error:
		curl_slist_free_all(headers);
		if (curl != NULL) {
			curl_easy_cleanup(curl);
		}
		return 0;
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


} // namespace Core
} // namespace Passenger

#endif /* _PASSENGER_TELEMETRY_COLLECTOR_H_ */
