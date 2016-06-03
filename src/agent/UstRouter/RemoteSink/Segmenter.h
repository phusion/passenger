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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_SEGMENTER_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_SEGMENTER_H_

#include <boost/intrusive_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/move/move.hpp>
#include <boost/config.hpp>
#include <string>
#include <algorithm>
#include <limits>
#include <cstddef>
#include <cassert>
#include <cstring>
#ifndef BOOST_NO_CXX11_HDR_RANDOM
	#include <random>
#endif

#include <curl/curl.h>
#include <ev.h>
#include <psg_sysqueue.h>

#include <Logging.h>
#include <Algorithms/MovingAverage.h>
#include <DataStructures/StringKeyTable.h>
#include <Integrations/LibevJsonUtils.h>
#include <Utils/JsonUtils.h>
#include <Utils/StrIntUtils.h>
#include <UstRouter/RemoteSink/Common.h>
#include <UstRouter/RemoteSink/Segment.h>

namespace Passenger {
namespace UstRouter {
namespace RemoteSink {

using namespace std;


class Segmenter {
public:
	enum {
		DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY = 5 * 60,
		DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS = 60
	};

protected:
	struct KeyInfo: public CurlLibevIntegration::TransferInfo {
		Segmenter * const self;
		SegmentPtr segment;
		const string key;
		ev_tstamp lastLookupSuccessTime;
		ev_tstamp lastLookupErrorTime;
		ev_tstamp suspendSendingUntil;
		unsigned int refreshTimeoutWhenAllHealthy;
		unsigned int refreshTimeoutWhenHaveErrors;
		string lastErrorMessage;
		bool lookingUp;

		mutable unsigned int refcount;

		CURL *curl;
		const string manifestUrl;
		ev_tstamp transferStartTime;
		string responseBody;
		char errorBuffer[CURL_ERROR_SIZE];

		KeyInfo(Segmenter *_self, const string &_key)
			: self(_self),
			  key(_key),
			  lastLookupSuccessTime(0),
			  lastLookupErrorTime(0),
			  suspendSendingUntil(0),
			  refreshTimeoutWhenAllHealthy(DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY),
			  refreshTimeoutWhenHaveErrors(DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS),
			  lookingUp(false),
			  refcount(1),
			  curl(NULL),
			  manifestUrl(_self->createManifestUrl(_key)),
			  transferStartTime(0)
		{
			errorBuffer[0] = '\0';
		}

		void ref() const {
			refcount++;
		}

		void unref() const {
			assert(refcount > 0);
			refcount--;
			if (refcount == 0) {
				delete this;
			}
		}

		void startTransfer(CURL *_curl, ev_tstamp now) {
			lookingUp = true;
			curl = _curl;
			transferStartTime = now;
			ref();
		}

		virtual void finish(CURL *curl, CURLcode code) {
			long httpCode = -1;
			P_ASSERT_EQ(this->curl, curl);

			if (code == CURLE_OK) {
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
			}
			curl_easy_cleanup(curl);
			this->curl = NULL;
			self->apiLookupFinished(key, transferStartTime, code, httpCode,
				responseBody, errorBuffer);
			responseBody.clear();
			unref();
		}
	};

	typedef boost::intrusive_ptr<KeyInfo> KeyInfoPtr;

	friend struct KeyInfo;
	friend inline void intrusive_ptr_add_ref(const KeyInfo *keyInfo);
	friend inline void intrusive_ptr_release(const KeyInfo *keyInfo);

private:
	static const unsigned int ESTIMATED_CACHE_LINE_SIZE = 64;
	static const unsigned int ESTIMATED_MALLOC_OVERHEAD = 16;

	typedef Segment::SmallServerList SmallServerList;

	Context * const context;
	SegmentProcessor * const batcher;
	AbstractServerLivelinessChecker * const checker;
	string manifestBaseUrl;
	struct ev_timer timer;
	ev_tstamp nextKeyInfoRefreshTime;
	ev_tstamp lastErrorTime;
	string lastErrorMessage;
	unsigned int nextSegmentNumber;
	unsigned int nextServerNumber;

	SegmentList segments;
	SmallServerList servers;
	StringKeyTable<KeyInfoPtr> keyInfos;
	TransactionList queued;

	size_t limit;
	size_t bytesQueued;
	size_t peakSize;
	size_t bytesForwarded;
	size_t bytesDropped;
	unsigned int nQueued;
	unsigned int nForwarded;
	unsigned int nDropped;
	double avgKeyInfoLookupTime;


	string createManifestUrl(const string &key) const {
		return manifestBaseUrl + key;
	}

	KeyInfoPtr findOrCreateKeyInfo(const HashedStaticString &key) {
		KeyInfoPtr *keyInfo;

		if (keyInfos.lookup(key, &keyInfo)) {
			return *keyInfo;
		} else {
			KeyInfoPtr newKeyInfo(new KeyInfo(this, key), false);
			keyInfos.insert(key, newKeyInfo);
			initiateApiLookup(newKeyInfo);
			return newKeyInfo;
		}
	}

	void forwardToBatcher(SegmentList &segments) {
		TRACE_POINT();
		Segment *segment;

		STAILQ_FOREACH(segment, &segments, nextScheduledForBatching) {
			segment->scheduledForBatching = false;
			bytesForwarded += segment->bytesIncomingTransactions;
			nForwarded += segment->nIncomingTransactions;
		}

		batcher->schedule(segments);
		assert(STAILQ_EMPTY(&segments));
	}

	void forwardToBatcher(Segment *segment) {
		TRACE_POINT();
		SegmentList segments;
		STAILQ_INIT(&segments);
		STAILQ_INSERT_TAIL(&segments, segment, nextScheduledForBatching);
		bytesForwarded += segment->bytesIncomingTransactions;
		nForwarded += segment->nIncomingTransactions;
		batcher->schedule(segments);
		assert(STAILQ_EMPTY(&segments));
	}

	ev_tstamp calculateNextKeyInfoRefreshTime(const KeyInfoPtr &keyInfo) const {
		if (keyInfo->lookingUp) {
			return 0;
		}

		ev_tstamp result;

		if (keyInfo->lastLookupErrorTime > keyInfo->lastLookupSuccessTime) {
			result = keyInfo->lastLookupErrorTime + keyInfo->refreshTimeoutWhenHaveErrors;
		} else {
			result = keyInfo->lastLookupSuccessTime + keyInfo->refreshTimeoutWhenAllHealthy;
		}

		return std::max(result, ev_now(getLoop()));
	}

	void rescheduleNextKeyInfoRefresh() {
		TRACE_POINT();
		ev_tstamp nextTimeout = std::numeric_limits<ev_tstamp>::max();
		StringKeyTable<KeyInfoPtr>::ConstIterator it(keyInfos);

		while (*it != NULL) {
			const KeyInfoPtr &keyInfo = it.getValue();
			ev_tstamp t = calculateNextKeyInfoRefreshTime(keyInfo);
			if (t != 0) {
				nextTimeout = std::min(nextTimeout, t);
			}
			it.next();
		}

		if (nextTimeout != std::numeric_limits<ev_tstamp>::max()) {
			// Align the time to a multiple of 5 seconds to save power on laptops.
			nextTimeout = roundUpD(nextTimeout, 5);
		}

		if (nextTimeout == this->nextKeyInfoRefreshTime) {
			// Scheduled time not changed. No action required.
			return;
		}

		this->nextKeyInfoRefreshTime = nextTimeout;
		if (ev_is_active(&timer)) {
			ev_timer_stop(getLoop(), &timer);
		}
		if (nextTimeout != std::numeric_limits<ev_tstamp>::max()) {
			P_DEBUG("[RemoteSink segmenter] Rescheduling next key info refresh time: "
				<< distanceOfTimeInWords(ev_now(getLoop()), nextTimeout) << " from now");
			ev_timer_set(&timer, nextTimeout - ev_now(getLoop()), 0);
			ev_timer_start(getLoop(), &timer);
		}
	}

	string createSegmentKey(const Json::Value &doc) {
		return stringifyJson(doc["targets"]);
	}

	Segment *findSegment(const StaticString &segmentKey) {
		Segment *segment;

		STAILQ_FOREACH(segment, &segments, nextInSegmenterList) {
			if (segmentKey == segment->segmentKey) {
				return segment;
			}
		}

		return NULL;
	}

	void updateKeyInfoFromManifest(const KeyInfoPtr &keyInfo, const Json::Value &doc) {
		TRACE_POINT();
		if (doc.isMember("recheck_balancer_in")) {
			keyInfo->refreshTimeoutWhenAllHealthy = getJsonUintField(
				doc["recheck_balancer_in"], "all_healthy",
				keyInfo->refreshTimeoutWhenAllHealthy);
			keyInfo->refreshTimeoutWhenHaveErrors = getJsonUintField(
				doc["recheck_balancer_in"], "has_errors",
				keyInfo->refreshTimeoutWhenHaveErrors);
		}
	}

	ServerPtr findOrCreateServer(const StaticString &baseUrl, unsigned int weight) {
		SmallServerList::const_iterator it, end = servers.end();

		for (it = servers.begin(); it != end; it++) {
			const ServerPtr &server = *it;
			if (server->getBaseUrl() == baseUrl && server->getWeight() == weight) {
				return server;
			}
		}

		ServerPtr server(boost::make_shared<Server>(nextServerNumber++,
			baseUrl, weight));
		servers.push_back(server);
		return BOOST_MOVE_RET(ServerPtr, server);
	}

	bool serverListEquals(const Segment::SmallServerList &list1,
		const Segment::SmallServerList &list2) const
	{
		if (list1.size() != list2.size()) {
			return false;
		}

		Segment::SmallServerList::const_iterator it1, end1 = list1.end();
		Segment::SmallServerList::const_iterator it2;
		for (it1 = list1.begin(), it2 = list2.begin(); it1 != end1; it1++, it2++) {
			const ServerPtr &server1 = *it1;
			const ServerPtr &server2 = *it2;
			if (!server1->equals(*server2)) {
				return false;
			}
		}

		return true;
	}

	void updateSegmentFromManifest(const SegmentPtr &segment, const Json::Value &doc) {
		TRACE_POINT();
		const Json::Value &targets = doc["targets"];
		Json::Value::const_iterator it, end = targets.end();
		Segment::SmallServerList newServerList;

		for (it = targets.begin(); it != end; it++) {
			const Json::Value &target = *it;
			const string baseUrl = target["base_url"].asString();
			unsigned int weight = getJsonUintField(target, "weight", 1);

			ServerPtr server(findOrCreateServer(baseUrl, weight));
			newServerList.push_back(server);
		}

		if (!serverListEquals(segment->servers, newServerList)) {
			checker->registerServers(newServerList);
			segment->servers = boost::move(newServerList);
			recreateBalancingList(segment);
		}

		if (doc.isMember("recheck_down_gateway_in")) {
			setLivelinessCheckPeriodForAllServers(segment,
				doc["recheck_down_gateway_in"].asUInt());
		}
	}

	void recreateBalancingList(const SegmentPtr &segment) {
		Segment::SmallServerList::const_iterator it, end = segment->servers.end();

		segment->balancingList.clear();
		segment->nextBalancingIndex = 0;

		for (it = segment->servers.begin(); it != end; it++) {
			const ServerPtr &server = *it;
			for (unsigned int i = 0; i < server->getWeight(); i++) {
				segment->balancingList.push_back(server);
			}
		}

		#ifdef BOOST_NO_CXX11_HDR_RANDOM
			std::random_shuffle(segment->balancingList.begin(),
				segment->balancingList.end());
		#else
			std::shuffle(segment->balancingList.begin(),
				segment->balancingList.end(),
				std::random_device());
		#endif
	}

	void setLivelinessCheckPeriodForAllServers(const SegmentPtr &segment,
		unsigned int value)
	{
		Segment::SmallServerList::iterator it, end = segment->servers.end();

		for (it = segment->servers.begin(); it != end; it++) {
			const ServerPtr &server = *it;
			server->setLivelinessCheckPeriod(value);
		}
	}

	void removeQueuedTransactionsWithKey(const StaticString &key) {
		Transaction *transaction, *nextTransaction;

		STAILQ_FOREACH_SAFE(transaction, &queued, next, nextTransaction) {
			if (transaction->getUnionStationKey() == key) {
				STAILQ_REMOVE(&queued, transaction, Transaction, next);
				bytesQueued -= transaction->getBody().size();
				nQueued--;
				delete transaction;
			}
		}
	}

	bool validateApiResponse(const Json::Value &doc) const {
		TRACE_POINT();
		if (OXT_UNLIKELY(!doc.isObject())) {
			return false;
		}
		if (OXT_UNLIKELY(!doc.isMember("status") || !doc["status"].isString())) {
			return false;
		}
		if (OXT_LIKELY(doc["status"].asString() == "ok")) {
			if (OXT_UNLIKELY(!doc.isMember("targets") || !doc["targets"].isArray())) {
				return false;
			}

			Json::Value::const_iterator it, end = doc["targets"].begin();
			for (it = doc["targets"].begin(); it != end; it++) {
				const Json::Value &target = *it;
				if (OXT_UNLIKELY(!target.isObject())) {
					return false;
				}
				if (OXT_UNLIKELY(!target.isMember("base_url") || !target["base_url"].isString())) {
					return false;
				}
				if (OXT_UNLIKELY(!target.isMember("weight") || !target["weight"].isInt())) {
					return false;
				}
				if (target["weight"].asUInt() == 0) {
					return false;
				}
			}
		} else if (doc["status"].asString() == "error") {
			if (!doc.isMember("message") || !doc["message"].isString()) {
				return false;
			}
			if (doc.isMember("error_id") && !doc["error_id"].isString()) {
				return false;
			}
			if (doc.isMember("recheck_balancer_in") && !doc["recheck_balancer_in"].isInt()) {
				return false;
			}
			if (doc.isMember("suspend_sending") && !doc["suspend_sending"].isInt()) {
				return false;
			}
			if (doc.isMember("recheck_down_gateway_in") && !doc["recheck_down_gateway_in"].isInt()) {
				return false;
			}
		} else {
			return false;
		}

		if (OXT_UNLIKELY(doc.isMember("recheck_down_gateway_in")
		 && !doc["recheck_down_gateway_in"].isInt()))
		{
			return false;
		}

		return true;
	}

	void handleApiResponse(const KeyInfoPtr &keyInfo, long httpCode,
		const string &body)
	{
		TRACE_POINT();
		Json::Reader reader;
		Json::Value doc;

		if (OXT_UNLIKELY(!reader.parse(body, doc, false))) {
			handleApiResponseParseError(keyInfo, httpCode, body,
				reader.getFormattedErrorMessages());
			return;
		}
		if (OXT_UNLIKELY(!validateApiResponse(doc))) {
			handleApiResponseInvalid(keyInfo, httpCode, body);
			return;
		}
		if (OXT_UNLIKELY(doc["status"].asString() != "ok")) {
			handleApiResponseErrorMessage(keyInfo, doc);
			return;
		}
		if (OXT_UNLIKELY(httpCode / 100 != 2)) {
			handleApiResponseInvalidHttpCode(keyInfo, httpCode, body);
			return;
		}

		UPDATE_TRACE_POINT();
		handleApiSuccessResponse(keyInfo, doc);
	}

	void handleApiResponseParseError(const KeyInfoPtr &keyInfo, unsigned int httpCode,
		const string &body, const string &parseErrorMessage)
	{
		removeQueuedTransactionsWithKey(keyInfo->key);
		setApiLookupError(keyInfo,
			"Unable to fetch a list of Union Station gateway servers. "
			"The Union Station load balancing server " + createManifestUrl(keyInfo->key)
			+ " returned an invalid response (unparseable). "
			+ "Parse error: " + parseErrorMessage
			+ "; key: " + keyInfo->key
			+ "; HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"");
	}

	void handleApiResponseInvalid(const KeyInfoPtr &keyInfo, unsigned int httpCode,
		const string &body)
	{
		removeQueuedTransactionsWithKey(keyInfo->key);
		setApiLookupError(keyInfo,
			"Unable to fetch a list of Union Station gateway servers. "
			"The Union Station load balancing server " + createManifestUrl(keyInfo->key)
			+ " returned a invalid response (parseable, but does not comply to expected structure)."
			+ " Key: " + keyInfo->key
			+ "; HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"");
	}

	void handleApiResponseErrorMessage(const KeyInfoPtr &keyInfo,
		const Json::Value &doc)
	{
		P_ASSERT_EQ(doc["status"].asString(), "error");

		string message = "Unable to fetch a list of Union Station gateway servers. "
			"The Union Station load balancing server " + createManifestUrl(keyInfo->key)
			+ " returned an error. Message from server: "
			+ doc["message"].asString()
			+ "; key: " + keyInfo->key;
		if (doc.isMember("error_id")) {
			message.append("; error ID: ");
			message.append(doc["error_id"].asString());
		}
		setApiLookupError(keyInfo, message);

		removeQueuedTransactionsWithKey(keyInfo->key);

		if (doc.isMember("recheck_balancer_in")) {
			keyInfo->refreshTimeoutWhenHaveErrors = doc["recheck_balancer_in"].asUInt();
		}
		if (doc.isMember("suspend_sending")) {
			keyInfo->suspendSendingUntil = ev_now(getLoop())
				+ doc["suspend_sending"].asUInt64();
		}
	}

	void handleApiResponseInvalidHttpCode(const KeyInfoPtr &keyInfo,
		unsigned int httpCode, const string &body)
	{
		removeQueuedTransactionsWithKey(keyInfo->key);
		setApiLookupError(keyInfo,
			"Unable to fetch a list of Union Station gateway servers. "
			"The Union Station load balancing server " + createManifestUrl(keyInfo->key)
			+ " returned a invalid HTTP response code."
			+ " Key: " + keyInfo->key
			+ "; HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"");
	}

	void handleApiSuccessResponse(const KeyInfoPtr &keyInfo, const Json::Value &doc) {
		string segmentKey(createSegmentKey(doc));
		Segment *segment;

		keyInfo->lastLookupSuccessTime = ev_now(getLoop());

		if (keyInfo->segment == NULL) {
			// Create new segment
			segment = new Segment(nextSegmentNumber++, segmentKey);
			updateKeyInfoFromManifest(keyInfo, doc);
			updateSegmentFromManifest(segment, doc);
			keyInfo->segment.reset(segment);
			STAILQ_INSERT_TAIL(&segments, segment, nextInSegmenterList);

			// Move all queued transactions with the current key
			// into this segment
			Transaction *transaction, *nextTransaction;
			STAILQ_FOREACH_SAFE(transaction, &queued, next, nextTransaction) {
				if (transaction->getUnionStationKey() == keyInfo->key) {
					STAILQ_REMOVE(&queued, transaction, Transaction, next);
					bytesQueued -= transaction->getBody().size();
					nQueued--;

					STAILQ_INSERT_TAIL(&segment->incomingTransactions,
						transaction, next);
					segment->bytesIncomingTransactions +=
						transaction->getBody().size();
					segment->nIncomingTransactions++;
				}
			}

			forwardToBatcher(segment);

		} else if (segmentKey != keyInfo->segment->segmentKey) {
			// Move key to another segment
			segment = findSegment(segmentKey);
			if (segment == NULL) {
				segment = new Segment(nextSegmentNumber++, segmentKey);
				STAILQ_INSERT_TAIL(&segments, segment, nextInSegmenterList);
			}
			updateKeyInfoFromManifest(keyInfo, doc);
			updateSegmentFromManifest(segment, doc);
			keyInfo->segment.reset(segment);

		} else {
			updateKeyInfoFromManifest(keyInfo, doc);
			updateSegmentFromManifest(keyInfo->segment, doc);
		}
	}

	void handleApiLookupPerformError(const KeyInfoPtr &keyInfo,
		CURLcode code, const char *errorBuffer)
	{
		removeQueuedTransactionsWithKey(keyInfo->key);
		setApiLookupError(keyInfo,
			"Unable to fetch a list of Union Station gateway servers. "
			"The Union Station load balancing server " + createManifestUrl(keyInfo->key)
			+ " appears to be down. Error message: "
			+ errorBuffer);
	}

	void setApiLookupError(const KeyInfoPtr &keyInfo, const string &message) {
		P_ERROR("[RemoteSink segmenter] " << message);
		lastErrorMessage = keyInfo->lastErrorMessage = message;
		lastErrorTime = keyInfo->lastLookupErrorTime = ev_now(getLoop());
	}

	static size_t curlDataReceived(char *ptr, size_t size, size_t nmemb, void *userdata) {
		KeyInfo *keyInfo = static_cast<KeyInfo *>(userdata);
		keyInfo->responseBody.append(ptr, size * nmemb);
		return size * nmemb;
	}

	string getRecommendedMemoryLimit() const {
		return toString(peakSize * 2 / 1024) + " KB";
	}

	Json::Value inspectQueuedAsJson(ev_tstamp evNow, unsigned long long now) const {
		Json::Value doc(byteSizeAndCountToJson(bytesQueued, nQueued));
		Json::Value items(Json::arrayValue);
		Transaction *transaction;

		STAILQ_FOREACH(transaction, &queued, next) {
			items.append(transaction->inspectStateAsJson(evNow, now));
		}

		doc["items"] = items;
		return doc;
	}

	Json::Value inspectSegmentsAsJson(ev_tstamp evNow, unsigned long long now) const {
		Json::Value doc(Json::objectValue);
		Segment *segment;

		STAILQ_FOREACH(segment, &segments, nextInSegmenterList) {
			Json::Value subdoc;

			subdoc["segment_key"] = segment->segmentKey;
			subdoc["servers"] = inspectSegmentServersAsJson(segment);

			doc[toString(segment->number)] = subdoc;
		}

		return doc;
	}

	Json::Value inspectSegmentServersAsJson(Segment *segment) const {
		Json::Value doc(Json::arrayValue);
		Segment::SmallServerList::const_iterator it;

		for (it = segment->servers.begin(); it != segment->servers.end(); it++) {
			const ServerPtr &server = *it;
			doc.append(server->getNumber());
		}

		return doc;
	}

	Json::Value inspectServersAsJson(ev_tstamp evNow, unsigned long long now) const {
		Json::Value doc(Json::objectValue);
		SmallServerList::const_iterator it;

		for (it = servers.begin(); it != servers.end(); it++) {
			const ServerPtr &server = *it;
			doc[toString(server->getNumber())] = server->inspectStateAsJson(
				evNow, now);
		}

		return doc;
	}

	Json::Value inspectKeysAsJson(ev_tstamp evNow, unsigned long long now) const {
		Json::Value doc(Json::objectValue);
		StringKeyTable<KeyInfoPtr>::ConstIterator it(keyInfos);

		while (*it != NULL) {
			Json::Value subdoc;
			const KeyInfoPtr &keyInfo = it.getValue();

			if (keyInfo->segment == NULL) {
				subdoc["segment_number"] = Json::Value(Json::nullValue);
			} else {
				subdoc["segment_number"] = keyInfo->segment->number;
			}
			subdoc["looking_up"] = keyInfo->lookingUp;
			subdoc["last_lookup_success_time"] =
				evTimeToJson(keyInfo->lastLookupSuccessTime, evNow, now);
			subdoc["last_lookup_error_time"] =
				evTimeToJson(keyInfo->lastLookupErrorTime, evNow, now);
			subdoc["refresh_timeout_when_all_healthy"] =
				durationToJson(keyInfo->refreshTimeoutWhenAllHealthy * 1000000);
			subdoc["refresh_timeout_when_have_errors"] =
				durationToJson(keyInfo->refreshTimeoutWhenHaveErrors * 1000000);
			subdoc["next_refresh_time"] = evTimeToJson(
				calculateNextKeyInfoRefreshTime(keyInfo),
				evNow, now);
			if (keyInfo->suspendSendingUntil > evNow) {
				subdoc["suspend_sending_until"] =
					evTimeToJson(keyInfo->suspendSendingUntil, evNow, now);
			} else {
				subdoc["suspend_sending_until"] = Json::Value(Json::nullValue);
			}
			if (keyInfo->lastErrorMessage.empty()) {
				subdoc["last_error"]["message"] = Json::Value(Json::nullValue);
			} else {
				subdoc["last_error"]["message"] = lastErrorMessage;
			}

			doc[it.getKey()] = subdoc;
			it.next();
		}

		return doc;
	}

protected:
	struct ev_loop *getLoop() const {
		return context->loop;
	}

	void triggerTimeout() {
		onTimeout(getLoop(), &timer, 0);
	}

	// Only used in unit tests.
	Segment *getSegment(unsigned int number) {
		Segment *segment;

		STAILQ_FOREACH(segment, &segments, nextInSegmenterList) {
			if (segment->number == number) {
				return segment;
			}
		}

		return NULL;
	}

	// Virtual so that it can be stubbed out by unit tests.
	virtual bool initiateApiLookup(const KeyInfoPtr &keyInfo) {
		TRACE_POINT();
		CURL *curl;

		P_DEBUG("[RemoteSink segmenter] Performing API lookup for key: " << keyInfo->key);

		curl = curl_easy_init();
		if (curl == NULL) {
			P_ERROR("[RemoteSink segmenter] Error creating CURL handle. Maybe we're out of memory");
			keyInfo->lastLookupErrorTime = ev_now(getLoop());
			keyInfo->lastErrorMessage = "Error creating CURL handle. Maybe we're out of memory";
			return false;
		}

		curl_easy_setopt(curl, CURLOPT_URL, keyInfo->manifestUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long) CURL_HTTP_VERSION_2);
		curl_easy_setopt(curl, CURLOPT_PIPEWAIT, (long) 1);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, (long) 1);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long) 120);
		curl_easy_setopt(curl, CURLOPT_PRIVATE, keyInfo.get());
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, keyInfo->errorBuffer);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, PROGRAM_NAME " " PASSENGER_VERSION);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long) 1);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, (long) 1);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlDataReceived);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, keyInfo.get());

		CURLMcode ret = curl_multi_add_handle(context->curlMulti, curl);
		if (ret != CURLM_OK) {
			P_ERROR("[RemoteSink segmenter] Error scheduling API lookup request: " <<
				curl_multi_strerror(ret) << " (code=" << ret << ")");
			curl_easy_cleanup(curl);
			keyInfo->lastLookupErrorTime = ev_now(getLoop());
			keyInfo->lastErrorMessage = string("Error scheduling API lookup request: ")
				+ curl_multi_strerror(ret) + " (code=" + toString(ret) + ")";
			return false;
		}

		keyInfo->startTransfer(curl, ev_now(getLoop()));
		return true;
	}

	// Protected so that it can be accessed from unit tests.
	static void onTimeout(EV_P_ ev_timer *timer, int revents) {
		TRACE_POINT();
		Segmenter *self = static_cast<Segmenter *>(timer->data);

		P_DEBUG("[RemoteSink segmenter] Time to refresh key infos");

		StringKeyTable<KeyInfoPtr>::Iterator it(self->keyInfos);
		ev_tstamp now = ev_now(self->getLoop());
		while (*it != NULL) {
			const KeyInfoPtr &keyInfo = it.getValue();
			if (!keyInfo->lookingUp
			 && self->calculateNextKeyInfoRefreshTime(keyInfo) <= now)
			{
				self->initiateApiLookup(keyInfo);
			}
			it.next();
		}

		self->nextKeyInfoRefreshTime = std::numeric_limits<ev_tstamp>::max();
		self->rescheduleNextKeyInfoRefresh();
	}

public:
	// TODO: allow using proxy
	Segmenter(Context *_context, SegmentProcessor *_batcher,
		AbstractServerLivelinessChecker *_checker, const VariantMap &options)
		: context(_context),
		  batcher(_batcher),
		  checker(_checker),
		  manifestBaseUrl(options.get("union_station_load_balancer_manifest_base_url",
		      false, "https://gateway-v2.unionstationapp.com/v2/balance/")),
		  nextKeyInfoRefreshTime(std::numeric_limits<ev_tstamp>::max()),
		  lastErrorTime(0),
		  nextSegmentNumber(1),
		  nextServerNumber(1),
		  keyInfos(2, ESTIMATED_CACHE_LINE_SIZE - ESTIMATED_MALLOC_OVERHEAD),
		  limit(options.getULL("union_station_segmenter_memory_limit")),
		  bytesQueued(0),
		  peakSize(0),
		  bytesForwarded(0),
		  bytesDropped(0),
		  nQueued(0),
		  nForwarded(0),
		  nDropped(0),
		  avgKeyInfoLookupTime(-1)
	{
		STAILQ_INIT(&segments);
		STAILQ_INIT(&queued);

		memset(&timer, 0, sizeof(timer));
		ev_timer_init(&timer, onTimeout, 0, 0);
		timer.data = this;
	}

	virtual ~Segmenter() {
		TRACE_POINT();
		Segment *segment, *nextSegment;
		Transaction *transaction, *nextTransaction;
		StringKeyTable<KeyInfoPtr>::Iterator it(keyInfos);

		while (*it != NULL) {
			const KeyInfoPtr &keyInfo = it.getValue();
			if (keyInfo->curl != NULL) {
				curl_multi_remove_handle(context->curlMulti, keyInfo->curl);
				curl_easy_cleanup(keyInfo->curl);
				keyInfo->curl = NULL;
			}
			it.next();
		}

		STAILQ_FOREACH_SAFE(segment, &segments, nextInSegmenterList, nextSegment) {
			STAILQ_NEXT(segment, nextInSegmenterList) = NULL;
			segment->unref();
		}

		STAILQ_FOREACH_SAFE(transaction, &queued, next, nextTransaction) {
			delete transaction;
		}

		if (ev_is_active(&timer)) {
			ev_timer_stop(getLoop(), &timer);
		}
	}

	void schedule(TransactionList &transactions, size_t totalBodySize, unsigned int count,
		size_t &bytesScheduled, unsigned int &nScheduled)
	{
		TRACE_POINT();
		SegmentList segmentsToForward;
		bool shouldRescheduleNextKeyInfoRefresh = false;
		size_t bytesSeen = 0;
		unsigned int nSeen = 0;
		unsigned int i = 0;

		STAILQ_INIT(&segmentsToForward);

		bytesScheduled = 0;
		nScheduled = 0;
		peakSize = std::max<size_t>(peakSize, bytesQueued + totalBodySize);

		while (i < count && bytesQueued < limit) {
			Transaction *transaction = STAILQ_FIRST(&transactions);
			size_t bodySize = transaction->getBody().size();

			STAILQ_REMOVE_HEAD(&transactions, next);
			bytesSeen += bodySize;
			nSeen++;
			i++;

			KeyInfoPtr keyInfo(findOrCreateKeyInfo(transaction->getUnionStationKey()));
			if (keyInfo->suspendSendingUntil > ev_now(getLoop())) {
				bytesDropped += bodySize;
				nDropped++;
				delete transaction;
				continue;
			}

			Segment *segment = keyInfo->segment.get();
			if (segment != NULL) {
				segment->bytesIncomingTransactions += bodySize;
				segment->nIncomingTransactions++;
				STAILQ_INSERT_TAIL(&segment->incomingTransactions, transaction, next);

				bytesScheduled += bodySize;
				nScheduled++;

				if (!segment->scheduledForBatching) {
					segment->scheduledForBatching = true;
					STAILQ_INSERT_TAIL(&segmentsToForward, segment, nextScheduledForBatching);
				}
			} else {
				bytesQueued += bodySize;
				nQueued++;
				bytesScheduled += bodySize;
				nScheduled++;
				STAILQ_INSERT_TAIL(&queued, transaction, next);

				// If API lookup failed to initiate, then retry at a later time.
				shouldRescheduleNextKeyInfoRefresh =
					shouldRescheduleNextKeyInfoRefresh || !keyInfo->lookingUp;
			}
		}

		bytesDropped += totalBodySize - bytesSeen;
		nDropped += count - nSeen;

		UPDATE_TRACE_POINT();
		if (i != count) {
			assert(bytesQueued > limit);
			assert(totalBodySize > bytesScheduled);
			P_WARN("Unable to lookup Union Station key information quickly enough. "
				"Please increase the Union Station segmenter memory limit "
				"(recommended limit: " + getRecommendedMemoryLimit() + ")");
		}

		if (shouldRescheduleNextKeyInfoRefresh) {
			rescheduleNextKeyInfoRefresh();
		}

		forwardToBatcher(segmentsToForward);
	}

	void apiLookupFinished(const HashedStaticString &key, ev_tstamp startTime, CURLcode code,
		long httpCode, const string &body, const char *errorBuffer)
	{
		TRACE_POINT();
		KeyInfoPtr keyInfo(keyInfos.lookupCopy(key));
		assert(keyInfo != NULL);

		assert(keyInfo->lookingUp);
		keyInfo->lookingUp = false;
		avgKeyInfoLookupTime = expMovingAverage(avgKeyInfoLookupTime,
			ev_now(getLoop()) - startTime, 0.5);

		if (code == CURLE_OK) {
			handleApiResponse(keyInfo, httpCode, body);
		} else {
			handleApiLookupPerformError(keyInfo, code, errorBuffer);
		}

		rescheduleNextKeyInfoRefresh();
	}

	void refreshKey(const HashedStaticString &key) {
		TRACE_POINT();
		KeyInfoPtr keyInfo(findOrCreateKeyInfo(key));
		if (!keyInfo->lookingUp) {
			initiateApiLookup(keyInfo);
		}
	}

	Json::Value inspectStateAsJson() const {
		Json::Value doc;
		ev_tstamp evNow = ev_now(getLoop());
		unsigned long long now = SystemTime::getUsec();

		doc["total_in_memory"]["size"] = byteSizeToJson(bytesQueued);
		doc["total_in_memory"]["count"] = nQueued;
		doc["total_in_memory"]["peak_size"] = byteSizeToJson(peakSize);
		doc["total_in_memory"]["limit"] = byteSizeToJson(limit);

		doc["forwarded"] = byteSizeAndCountToJson(bytesForwarded, nForwarded);
		doc["dropped"] = byteSizeAndCountToJson(bytesDropped, nDropped);
		if (nextKeyInfoRefreshTime == std::numeric_limits<ev_tstamp>::max()) {
			doc["next_key_refresh_time"] = Json::Value(Json::nullValue);
		} else {
			doc["next_key_refresh_time"] = evTimeToJson(nextKeyInfoRefreshTime, evNow, now);
		}
		if (avgKeyInfoLookupTime == -1) {
			doc["average_key_info_lookup_time"] = Json::Value(Json::nullValue);
		} else {
			doc["average_key_info_lookup_time"] = durationToJson(avgKeyInfoLookupTime * 10000000);
		}
		doc["last_error"] = errorAndOcurrenceEvTimeToJson(lastErrorMessage,
			lastErrorTime, evNow, now);

		doc["queued"] = inspectQueuedAsJson(evNow, now);
		doc["segments"] = inspectSegmentsAsJson(evNow, now);
		doc["servers"] = inspectServersAsJson(evNow, now);
		doc["keys"] = inspectKeysAsJson(evNow, now);

		return doc;
	}
};


inline void
intrusive_ptr_add_ref(const Segmenter::KeyInfo *keyInfo) {
	keyInfo->ref();
}

inline void
intrusive_ptr_release(const Segmenter::KeyInfo *keyInfo) {
	keyInfo->unref();
}


} // namespace RemoteSink
} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_SEGMENTER_H_ */
