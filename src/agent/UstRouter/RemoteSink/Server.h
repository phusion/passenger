/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_SERVER_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_SERVER_H_

#include <boost/shared_ptr.hpp>
#include <string>
#include <algorithm>
#include <cassert>
#include <cstddef>

#include <ev.h>

#include <Logging.h>
#include <Algorithms/MovingAverage.h>
#include <Integrations/LibevJsonUtils.h>
#include <Utils/JsonUtils.h>
#include <Utils/SystemTime.h>

namespace Passenger {
namespace UstRouter {
namespace RemoteSink {

using namespace std;


class Server {
public:
	enum {
		DEFAULT_LIVELINESS_CHECK_PERIOD = 60
	};

private:
	const string baseUrl, pingUrl, sinkUrlWithoutCompression, sinkUrlWithCompression;
	const unsigned int weight, number;

	unsigned int nSent, nAccepted, nRejected, nDropped, nActiveRequests;
	size_t bytesSent, bytesAccepted, bytesRejected, bytesDropped;
	ev_tstamp lastRequestBeginTime, lastRequestEndTime;
	ev_tstamp lastAcceptTime, lastRejectionErrorTime, lastDropErrorTime, lastLivelinessOkTime;
	ev_tstamp lastLivelinessCheckInitiateTime, lastLivelinessCheckEndTime, lastLivelinessCheckErrorTime;
	double avgUploadTime, avgUploadSpeed, avgServerProcessingTime;
	DiscExpMovingAverageWithStddev<700, 5 * 1000000, 10 * 1000000> bandwidthUsage;
	unsigned int livelinessCheckPeriod;
	string lastRejectionErrorMessage, lastDropErrorMessage, lastLivelinessCheckErrorMessage;
	bool up;
	bool checkingLiveliness;

	Json::Value inspectBandwidthUsageAsJson() const {
		if (bandwidthUsage.available()) {
			Json::Value doc;
			doc["average"] = byteSpeedToJson(bandwidthUsage.average()
				* 60 * 1000000, "minute");
			doc["stddev"] = byteSpeedToJson(bandwidthUsage.stddev()
				* 60 * 1000000, "minute");
			return doc;
		} else {
			return Json::Value(Json::nullValue);
		}
	}

public:
	Server(unsigned int _number, const StaticString &_baseUrl, unsigned int _weight)
		: baseUrl(_baseUrl.data(), _baseUrl.size()),
		  pingUrl(_baseUrl + "/ping"),
		  sinkUrlWithoutCompression(_baseUrl + "/sink"),
		  sinkUrlWithCompression(_baseUrl + "/sink?compressed=1"),
		  weight(_weight),
		  number(_number),
		  nSent(0),
		  nAccepted(0),
		  nRejected(0),
		  nDropped(0),
		  nActiveRequests(0),
		  bytesSent(0),
		  bytesAccepted(0),
		  bytesRejected(0),
		  bytesDropped(0),
		  lastRequestBeginTime(0),
		  lastRequestEndTime(0),
		  lastAcceptTime(0),
		  lastRejectionErrorTime(0),
		  lastDropErrorTime(0),
		  lastLivelinessOkTime(0),
		  lastLivelinessCheckInitiateTime(0),
		  lastLivelinessCheckEndTime(0),
		  lastLivelinessCheckErrorTime(0),
		  avgUploadTime(-1),
		  avgUploadSpeed(-1),
		  avgServerProcessingTime(-1),
		  livelinessCheckPeriod(DEFAULT_LIVELINESS_CHECK_PERIOD),
		  up(true),
		  checkingLiveliness(false)
	{
		assert(_weight > 0);
	}

	const string &getBaseUrl() const {
		return baseUrl;
	}

	const string &getPingUrl() const {
		return pingUrl;
	}

	const string &getSinkUrlWithCompression() const {
		return sinkUrlWithCompression;
	}

	const string &getSinkUrlWithoutCompression() const {
		return sinkUrlWithoutCompression;
	}

	unsigned int getWeight() const {
		return weight;
	}

	unsigned int getNumber() const {
		return number;
	}

	bool isUp() const {
		return up;
	}

	bool isCheckingLiveliness() const {
		return checkingLiveliness;
	}

	bool equals(const Server &other) const {
		return baseUrl == other.getBaseUrl() && weight == other.getWeight();
	}

	ev_tstamp getNextLivelinessCheckTime(ev_tstamp now) const {
		if (up || lastDropErrorTime == 0) {
			return 0;
		} else {
			ev_tstamp base = std::max(lastDropErrorTime, lastLivelinessCheckEndTime);
			return std::max(now, base + livelinessCheckPeriod);
		}
	}

	void setLivelinessCheckPeriod(unsigned int value) {
		livelinessCheckPeriod = value;
	}

	bool isBeingCheckedForLiveliness() const {
		return checkingLiveliness;
	}

	void reportRequestBegin(ev_tstamp now) {
		nSent++;
		nActiveRequests++;
		lastRequestBeginTime = now;
	}

	void reportRequestAccepted(size_t uploadSize, ev_tstamp uploadTime,
		ev_tstamp serverProcessingTime, ev_tstamp now)
	{
		nAccepted++;
		nActiveRequests--;
		bytesAccepted += uploadSize;
		lastRequestEndTime = now;
		lastAcceptTime = now;
		lastLivelinessOkTime = now;

		avgUploadTime = expMovingAverage(avgUploadTime, uploadTime, 0.5);
		avgUploadSpeed = expMovingAverage(avgUploadSpeed,
			uploadSize / uploadTime, 0.5);
		avgServerProcessingTime = expMovingAverage(
			avgServerProcessingTime, serverProcessingTime, 0.5);
		bandwidthUsage.update(uploadSize / uploadTime, now * 1000000);
	}

	void reportRequestRejected(size_t uploadSize, ev_tstamp now, ev_tstamp uploadTime,
		const string &errorMessage)
	{
		nRejected++;
		nActiveRequests--;
		bytesRejected += uploadSize;
		lastRequestEndTime = now;
		lastRejectionErrorTime = now;
		lastRejectionErrorMessage = errorMessage;
		lastLivelinessOkTime = now;

		avgUploadTime = expMovingAverage(avgUploadTime, uploadTime, 0.5);
		avgUploadSpeed = expMovingAverage(avgUploadSpeed,
			uploadSize / uploadTime, 0.5);
		bandwidthUsage.update(uploadSize / uploadTime, now * 1000000);
	}

	void reportRequestDropped(size_t uploadSize, ev_tstamp now,
		const string &errorMessage)
	{
		up = false;
		nDropped++;
		nActiveRequests--;
		bytesDropped += uploadSize;
		lastRequestEndTime = now;
		lastDropErrorTime = now;
		lastDropErrorMessage = errorMessage;
	}

	void reportLivelinessCheckBegin(ev_tstamp now) {
		assert(!checkingLiveliness);
		checkingLiveliness = true;
		lastLivelinessCheckEndTime = now;
	}

	void reportLivelinessCheckSuccess(ev_tstamp now) {
		assert(checkingLiveliness);
		checkingLiveliness = false;
		up = true;
		lastLivelinessCheckEndTime = now;
		lastLivelinessOkTime = now;
	}

	void reportLivelinessCheckError(ev_tstamp now, const string &errorMessage) {
		assert(checkingLiveliness);
		checkingLiveliness = false;
		lastLivelinessCheckEndTime = now;
		lastLivelinessCheckErrorTime = now;
		lastLivelinessCheckErrorMessage = errorMessage;
	}

	Json::Value inspectStateAsJson(ev_tstamp evNow, unsigned long long now) const {
		Json::Value doc;

		doc["number"] = number;
		doc["base_url"] = baseUrl;
		doc["ping_url"] = pingUrl;
		doc["sink_url"] = sinkUrlWithoutCompression;
		doc["weight"] = weight;
		doc["sent"] = byteSizeAndCountToJson(bytesSent, nSent);
		doc["accepted"] = byteSizeAndCountToJson(bytesAccepted, nAccepted);
		doc["rejected"] = byteSizeAndCountToJson(bytesRejected, nRejected);
		doc["dropped"] = byteSizeAndCountToJson(bytesDropped, nDropped);
		doc["average_upload_time"] = durationToJson(avgUploadTime);
		doc["average_upload_speed"] = byteSpeedToJson(
			avgUploadSpeed * 1000000.0, "second");
		doc["average_server_processing_time"] = durationToJson(
			avgServerProcessingTime);
		doc["bandwidth_usage"] = inspectBandwidthUsageAsJson();
		doc["up"] = up;
		doc["checking_liveliness"] = checkingLiveliness;
		doc["next_liveliness_check_time"] = evTimeToJson(
			getNextLivelinessCheckTime(evNow), evNow, now);
		doc["liveliness_check_period"] = durationToJson(livelinessCheckPeriod * 1000000ull);
		doc["last_liveliness_check_initiate_time"] = evTimeToJson(lastLivelinessCheckInitiateTime,
			evNow, now);
		doc["last_liveliness_check_end_time"] = evTimeToJson(lastLivelinessCheckEndTime,
			evNow, now);
		doc["last_liveliness_ok_time"] = evTimeToJson(lastLivelinessOkTime,
			evNow, now);

		if (!lastRejectionErrorMessage.empty()) {
			doc["last_rejection_error"] = evTimeToJson(lastRejectionErrorTime,
				evNow, now);
			doc["last_rejection_error"]["message"] = lastRejectionErrorMessage;
		}
		if (!lastDropErrorMessage.empty()) {
			doc["last_drop_error"] = evTimeToJson(lastDropErrorTime,
				evNow, now);
			doc["last_drop_error"]["message"] = lastDropErrorMessage;
		}
		if (!lastLivelinessCheckErrorMessage.empty()) {
			doc["last_liveliness_check_error"] = evTimeToJson(lastLivelinessCheckErrorTime,
				evNow, now);
			doc["last_liveliness_check_error"]["message"] = lastLivelinessCheckErrorMessage;
		}

		return doc;
	}
};

typedef boost::shared_ptr<Server> ServerPtr;


} // namespace RemoteSink
} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_SERVER_H_ */
