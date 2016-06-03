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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_SENDER_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_SENDER_H_

#include <boost/move/move.hpp>
#include <oxt/macros.hpp>
#include <oxt/backtrace.hpp>
#include <algorithm>
#include <string>
#include <stdexcept>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cerrno>

#include <sys/types.h>

#include <curl/curl.h>
#include <psg_sysqueue.h>

#include <StaticString.h>
#include <Logging.h>
#include <Exceptions.h>
#include <DataStructures/StringKeyTable.h>
#include <Integrations/CurlLibevIntegration.h>
#include <Utils/StrIntUtils.h>
#include <UstRouter/RemoteSink/Batch.h>
#include <UstRouter/RemoteSink/Common.h>
#include <UstRouter/RemoteSink/Segment.h>

namespace Passenger {
namespace UstRouter {
namespace RemoteSink {

using namespace std;
using namespace boost;
using namespace oxt;


class Sender: public SegmentProcessor {
private:
	static const unsigned int MAX_FREE_TRANSFERS = 32;

	typedef StringKeyTable<SegmentPtr, SKT_EnableMoveSupport> SegmentsTable;

	enum TransferState {
		CONNECTING,
		UPLOADING,
		RECEIVING_RESPONSE
	};

	class Transfer: public CurlLibevIntegration::TransferInfo {
	public:
		Sender * const sender;
		CURL * const curl;
		unsigned int number;
		SegmentPtr segment;
		Batch batch;
		ServerPtr server;
		TransferState state;
		ev_tstamp lastActivity, startTime, uploadBeginTime, uploadEndTime;
		off_t alreadyUploaded;
		STAILQ_ENTRY(Transfer) next;
		string responseData;
		char errorBuf[CURL_ERROR_SIZE];

		Transfer(Sender *_sender)
			: sender(_sender),
			  curl(curl_easy_init())
		{
			STAILQ_NEXT(this, next) = NULL;
			errorBuf[0] = '\0';
		}

		~Transfer() {
			curl_easy_cleanup(curl);
		}

		virtual void finish(CURL *curl, CURLcode code) {
			long httpCode = -1;
			P_ASSERT_EQ(curl, this->curl);

			if (code == CURLE_OK) {
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
			}

			sender->finishTransfer(this, code, httpCode,
				responseData, errorBuf);
		}
	};

	STAILQ_HEAD(TransferList, Transfer);

	Context * const context;
	SegmentsTable segments;
	TransferList transfers;
	TransferList freeTransfers;
	size_t bytesTransferring;
	size_t bytesAccepted;
	size_t bytesRejected;
	size_t bytesDropped;
	size_t peakSize;
	size_t limit;
	unsigned int nTransfers;
	unsigned int nFreeTransfers;
	unsigned int nPeakTransferring;
	unsigned int nAccepted;
	unsigned int nRejected;
	unsigned int nDropped;
	unsigned int nextTransferNumber;
	ev_tstamp lastInitiateTime, lastAcceptTime, lastRejectTime, lastDropTime;
	unsigned int connectTimeout, uploadTimeout, responseTimeout;
	string lastRejectionErrorMessage, lastDropErrorMessage;


	struct ev_loop *getLoop() const {
		return context->loop;
	}

	bool initiateTransfer(Segment *segment, BOOST_RV_REF(Batch) batch) {
		ServerPtr server(checkoutNextServer(segment));
		if (server == NULL) {
			P_ERROR("[RemoteSink sender] Could not send data to a Union "
				"Station gateway server: all gateways are down. Keys: "
				<< toString(batch.getKeys()));
			lastDropTime = ev_now(getLoop());
			lastDropErrorMessage = "Could not send data to a Union "
				"Station gateway server: all gateways are down. Keys: "
				+ toString(batch.getKeys());
			return false;
		}

		Transfer *transfer = checkoutTransferObject(segment, boost::move(batch),
			boost::move(server));
		if (OXT_UNLIKELY(transfer == NULL)) {
			// checkoutTransferObject() already logged the error.
			P_ASSERT_EQ(lastDropTime, ev_now(getLoop()));
			assert(!lastDropErrorMessage.empty());
			return false;
		}

		CURL *curl = transfer->curl;
		if (transfer->batch.isCompressed()) {
			curl_easy_setopt(curl, CURLOPT_URL,
				transfer->server->getSinkUrlWithCompression().c_str());
		} else {
			curl_easy_setopt(curl, CURLOPT_URL,
				transfer->server->getSinkUrlWithoutCompression().c_str());
		}
		curl_easy_setopt(curl, CURLOPT_UPLOAD, (long) 1);
		curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long) CURL_HTTP_VERSION_2);
		curl_easy_setopt(curl, CURLOPT_PIPEWAIT, (long) 1);
		curl_easy_setopt(curl, CURLOPT_PRIVATE, transfer);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, (long) 1);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, (long) 0);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long) 0);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, transfer->errorBuf);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, PROGRAM_NAME " " PASSENGER_VERSION);
		curl_easy_setopt(curl, CURLOPT_POST, (long) 1);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long) connectTimeout);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, transfer);
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, readTransferData);
		curl_easy_setopt(curl, CURLOPT_READDATA, transfer);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
			(curl_off_t) transfer->batch.getDataSize());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, handleResponseData);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, transfer);

		CURLMcode ret = curl_multi_add_handle(context->curlMulti, curl);
		if (ret != CURLM_OK) {
			P_ERROR("[RemoteSink sender] Error initiating transfer to gateway"
				<< transfer->server->getSinkUrlWithoutCompression() << ": " <<
				curl_multi_strerror(ret) << " (code=" << ret << ")");
			lastDropTime = ev_now(getLoop());
			lastDropErrorMessage = string("Error initiating transfer to gateway")
				+ transfer->server->getSinkUrlWithoutCompression() + ": "
				+ curl_multi_strerror(ret) + " (code=" + toString(ret) + ")";
			freeTransferObject(transfer);
			return false;
		}

		STAILQ_INSERT_TAIL(&transfers, transfer, next);
		bytesTransferring += transfer->batch.getDataSize();
		nTransfers++;
		nPeakTransferring = std::max(nPeakTransferring, nTransfers);
		lastInitiateTime = ev_now(getLoop());
		segment->lastInitiateTime = ev_now(getLoop());
		return true;
	}

	Transfer *checkoutTransferObject(Segment *segment, BOOST_RV_REF(Batch) batch,
		BOOST_RV_REF(ServerPtr) server)
	{
		Transfer *transfer;

		if (STAILQ_EMPTY(&freeTransfers)) {
			try {
				transfer = new Transfer(this);
			} catch (const std::bad_alloc &) {
				P_ERROR("[RemoteSink sender] Error allocating memory for a transfer");
				lastDropTime = ev_now(getLoop());
				lastDropErrorMessage = "Error allocating memory for a transfer";
				return NULL;
			}
			if (transfer->curl == NULL) {
				P_ERROR("[RemoteSink sender] Error creating CURL handle."
					"Maybe we're out of memory");
				lastDropTime = ev_now(getLoop());
				lastDropErrorMessage = "Error creating CURL handle."
					" Maybe we're out of memory";
				delete transfer;
				return NULL;
			}
		} else {
			transfer = STAILQ_FIRST(&freeTransfers);
			STAILQ_REMOVE_HEAD(&freeTransfers, next);
			nFreeTransfers--;
		}

		transfer->number = nextTransferNumber++;
		transfer->segment.reset(segment);
		transfer->batch = boost::move(batch);
		transfer->server = boost::move(server);
		transfer->state = CONNECTING;
		transfer->lastActivity = ev_now(getLoop());
		transfer->startTime = ev_now(getLoop());
		transfer->uploadBeginTime = 0;
		transfer->uploadEndTime = 0;
		transfer->alreadyUploaded = 0;
		transfer->errorBuf[0] = '\0';
		STAILQ_NEXT(transfer, next) = NULL;

		transfer->server->reportRequestBegin(ev_now(getLoop()));

		return transfer;
	}

	void freeTransferObject(Transfer *transfer) {
		curl_multi_remove_handle(context->curlMulti, transfer->curl);
		if (nFreeTransfers >= MAX_FREE_TRANSFERS) {
			delete transfer;
		} else {
			STAILQ_INSERT_HEAD(&freeTransfers, transfer, next);
			transfer->segment.reset();
			transfer->batch = Batch();
			transfer->server.reset();
			transfer->responseData.clear();
			curl_easy_reset(transfer->curl);
		}
	}

	ServerPtr checkoutNextServer(Segment *segment) {
		size_t i;
		size_t size = segment->balancingList.size();

		for (i = 0; i < size; i++) {
			unsigned int index = (segment->nextBalancingIndex + 1) % size;
			segment->nextBalancingIndex = index;
			if (segment->balancingList[i]->isUp()) {
				return segment->balancingList[i];
			}
		}

		// All servers down
		return ServerPtr();
	}

	static size_t readTransferData(char *buffer, size_t size, size_t nitems, void *instream) {
		TRACE_POINT();
		Transfer *transfer = static_cast<Transfer *>(instream);
		StaticString data(transfer->batch.getData().substr(transfer->alreadyUploaded,
			size * nitems));
		memcpy(buffer, data.data(), data.size());
		transfer->alreadyUploaded += data.size();
		return data.size();
	}

	static size_t handleResponseData(char *ptr, size_t size, size_t nmemb, void *userdata) {
		TRACE_POINT();
		Transfer *transfer = static_cast<Transfer *>(userdata);
		transfer->responseData.append(ptr, size * nmemb);
		return size * nmemb;
	}

	static int progressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
		curl_off_t ultotal, curl_off_t ulnow)
	{
		TRACE_POINT();
		Transfer *transfer = static_cast<Transfer *>(clientp);
		Sender *self = transfer->sender;
		ev_tstamp now = ev_now(self->getLoop());

		transfer->lastActivity = now;

		switch (transfer->state) {
		case CONNECTING:
			if (ultotal == ulnow) {
				// Upload done
				transfer->state = RECEIVING_RESPONSE;
				transfer->uploadBeginTime = now;
				transfer->uploadEndTime = now;
			} else if (ulnow > 0) {
				// Upload in progress
				transfer->state = UPLOADING;
				transfer->uploadBeginTime = now;
			}
			// libcurl automatically takes care of connection timeouts.
			return 0;
		case UPLOADING:
			if (ultotal == ulnow) {
				// Upload done
				assert(transfer->uploadBeginTime > 0);
				transfer->state = RECEIVING_RESPONSE;
				transfer->uploadEndTime = now;
				return 0;
			} else if (now >= transfer->startTime + self->uploadTimeout) {
				// Upload timeout
				return -1;
			} else {
				// Upload in progress
				return 0;
			}
		case RECEIVING_RESPONSE:
			if (now >= transfer->startTime + self->responseTimeout) {
				// Timeout receiving response
				return -1;
			} else {
				// Response in progress
				return 0;
			}
		default:
			return 0;
		}
	}

	void finishTransfer(Transfer *transfer, CURLcode code, long httpCode,
		const string &body, const char *errorBuf)
	{
		TRACE_POINT();
		assert(bytesTransferring >= transfer->batch.getDataSize());
		assert(nTransfers > 0);

		processFinishedTransfer(transfer, code, httpCode,
			transfer->responseData, transfer->errorBuf);

		STAILQ_REMOVE(&transfers, transfer, Transfer, next);
		bytesTransferring -= transfer->batch.getDataSize();
		nTransfers--;
		freeTransferObject(transfer);
	}

	void processFinishedTransfer(Transfer *transfer, CURLcode code, long httpCode,
		const string &body, const char *errorBuf)
	{
		TRACE_POINT();
		Json::Reader reader;
		Json::Value doc;

		if (OXT_UNLIKELY(code != CURLE_OK)) {
			handleTransferPerformError(transfer, code, errorBuf);
			return;
		}
		if (OXT_UNLIKELY(!reader.parse(body, doc, false))) {
			handleResponseParseError(transfer, httpCode, body,
				reader.getFormattedErrorMessages());
			return;
		}
		if (OXT_UNLIKELY(!validateResponse(doc))) {
			handleResponseInvalid(transfer, httpCode, body);
			return;
		}
		if (OXT_UNLIKELY(doc["status"].asString() != "ok")) {
			handleResponseErrorMessage(transfer, doc);
			return;
		}
		if (OXT_UNLIKELY(httpCode / 100 != 2)) {
			handleResponseInvalidHttpCode(transfer, httpCode, body);
			return;
		}

		handleSuccessResponse(transfer, doc);
	}

	bool validateResponse(const Json::Value &doc) const {
		if (OXT_UNLIKELY(!doc.isObject())) {
			return false;
		}
		if (OXT_UNLIKELY(!doc.isMember("status") || !doc["status"].isString())) {
			return false;
		}

		string status = doc["status"].asString();
		if (OXT_UNLIKELY(status != "ok" && status != "error")) {
			return false;
		}

		if (status == "error") {
			if (!doc.isMember("message") || !doc["message"].isString()) {
				return false;
			}
			if (doc.isMember("error_id") && !doc["error_id"].isString()) {
				return false;
			}
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

		return true;
	}

	void handleResponseParseError(Transfer *transfer, long httpCode,
		const string &body, const string &parseErrorMessage)
	{
		// This is probably a bug in the server, so we treat the server
		// as down until it is fixed.
		handleServerDown(transfer,
			"Could not send data to the Union Station gateway server "
			+ transfer->server->getSinkUrlWithoutCompression()
			+ ". It returned an invalid response (unparseable)."
			+ " Parse error: " + parseErrorMessage
			+ "; keys: " + toString(transfer->batch.getKeys())
			+ "; HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"",

			"The server returned an invalid response (unparseable)."
			" Parse error: " + parseErrorMessage
			+ "; keys: " + toString(transfer->batch.getKeys())
			+ "; HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"");
	}

	void handleResponseInvalid(Transfer *transfer, long httpCode, const string &body) {
		// This is probably a bug in the server, so we treat the server
		// as down until it is fixed.
		handleServerDown(transfer,
			"Could not send data to the Union Station gateway server "
			+ transfer->server->getSinkUrlWithoutCompression()
			+ ". It returned an invalid response (parseable,"
			" but does not comply to expected structure)."
			+ " Keys: " + toString(transfer->batch.getKeys())
			+ "; HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"",

			"The server returned an invalid response (parseable,"
			" but does not comply to expected structure)."
			" Keys: " + toString(transfer->batch.getKeys())
			+ "; HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"");
	}

	void handleResponseErrorMessage(Transfer *transfer, const Json::Value &doc) {
		P_ASSERT_EQ(doc["status"].asString(), "error");

		size_t dataSize = transfer->batch.getDataSize();
		ev_tstamp now = ev_now(getLoop());
		ev_tstamp uploadTime = transfer->uploadEndTime - transfer->uploadBeginTime;

		transfer->server->reportRequestRejected(dataSize, now, uploadTime,
			"Error message from server: "
			+ doc["message"].asString()
			+ "; keys: " + toString(transfer->batch.getKeys()));
		assert(transfer->server->isUp());

		lastRejectionErrorMessage = "Could not send data to the Union Station gateway server "
			+ transfer->server->getSinkUrlWithoutCompression()
			+ ". It returned the following error message: "
			+ doc["message"].asString()
			+ "; keys: " + toString(transfer->batch.getKeys());

		bytesRejected += dataSize;
		nRejected++;
		lastRejectTime = now;
		transfer->segment->bytesRejected += dataSize;
		transfer->segment->nRejected++;
		transfer->segment->lastRejectTime = now;

		handleResponseKeys(transfer, doc);
	}

	void handleResponseInvalidHttpCode(Transfer *transfer, long httpCode,
		const string &body)
	{
		// This is probably a bug in the server, so we treat the server
		// as down until it is fixed.
		handleServerDown(transfer,
			"Could not send data to the Union Station gateway server "
			+ transfer->server->getSinkUrlWithoutCompression()
			+ ". It responded with an invalid HTTP code."
			" Keys: " + toString(transfer->batch.getKeys())
			+ "; HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"",

			"Response with invalid HTTP code."
			" Keys: " + toString(transfer->batch.getKeys())
			+ "; HTTP code: " + toString(httpCode)
			+ "; body: \"" + cEscapeString(body) + "\"");
	}

	void handleSuccessResponse(Transfer *transfer, const Json::Value &doc) {
		size_t dataSize = transfer->batch.getDataSize();
		ev_tstamp now = ev_now(getLoop());
		ev_tstamp uploadTime = transfer->uploadEndTime - transfer->startTime;
		ev_tstamp responseTime = now - transfer->uploadEndTime;

		transfer->server->reportRequestAccepted(dataSize, uploadTime,
			responseTime, now);

		bytesAccepted += dataSize;
		nAccepted++;
		lastAcceptTime = now;
		transfer->segment->bytesAccepted += dataSize;
		transfer->segment->nAccepted++;
		transfer->segment->lastAcceptTime = now;

		handleResponseKeys(transfer, doc);
	}

	void handleResponseKeys(Transfer *transfer, const Json::Value &doc) {
		if (doc.isMember("recheck_balancer_in")) {
			// TODO
		}
		if (doc.isMember("suspend_sending")) {
			// TODO
		}
		if (doc.isMember("recheck_down_gateway_in")) {
			// TODO
		}
	}

	void handleTransferPerformError(Transfer *transfer, CURLcode code,
		const char *errorBuf)
	{
		handleServerDown(transfer,
			"Could not send data to the Union Station gateway server "
			+ transfer->server->getSinkUrlWithoutCompression()
			+ ". It might be down. Keys: " + toString(transfer->batch.getKeys())
			+ "; error message: " + errorBuf,

			"Server appears to be down."
			" Keys: " + toString(transfer->batch.getKeys())
			+ "; error message: " + errorBuf);
	}

	void handleServerDown(Transfer *transfer, const string &globalErrorMessage,
		const string &serverSpecificErrorMessage)
	{
		size_t dataSize = transfer->batch.getDataSize();
		ev_tstamp now = ev_now(getLoop());
		Segment *segment = transfer->segment.get();

		transfer->server->reportRequestDropped(dataSize, now,
			serverSpecificErrorMessage);
		assert(!transfer->server->isUp());

		if (segment->balancingList.empty()) {
			P_ERROR("[RemoteSink sender] " << globalErrorMessage);
			lastDropErrorMessage = globalErrorMessage;
			lastDropTime = now;
			bytesDropped += dataSize;
			nDropped++;
			segment->bytesDroppedBySender += dataSize;
			segment->nDroppedBySender++;
			segment->lastDroppedBySenderTime = now;
		} else {
			P_INFO("[RemoteSink sender] " << globalErrorMessage);
			P_INFO("[RemoteSink sender] Retrying by sending the data to a different"
			" gateway server...");
			if (!initiateTransfer(segment, boost::move(transfer->batch))) {
				assert(!lastDropErrorMessage.empty());
				P_ASSERT_EQ(lastDropTime, now);

				bytesDropped += dataSize;
				nDropped++;
				segment->bytesDroppedBySender += dataSize;
				segment->nDroppedBySender++;
				segment->lastDroppedBySenderTime = now;
			}
		}
	}

	size_t calculateSegmentListTotalIncomingBatchesSize(const SegmentList &segments) const {
		const Segment *segment;
		size_t result = 0;

		STAILQ_FOREACH(segment, &segments, nextScheduledForSending) {
			Segment::BatchList::const_iterator it, end = segment->incomingBatches.end();

			for (it = segment->incomingBatches.begin(); it != end; it++) {
				const Batch &batch = *it;
				result += batch.getDataSize();
			}
		}

		return result;
	}

	Json::Value inspectTransfersAsJson(ev_tstamp evNow, unsigned long long now) const {
		Json::Value doc;

		doc["count"] = nTransfers;
		doc["peak_count"] = nPeakTransferring;
		doc["freelist_count"] = nFreeTransfers;
		doc["items"] = inspectTransferItemsAsJson(evNow, now);

		return doc;
	}

	Json::Value inspectTransferItemsAsJson(ev_tstamp evNow, unsigned long long now) const {
		Json::Value doc(Json::objectValue);
		Transfer *transfer;

		STAILQ_FOREACH(transfer, &transfers, next) {
			Json::Value item;

			item["segment_number"] = transfer->segment->number;
			item["server_number"] = transfer->server->getNumber();
			item["server_sink_url"] = transfer->server->getSinkUrlWithoutCompression();
			item["last_activity"] = evTimeToJson(transfer->lastActivity,
				evNow, now);
			item["start_time"] = evTimeToJson(transfer->startTime, evNow, now);
			item["upload_begin_time"] = evTimeToJson(transfer->uploadBeginTime, evNow, now);
			item["upload_end_time"] = evTimeToJson(transfer->uploadEndTime, evNow, now);
			item["already_uploaded"] = byteSizeToJson(transfer->alreadyUploaded);
			item["size"] = byteSizeToJson(transfer->batch.getDataSize());

			switch (transfer->state) {
			case CONNECTING:
				item["state"] = "CONNECTING";
				break;
			case UPLOADING:
				item["state"] = "UPLOADING";
				break;
			case RECEIVING_RESPONSE:
				item["state"] = "RECEIVING_RESPONSE";
				break;
			default:
				item["state"] = "UNKNOWN";
				break;
			}

			doc[toString(transfer->number)] = item;
		}

		return doc;
	}

/*	void inspectTransferListAsJson(Json::Value &doc, TransferList &transfers) const {
		Transaction *transaction;

		STAILQ_FOREACH(transfer, &transfers, next) {
			Json::Value subdoc;

			subdoc["state"] = getStateString(transfer->state).toString();
			subdoc["server"] = transfer->server->inspectStateAsJson();
			subdoc["data_size"] = transfer->batch.getDataSize();
			if (transfer->lastActivity == 0) {
				subdoc["last_activity"] = Json::Value(Json::nullValue);
			} else {
				subdoc["last_activity"] = timeToJson(transfer->lastActivity);
			}
			if (transfer->startTime == 0) {
				subdoc["start_time"] = Json::Value(Json::nullValue);
			} else {
				subdoc["start_time"] = timeToJson(transfer->startTime);
			}
			if (transfer->uploadBeginTime == 0) {
				subdoc["upload_begin_time"] = Json::Value(Json::nullValue);
			} else {
				subdoc["upload_begin_time"] = timeToJson(transfer->uploadBeginTime);
			}
			if (transfer->uploadEndTime == 0) {
				subdoc["upload_end_time"] = Json::Value(Json::nullValue);
			} else {
				subdoc["upload_end_time"] = timeToJson(transfer->uploadEndTime);
			}
			subdoc["already_uploaded"] = byteSizeToJson((size_t) transfer->alreadyUploaded);

			doc[toString(transfer->number)] = subdoc;
		}
	}
*/
protected:
	// Only used in unit tests.
	void transferFinished(unsigned int transferNumber, CURLcode code, long httpCode,
		const string &body, const char *errorBuf)
	{
		Transfer *transfer;

		STAILQ_FOREACH(transfer, &transfers, next) {
			if (transfer->number == transferNumber) {
				finishTransfer(transfer, code, httpCode, body, errorBuf);
				return;
			}
		}

		throw RuntimeException("Transfer not found");
	}

public:
	// TODO: handle proxy
	Sender(Context *_context, const VariantMap &options)
		: context(_context),
		  bytesTransferring(0),
		  bytesAccepted(0),
		  bytesRejected(0),
		  bytesDropped(0),
		  peakSize(0),
		  limit(options.getULL("union_station_sender_memory_limit")),
		  nTransfers(0),
		  nFreeTransfers(0),
		  nPeakTransferring(0),
		  nAccepted(0),
		  nRejected(0),
		  nDropped(0),
		  nextTransferNumber(1),
		  lastInitiateTime(0),
		  lastAcceptTime(0),
		  lastRejectTime(0),
		  lastDropTime(0),
		  connectTimeout(options.getUint("union_station_connect_timeout", false, 0)),
		  uploadTimeout(options.getUint("union_station_upload_timeout")),
		  responseTimeout(options.getUint("union_station_response_timeout"))
	{
		STAILQ_INIT(&transfers);
		STAILQ_INIT(&freeTransfers);
	}

	~Sender() {
		Transfer *transfer, *nextTransfer;

		STAILQ_FOREACH_SAFE(transfer, &transfers, next, nextTransfer) {
			curl_multi_remove_handle(context->curlMulti, transfer->curl);
			delete transfer;
		}

		STAILQ_FOREACH_SAFE(transfer, &freeTransfers, next, nextTransfer) {
			delete transfer;
		}
	}

	void schedule(SegmentList &segments) {
		TRACE_POINT();
		Segment *segment;
		SegmentPtr *ourSegment;

		peakSize = std::max(peakSize, bytesTransferring
			+ calculateSegmentListTotalIncomingBatchesSize(segments));

		STAILQ_FOREACH(segment, &segments, nextScheduledForSending) {
			Segment::BatchList::iterator it, end = segment->incomingBatches.end();
			char address[sizeof(Segment *)];
			memcpy(address, &segment, sizeof(Segment *));
			HashedStaticString addressString(address, sizeof(address));

			// Add this segment to our segments hash table if we
			// don't already have it.
			if (this->segments.lookup(addressString, &ourSegment)) {
				P_ASSERT_EQ(segment, ourSegment->get());
			} else {
				this->segments.insertByMoving(addressString, SegmentPtr(segment));
			}

			for (it = segment->incomingBatches.begin(); it != end; it++) {
				Batch &batch = *it;

				if (bytesTransferring >= limit
				 || !initiateTransfer(segment, boost::move(batch)))
				{
					ev_tstamp now = ev_now(getLoop());

					assert(!lastDropErrorMessage.empty());
					P_ASSERT_EQ(lastDropTime, now);

					bytesDropped += batch.getDataSize();
					nDropped++;
					segment->bytesDroppedBySender += batch.getDataSize();
					segment->nDroppedBySender++;
					segment->lastDroppedBySenderTime = now;
				}
			}

			segment->incomingBatches.clear();
		}

		STAILQ_INIT(&segments);
	}

	Json::Value inspectStateAsJson() const {
		Json::Value doc;
		ev_tstamp evNow = ev_now(getLoop());
		unsigned long long now = SystemTime::getUsec();

		doc["total_memory"]["size"] = byteSizeToJson(bytesTransferring);
		doc["total_memory"]["count"] = nTransfers;
		doc["total_memory"]["peak_size"] = byteSizeToJson(peakSize);
		doc["total_memory"]["limit"] = byteSizeToJson(limit);
		doc["transfers"] = inspectTransfersAsJson(evNow, now);
		doc["accepted"] = byteSizeAndCountToJson(bytesAccepted, nAccepted);
		doc["rejected"] = byteSizeAndCountToJson(bytesRejected, nRejected);
		doc["dropped"] = byteSizeAndCountToJson(bytesDropped, nDropped);
		doc["last_initiated"] = evTimeToJson(lastInitiateTime, evNow, now);
		doc["last_accepted"] = evTimeToJson(lastAcceptTime, evNow, now);
		doc["last_rejected"] = errorAndOcurrenceEvTimeToJson(lastRejectionErrorMessage,
			lastRejectTime, evNow, now);
		doc["last_dropped"] = errorAndOcurrenceEvTimeToJson(lastDropErrorMessage,
			lastDropTime, evNow, now);

		return doc;
	}
};


} // namespace RemoteSink
} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_SENDER_H_ */
