/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_SERVER_LIVELINESS_CHECKER_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_SERVER_LIVELINESS_CHECKER_H_

#include <boost/weak_ptr.hpp>
#include <oxt/backtrace.hpp>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cassert>

#include <ev.h>
#include <curl/curl.h>
#include <jsoncpp/json.h>
#include <psg_sysqueue.h>

#include <Logging.h>
#include <Exceptions.h>
#include <DataStructures/StringKeyTable.h>
#include <Integrations/CurlLibevIntegration.h>
#include <Integrations/LibevJsonUtils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/SystemTime.h>
#include <UstRouter/RemoteSink/Common.h>
#include <UstRouter/RemoteSink/Server.h>

namespace Passenger {
namespace UstRouter {
namespace RemoteSink {

using namespace std;
using namespace boost;
using namespace oxt;


class ServerLivelinessChecker: public AbstractServerLivelinessChecker {
private:
	static const unsigned int CHECK_TIMEOUT = 120;

	struct TransferInfo: public CurlLibevIntegration::TransferInfo {
		ServerLivelinessChecker *checker;
		CURL * const curl;
		const unsigned int number;
		const ServerPtr server;
		const ev_tstamp startedAt;
		STAILQ_ENTRY(TransferInfo) next;
		string responseData;
		char errorBuf[CURL_ERROR_SIZE];

		TransferInfo(ServerLivelinessChecker *_checker, unsigned int _number,
			const ServerPtr &_server, ev_tstamp _startedAt)
			: checker(_checker),
			  curl(curl_easy_init()),
			  number(_number),
			  server(_server),
			  startedAt(_startedAt)
		{
			STAILQ_NEXT(this, next) = NULL;
			errorBuf[0] = '\0';
		}

		~TransferInfo() {
			curl_easy_cleanup(curl);
		}

		virtual void finish(CURL *curl, CURLcode code) {
			long httpCode = -1;
			P_ASSERT_EQ(curl, this->curl);

			if (code == CURLE_OK) {
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
			}

			checker->finishTransfer(this, code, httpCode,
				responseData, errorBuf);
		}
	};

	typedef boost::weak_ptr<Server> WeakServerPtr;
	typedef StringKeyTable<WeakServerPtr, SKT_EnableMoveSupport> ServerTable;
	STAILQ_HEAD(TransferInfoList, TransferInfo);

	Context * const context;
	ServerTable servers;
	TransferInfoList transferInfos;
	unsigned int nextTransferInfoNumber;
	unsigned int nTransferInfos;
	unsigned int nChecksInitiated;
	unsigned int nChecksFinished;
	ev_tstamp lastInitiateTime;
	ev_tstamp lastErrorTime;
	ev_tstamp nextCheckTime;
	string lastErrorMessage;
	struct ev_timer timer;


	static void onTimeout(EV_P_ ev_timer *timer, int revents) {
		ServerLivelinessChecker *self = static_cast<ServerLivelinessChecker *>(timer->data);
		self->checkEligibleServers();
	}

	struct ev_loop *getLoop() const {
		return context->loop;
	}

	void rescheduleWithServers(const vector<ServerPtr> &servers) {
		ev_tstamp nextCheckTime = std::numeric_limits<ev_tstamp>::max();
		vector<ServerPtr>::const_iterator it, end = servers.end();

		for (it = servers.begin(); it != end; it++) {
			const ServerPtr &server = *it;

			if (!server->isUp() && !server->isCheckingLiveliness()) {
				ev_tstamp t = server->getNextLivelinessCheckTime(ev_now(getLoop()));
				nextCheckTime = std::min(nextCheckTime, t);
			}
		}

		if (ev_is_active(&timer)) {
			ev_timer_stop(getLoop(), &timer);
		}
		if (nextCheckTime != std::numeric_limits<ev_tstamp>::max()) {
			ev_tstamp now = ev_now(getLoop());
			this->nextCheckTime = roundUpD(nextCheckTime, 5);
			ev_timer_set(&timer, this->nextCheckTime - now, 0);
			ev_timer_start(getLoop(), &timer);
		}
	}

	static size_t handleResponseData(char *ptr, size_t size, size_t nmemb, void *userdata) {
		TRACE_POINT();
		TransferInfo *transferInfo = static_cast<TransferInfo *>(userdata);
		transferInfo->responseData.append(ptr, size * nmemb);
		return size * nmemb;
	}

	void check(const ServerPtr &server) {
		TransferInfo *transferInfo;
		CURL *curl;
		ev_tstamp now = ev_now(getLoop());

		lastInitiateTime = now;
		nChecksInitiated++;

		try {
			transferInfo = new TransferInfo(this, nextTransferInfoNumber++,
				server, now);
			curl = transferInfo->curl;
		} catch (const std::bad_alloc &) {
			transferInfo = NULL;
			curl = NULL;
		}
		if (transferInfo == NULL
		 || curl == NULL
		 || OXT_UNLIKELY(shouldFailCheckInitiation(server)))
		{
			char buf[1024];

			snprintf(buf, sizeof(buf), "[RemoteSink sender] Error initiating liveliness"
				" check for gateway %s: unable to allocate memory",
				server->getPingUrl().c_str());
			P_ERROR(buf);

			snprintf(buf, sizeof(buf), "Error initiating liveliness"
				" check for gateway %s: unable to allocate memory",
				server->getPingUrl().c_str());
			lastErrorTime = now;
			lastErrorMessage = buf;

			nChecksFinished++;
			delete transferInfo;
			reschedule();
			return;
		}

		curl_easy_setopt(curl, CURLOPT_URL, server->getPingUrl().c_str());
		curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long) CURL_HTTP_VERSION_2);
		curl_easy_setopt(curl, CURLOPT_PIPEWAIT, (long) 1);
		curl_easy_setopt(curl, CURLOPT_PRIVATE, transferInfo);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, (long) 1);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, (long) 1);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long) 0);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, transferInfo->errorBuf);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, PROGRAM_NAME " " PASSENGER_VERSION);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long) CHECK_TIMEOUT);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, handleResponseData);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, transferInfo);

		CURLMcode ret = curl_multi_add_handle(context->curlMulti, curl);
		if (ret != CURLM_OK) {
			P_ERROR("[RemoteSink sender] Error initiating liveliness check for gateway "
				<< server->getPingUrl() << ": "
				<< curl_multi_strerror(ret) << " (code=" << ret << ")");
			lastErrorTime = ev_now(getLoop());
			lastErrorMessage = string("Error initiating liveliness check for gateway ")
				+ server->getPingUrl() + ": "
				+ curl_multi_strerror(ret) + " (code=" + toString(ret) + ")";
			nChecksFinished++;
			delete transferInfo;
			reschedule();
			return;
		}

		server->reportLivelinessCheckBegin(now);
		STAILQ_INSERT_TAIL(&transferInfos, transferInfo, next);
		nTransferInfos++;
	}

	void finishTransfer(TransferInfo *transferInfo, CURLcode code, long httpCode,
		const string &body, const char *errorBuf)
	{
		TRACE_POINT();
		assert(nTransferInfos > 0);

		processFinishedTransfer(transferInfo->server, code, httpCode,
			body, errorBuf);

		STAILQ_REMOVE(&transferInfos, transferInfo, TransferInfo, next);
		nTransferInfos--;
		curl_multi_remove_handle(context->curlMulti, transferInfo->curl);
		delete transferInfo;
	}

	void processFinishedTransfer(const ServerPtr &server, CURLcode code, long httpCode,
		const string &body, const char *errorBuf)
	{
		TRACE_POINT();
		Json::Reader reader;
		Json::Value doc;

		assert(nTransferInfos > 0);
		nChecksFinished++;

		if (OXT_UNLIKELY(code != CURLE_OK)) {
			handleLivelinessCheckPerformError(server, code, errorBuf);
			return;
		}
		if (OXT_UNLIKELY(!reader.parse(body, doc, false))) {
			handleResponseParseError(server, httpCode, body,
				reader.getFormattedErrorMessages());
			return;
		}
		if (OXT_UNLIKELY(!validateResponse(doc))) {
			handleResponseInvalid(server, httpCode, body);
			return;
		}
		if (OXT_UNLIKELY(httpCode / 100 != 2)) {
			handleResponseInvalidHttpCode(server, httpCode, body);
			return;
		}

		UPDATE_TRACE_POINT();
		if (doc["status"] == "ok") {
			handleLivelinessCheckPassed(server);
		} else {
			handleLivelinessCheckFailed(server, body);
		}
	}

	bool validateResponse(const Json::Value &doc) const {
		if (OXT_UNLIKELY(!doc.isObject())) {
			return false;
		}
		if (OXT_UNLIKELY(!doc.isMember("status"))) {
			return false;
		}
		if (OXT_UNLIKELY(!doc["status"].isString())) {
			return false;
		}
		return true;
	}

	void handleLivelinessCheckPerformError(const ServerPtr &server, CURLcode code,
		const string &errorMessage)
	{
		string message = "Could not check liveliness of server "
			+ server->getPingUrl() + ". It appears to be down."
			" Error message: " + errorMessage;
		server->reportLivelinessCheckError(ev_now(getLoop()), message);
		setLastError(message);
		reschedule();
	}

	void handleResponseParseError(const ServerPtr &server, long httpCode,
		const string &body, const string &parseErrorMessage)
	{
		string message = "Could not check liveliness of server "
			+ server->getPingUrl() + ". It returned an invalid"
			" response (unparseable). Parse error: " + parseErrorMessage
			+ "; HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"";
		server->reportLivelinessCheckError(ev_now(getLoop()), message);
		setLastError(message);
		reschedule();
	}

	void handleResponseInvalid(const ServerPtr &server, long httpCode,
		const string &body)
	{
		string message = "Could not check liveliness of server "
			+ server->getPingUrl() + ". It returned an invalid"
			" response (parseable, but does not comply to expected structure)."
			+ " HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"";
		server->reportLivelinessCheckError(ev_now(getLoop()), message);
		setLastError(message);
		reschedule();
	}

	void handleResponseInvalidHttpCode(const ServerPtr &server, long httpCode,
		const string &body)
	{
		string message = "Could not check liveliness of server "
			+ server->getPingUrl() + ". It responded with an invalid"
			" HTTP code. HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"";
		server->reportLivelinessCheckError(ev_now(getLoop()), message);
		setLastError(message);
		reschedule();
	}

	void handleLivelinessCheckPassed(const ServerPtr &server) {
		server->reportLivelinessCheckSuccess(ev_now(getLoop()));
		reschedule();
	}

	void handleLivelinessCheckFailed(const ServerPtr &server, const string &body) {
		string message = "Server " + server->getPingUrl() + " is down."
			" HTTP body: \"" + cEscapeString(body) + "\"";
		server->reportLivelinessCheckError(ev_now(getLoop()), message);
		setLastError(message);
		reschedule();
	}

	void setLastError(const string &errorMessage) {
		lastErrorTime = ev_now(getLoop());
		lastErrorMessage = errorMessage;
	}

	Json::Value inspectChecksInProgress(ev_tstamp evNow, unsigned long long now) const {
		Json::Value doc;
		Json::Value items(Json::objectValue);
		const TransferInfo *transferInfo;

		STAILQ_FOREACH(transferInfo, &transferInfos, next) {
			Json::Value item;

			item["server_number"] = transferInfo->server->getNumber();
			item["ping_url"] = transferInfo->server->getPingUrl();
			item["started_at"] = evTimeToJson(transferInfo->startedAt,
				evNow, now);

			items[toString(transferInfo->number)] = item;
		}

		doc["count"] = nTransferInfos;
		doc["items"] = items;

		return doc;
	}

protected:
	// Virtual so that it can be mocked in unit tests.
	virtual bool shouldFailCheckInitiation(const ServerPtr &server) const {
		return false;
	}

	// Only used in unit tests.
	void checkFinished(const ServerPtr &server, CURLcode code,
		long httpCode, const string &body, const char *errorBuf)
	{
		TransferInfo *transferInfo;

		STAILQ_FOREACH(transferInfo, &transferInfos, next) {
			if (transferInfo->server.get() == server.get()) {
				finishTransfer(transferInfo, code, httpCode, body, errorBuf);
				return;
			}
		}

		throw RuntimeException("TransferInfo not found");
	}

public:
	ServerLivelinessChecker(Context *_context)
		: context(_context),
		  nextTransferInfoNumber(1),
		  nTransferInfos(0),
		  nChecksInitiated(0),
		  nChecksFinished(0),
		  lastInitiateTime(0),
		  lastErrorTime(0),
		  nextCheckTime(0)
	{
		STAILQ_INIT(&transferInfos);
		memset(&timer, 0, sizeof(timer));
		ev_timer_init(&timer, onTimeout, 0, 0);
		timer.data = this;
	}

	virtual ~ServerLivelinessChecker() {
		TRACE_POINT();
		TransferInfo *transferInfo, *nextTransferInfo;

		STAILQ_FOREACH_SAFE(transferInfo, &transferInfos, next, nextTransferInfo) {
			curl_multi_remove_handle(context->curlMulti, transferInfo->curl);
			delete transferInfo;
		}

		if (ev_is_active(&timer)) {
			ev_timer_stop(getLoop(), &timer);
		}
	}

	void registerServers(const Segment::SmallServerList &servers) {
		TRACE_POINT();
		Segment::SmallServerList::const_iterator it, end;
		char address[sizeof(Server *)];

		for (it = servers.begin(); it != servers.end(); it++) {
			const ServerPtr &server = *it;
			void *tmp = server.get();
			memcpy(address, &tmp, sizeof(Server *));
			HashedStaticString key(address, sizeof(Server *));

			this->servers.insertByMoving(key, WeakServerPtr(server));
		}

		reschedule();
	}

	vector<ServerPtr> getServersAndCleanupStale() {
		ServerTable::ConstIterator it(servers);
		vector<ServerPtr> result;
		vector<HashedStaticString> toRemove;
		vector<HashedStaticString>::const_iterator k_it;

		while (*it != NULL) {
			ServerPtr server(it.getValue().lock());
			if (server != NULL) {
				result.push_back(server);
			} else {
				toRemove.push_back(it.getKey());
			}
			it.next();
		}

		if (!toRemove.empty()) {
			for (k_it = toRemove.begin(); k_it != toRemove.end(); k_it++) {
				servers.erase(*k_it);
			}
			servers.compact();
		}

		return result;
	}

	void checkEligibleServers() {
		TRACE_POINT();
		vector<ServerPtr> servers(getServersAndCleanupStale());
		vector<ServerPtr>::const_iterator it, end = servers.end();
		ev_tstamp now = ev_now(getLoop());

		for (it = servers.begin(); it != end; it++) {
			const ServerPtr &server = *it;

			if (!server->isUp()
			 && !server->isBeingCheckedForLiveliness()
			 && server->getNextLivelinessCheckTime(now) <= now)
			{
				check(server);
			}
		}

		rescheduleWithServers(servers);
	}

	void reschedule() {
		rescheduleWithServers(getServersAndCleanupStale());
	}

	Json::Value inspectStateAsJson() const {
		Json::Value doc;
		ev_tstamp evNow = ev_now(getLoop());
		unsigned long long now = SystemTime::getUsec();

		doc["checks_in_progress"] = inspectChecksInProgress(evNow, now);
		doc["last_initiate_time"] = evTimeToJson(lastInitiateTime, evNow, now);
		doc["checks_initiated"] = nChecksInitiated;
		doc["checks_finished"] = nChecksFinished;
		doc["servers"]["count"] = servers.size();
		if (!lastErrorMessage.empty()) {
			doc["last_error"] = evTimeToJson(lastErrorTime, evNow, now);
			doc["last_error"]["message"] = lastErrorMessage;
		} else {
			doc["last_error"] = Json::Value(Json::nullValue);
		}
		if (ev_is_active(&timer)) {
			doc["next_liveliness_check_time"] = evTimeToJson(nextCheckTime, evNow, now);
		} else {
			doc["next_liveliness_check_time"] = Json::Value(Json::nullValue);
		}

		return doc;
	}
};


} // namespace Passenger
} // namespace UstRouter
} // namespace RemoteSink

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_SERVER_LIVELINESS_CHECKER_H_ */
