/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion Holding B.V.
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
#ifndef _PASSENGER_REMOTE_SENDER_H_
#define _PASSENGER_REMOTE_SENDER_H_

#include <sys/types.h>
#include <ctime>
#include <cassert>
#include <curl/curl.h>
#include <zlib.h>

#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <oxt/thread.hpp>
#include <string>
#include <list>
#include <jsoncpp/json.h>
#include <modp_b64.h>

#include <Logging.h>
#include <StaticString.h>
#include <Utils.h>
#include <Utils/BlockingQueue.h>
#include <Utils/SystemTime.h>
#include <Utils/ScopeGuard.h>
#include <Utils/JsonUtils.h>
#include <Utils/Curl.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;

#ifdef PASSENGER_IS_ENTERPRISE
	#define UST_ROUTER_CLIENT_DESCRIPTION PROGRAM_NAME " Enterprise " PASSENGER_VERSION
#else
	#define UST_ROUTER_CLIENT_DESCRIPTION PROGRAM_NAME " " PASSENGER_VERSION
#endif


class RemoteSender {
private:
	struct Item {
		bool exit;
		bool compressed;
		string unionStationKey;
		string nodeName;
		string category;
		string data;

		Item() {
			exit = false;
			compressed = false;
		}
	};

	class Server {
	public:
		enum SendResult {
			/**
			 * The gateway accepted the packet.
			 */
			SR_OK,

			/**
			 * Unable to contact the gateway: it appears to be down.
			 * Unable to obtain a valid HTTP response from the gateway.
			 */
			SR_DOWN,

			/**
			 * We were able to contact the gateway, but it appears to be
			 * responding with gibberish. It might be so that the gateway
			 * machine is up, but the actual service running inside is
			 * down or malfunctioning.
			 */
			SR_MALFUNCTION,

			/**
			 * We were able to contact the gateway, but the
			 * rejected the packet by responding with an error.
			 */
			SR_REJECTED
		};

	private:
		string ip;
		unsigned short port;
		string certificate;
		const CurlProxyInfo *proxyInfo;

		CURL *curl;
		struct curl_slist *headers;
		char lastCurlErrorMessage[CURL_ERROR_SIZE];
		string hostHeader;
		string responseBody;

		string pingURL;
		string sinkURL;

		mutable boost::mutex syncher;
		string lastErrorMessage;
		unsigned long long lastErrorTime;
		unsigned long long lastSuccessTime;
		unsigned int pingErrors;
		unsigned int packetsAccepted;
		unsigned int packetsRejected;
		unsigned int packetsDropped;

		void resetConnection() {
			if (curl != NULL) {
				#ifdef HAS_CURL_EASY_RESET
					curl_easy_reset(curl);
				#else
					curl_easy_cleanup(curl);
					curl = NULL;
				#endif
			}
			if (curl == NULL) {
				curl = curl_easy_init();
				if (curl == NULL) {
					throw IOException("Unable to create a CURL handle");
				}
			}
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180);
			curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, lastCurlErrorMessage);
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlDataReceived);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
			if (certificate.empty()) {
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
			} else {
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
				curl_easy_setopt(curl, CURLOPT_CAINFO, certificate.c_str());
			}
			/* No host name verification because Curl thinks the
			 * host name is the IP address. But if we have the
			 * certificate then it doesn't matter.
			 */
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
			setCurlProxy(curl, *proxyInfo);
			responseBody.clear();
		}

		void prepareRequest(const string &url) {
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			responseBody.clear();
		}

		static bool validateResponse(const Json::Value &response) {
			if (response.isObject() && response["status"].isString()) {
				string status = response["status"].asString();
				if (status == "ok") {
					return true;
				} else if (status == "error") {
					return response["message"].isString();
				} else {
					return false;
				}
			} else {
				return false;
			}
		}

		SendResult handleSendResponse(const Item &item) {
			Json::Reader reader;
			Json::Value response;
			long httpCode = -1;

			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

			if (!reader.parse(responseBody, response, false) || !validateResponse(response)) {
				setRequestError(
					"The Union Station gateway server " + ip +
					" encountered an error while processing sent analytics data. "
					"It sent an invalid response. Key: " + item.unionStationKey
					+ ". Parse error: " + reader.getFormattedErrorMessages()
					+ "; HTTP code: " + toString(httpCode)
					+ "; data: \"" + cEscapeString(responseBody) + "\"");
				return SR_MALFUNCTION;
			} else if (response["status"].asString() == "ok") {
				if (httpCode == 200) {
					handleResponseSuccess();
					P_DEBUG("The Union Station gateway server " << ip
						<< " accepted the packet. Key: "
						<< item.unionStationKey);
					return SR_OK;
				} else {
					setRequestError(
						"The Union Station gateway server " + ip
						+ " encountered an error while processing sent "
						"analytics data. It sent an invalid response. Key: "
						+ item.unionStationKey + ". HTTP code: "
						+ toString(httpCode) + ". Data: \""
						+ cEscapeString(responseBody) + "\"");
					return SR_MALFUNCTION;
				}
			} else {
				// response == error
				setPacketRejectedError(
					"The Union Station gateway server "
					+ ip + " did not accept the sent analytics data. "
					"Key: " + item.unionStationKey + ". "
					"Error: " + response["message"].asString());
				return SR_REJECTED;
			}
		}

		void handleSendError(const Item &item) {
			setRequestError(
				"Could not send data to Union Station gateway server " +
				ip + ". It might be down. Key: " + item.unionStationKey +
				". Error: " + lastCurlErrorMessage);
		}

		void setPingError(const string &message) {
			boost::lock_guard<boost::mutex> l(syncher);
			P_INFO(message);
			setLastErrorMessage(message);
			pingErrors++;
		}

		/**
		 * Handles the case when SendResult == SR_DOWN
		 * or SendResult == SR_MALFUNCTION.
		 * See SendResult comments for notes.
		 */
		void setRequestError(const string &message) {
			boost::lock_guard<boost::mutex> l(syncher);
			P_ERROR(message);
			setLastErrorMessage(message);
			packetsDropped++;
		}

		/**
		 * Handles the case when SendResult == SR_REJECTED.
		 * See SendResult comments for notes.
		 */
		void setPacketRejectedError(const string &message) {
			boost::lock_guard<boost::mutex> l(syncher);
			P_ERROR(message);
			setLastErrorMessage(message);
			packetsRejected++;
		}

		void setLastErrorMessage(const string &message) {
			lastErrorMessage = message;
			lastErrorTime = SystemTime::getUsec();
		}

		void handleResponseSuccess() {
			boost::lock_guard<boost::mutex> l(syncher);
			lastSuccessTime = SystemTime::getUsec();
			packetsAccepted++;
		}

		static size_t curlDataReceived(void *buffer, size_t size, size_t nmemb, void *userData) {
			Server *self = (Server *) userData;
			self->responseBody.append((const char *) buffer, size * nmemb);
			return size * nmemb;
		}

	public:
		Server(const string &ip, const string &hostName, unsigned short port,
			const string &cert, const CurlProxyInfo *proxyInfo)
		{
			this->ip = ip;
			this->port = port;
			this->certificate = cert;
			this->proxyInfo = proxyInfo;

			hostHeader = "Host: " + hostName;
			headers = NULL;
			headers = curl_slist_append(headers, hostHeader.c_str());
			if (headers == NULL) {
				throw IOException("Unable to create a CURL linked list");
			}

			// Older libcurl versions didn't strdup() any option
			// strings so we need to keep these in memory.
			pingURL = string("https://") + ip + ":" + toString(port) +
				"/ping";
			sinkURL = string("https://") + ip + ":" + toString(port) +
				"/sink";

			curl = NULL;
			lastErrorTime = 0;
			lastSuccessTime = 0;
			pingErrors = 0;
			packetsAccepted = 0;
			packetsRejected = 0;
			packetsDropped = 0;
			resetConnection();
		}

		~Server() {
			if (curl != NULL) {
				curl_easy_cleanup(curl);
			}
			curl_slist_free_all(headers);
		}

		string name() const {
			return ip + ":" + toString(port);
		}

		bool ping() {
			P_INFO("Pinging Union Station gateway " << ip << ":" << port);
			ScopeGuard guard(boost::bind(&Server::resetConnection, this));
			prepareRequest(pingURL);

			curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
			if (curl_easy_perform(curl) != 0) {
				setPingError(
					"Could not ping Union Station gateway server " +
					ip + ": " + lastCurlErrorMessage);
				return false;
			}
			if (responseBody == "pong") {
				guard.clear();
				return true;
			} else {
				setPingError(
					"Union Station gateway server " + ip +
					" returned an unexpected ping message: " +
					responseBody);
				return false;
			}
		}

		SendResult send(const Item &item) {
			ScopeGuard guard(boost::bind(&Server::resetConnection, this));
			prepareRequest(sinkURL);

			struct curl_httppost *post = NULL;
			struct curl_httppost *last = NULL;
			string base64_data;

			curl_formadd(&post, &last,
				CURLFORM_PTRNAME, "key",
				CURLFORM_PTRCONTENTS, item.unionStationKey.c_str(),
				CURLFORM_CONTENTSLENGTH, (long) item.unionStationKey.size(),
				CURLFORM_END);
			curl_formadd(&post, &last,
				CURLFORM_PTRNAME, "node_name",
				CURLFORM_PTRCONTENTS, item.nodeName.c_str(),
				CURLFORM_CONTENTSLENGTH, (long) item.nodeName.size(),
				CURLFORM_END);
			curl_formadd(&post, &last,
				CURLFORM_PTRNAME, "category",
				CURLFORM_PTRCONTENTS, item.category.c_str(),
				CURLFORM_CONTENTSLENGTH, (long) item.category.size(),
				CURLFORM_END);
			curl_formadd(&post, &last,
				CURLFORM_PTRNAME, "client_description",
				CURLFORM_PTRCONTENTS, UST_ROUTER_CLIENT_DESCRIPTION,
				CURLFORM_CONTENTSLENGTH, (long) sizeof(UST_ROUTER_CLIENT_DESCRIPTION),
				CURLFORM_END);
			if (item.compressed) {
				base64_data = modp::b64_encode(item.data);
				curl_formadd(&post, &last,
					CURLFORM_PTRNAME, "data",
					CURLFORM_PTRCONTENTS, base64_data.data(),
					CURLFORM_CONTENTSLENGTH, (long) base64_data.size(),
					CURLFORM_END);
				curl_formadd(&post, &last,
					CURLFORM_PTRNAME, "compressed",
					CURLFORM_PTRCONTENTS, "1",
					CURLFORM_END);
			} else {
				curl_formadd(&post, &last,
					CURLFORM_PTRNAME, "data",
					CURLFORM_PTRCONTENTS, item.data.c_str(),
					CURLFORM_CONTENTSLENGTH, (long) item.data.size(),
					CURLFORM_END);
			}

			curl_easy_setopt(curl, CURLOPT_HTTPGET, 0);
			curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);
			P_DEBUG("Sending Union Station packet: key=" << item.unionStationKey <<
				", node=" << item.nodeName << ", category=" << item.category <<
				", compressedDataSize=" << item.data.size());
			CURLcode code = curl_easy_perform(curl);
			curl_formfree(post);

			if (code == CURLE_OK) {
				guard.clear();
				return handleSendResponse(item);
			} else {
				handleSendError(item);
				return SR_DOWN;
			}
		}

		Json::Value inspectStateAsJson() const {
			Json::Value doc, errorDoc;
			doc["sink_url"] = sinkURL;
			doc["ping_url"] = pingURL;

			boost::lock_guard<boost::mutex> l(syncher);

			if (lastErrorTime == 0) {
				doc["last_error_time"] = Json::Value(Json::nullValue);
			} else {
				doc["last_error_time"] = timeToJson(lastErrorTime);
			}
			if (!lastErrorMessage.empty()) {
				doc["last_error_message"] = lastErrorMessage;
			}
			if (lastSuccessTime == 0) {
				doc["last_success_time"] = Json::Value(Json::nullValue);
			} else {
				doc["last_success_time"] = timeToJson(lastSuccessTime);
			}

			errorDoc["ping_errors"] = pingErrors;
			errorDoc["packets_dropped"] = packetsDropped;
			errorDoc["packets_rejected"] = packetsRejected;

			doc["errors"] = errorDoc;
			doc["packets_accepted"] = packetsAccepted;

			return doc;
		}
	};

	typedef boost::shared_ptr<Server> ServerPtr;

	string gatewayAddress;
	unsigned short gatewayPort;
	string certificate;
	CurlProxyInfo proxyInfo;
	BlockingQueue<Item> queue;
	oxt::thread *thr;

	mutable boost::mutex syncher;
	list<ServerPtr> upServers;
	vector<ServerPtr> downServers;
	time_t lastCheckupTime, nextCheckupTime;
	string lastDnsErrorMessage;
	unsigned int packetsAccepted, packetsRejected, packetsDropped;

	void threadMain() {
		ScopeGuard guard(boost::bind(&RemoteSender::freeThreadData, this));

		while (true) {
			Item item;
			bool hasItem;

			if (firstStarted()) {
				item = queue.get();
				hasItem = true;
			} else {
				hasItem = queue.timedGet(item, msecUntilNextCheckup());
			}

			if (hasItem) {
				if (item.exit) {
					return;
				} else {
					if (timeForCheckup()) {
						recheckServers();
					}
					sendOut(item);
				}
			} else if (timeForCheckup()) {
				recheckServers();
			}
		}
	}

	bool firstStarted() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return nextCheckupTime == 0;
	}

	void recheckServers() {
		P_INFO("Rechecking Union Station gateway servers (" << gatewayAddress << ")...");

		vector<string> ips;
		vector<string>::const_iterator it;
		list<ServerPtr> upServers;
		vector<ServerPtr> downServers;

		try {
			ips = resolveHostname(gatewayAddress, gatewayPort);
		} catch (const tracable_exception &e) {
			P_ERROR(e.what());
			// DNS errors tend to be temporary, so retry
			// after a short timeout.
			scheduleNextCheckup(1 * 60);
			// Take note of the error, but do not change the server
			// list so that the RemoteSender can keep working with
			// the last known server list.
			boost::lock_guard<boost::mutex> l(syncher);
			this->lastCheckupTime = SystemTime::get();
			this->lastDnsErrorMessage = e.what();
			return;
		}


		P_INFO(ips.size() << " Union Station gateway servers found");

		for (it = ips.begin(); it != ips.end(); it++) {
			ServerPtr server = boost::make_shared<Server>(
				*it, gatewayAddress, gatewayPort, certificate,
				&proxyInfo);
			if (server->ping()) {
				upServers.push_back(server);
			} else {
				downServers.push_back(server);
			}
		}
		P_INFO(upServers.size() << " Union Station gateway servers are up");

		if (downServers.empty()) {
			if (upServers.empty()) {
				// The DNS lookup was successful, but returned no results.
				// This is probably some kind of DNS misconfiguration which
				// the infrastructure team is working on, so we check back
				// in a short while. It may not help because DNS queries are
				// cached, but it's better than not trying.
				scheduleNextCheckup(1 * 60);
			} else {
				// If all gateways are healthy then the list of gateways
				// is unlikely to change, so schedule the next checkup
				// in 3 hours.
				scheduleNextCheckup(3 * 60 * 60);
			}
		} else {
			// If some gateways are down then the infrastructure team
			// is likely already working on the problem, so we check
			// back in 1 minute.
			scheduleNextCheckup(1 * 60);
		}

		boost::lock_guard<boost::mutex> l(syncher);
		this->lastCheckupTime = SystemTime::get();
		this->upServers = upServers;
		this->downServers = downServers;
		this->lastDnsErrorMessage.clear();
	}

	void freeThreadData() {
		boost::lock_guard<boost::mutex> l(syncher);
		// Invoke destructors inside this thread.
		upServers.clear();
		downServers.clear();
	}

	/**
	 * Schedules the next checkup to be run after the given number
	 * of seconds, unless there's already a checkup scheduled for
	 * earlier.
	 */
	void scheduleNextCheckup(unsigned int seconds) {
		time_t now = SystemTime::get();
		if (now >= nextCheckupTime || (time_t) (now + seconds) < nextCheckupTime) {
			nextCheckupTime = now + seconds;
			P_DEBUG("Next checkup time in about " << seconds << " seconds");
		}
	}

	unsigned int msecUntilNextCheckup() const {
		boost::lock_guard<boost::mutex> l(syncher);
		time_t now = SystemTime::get();
		if (now >= nextCheckupTime) {
			return 0;
		} else {
			return (nextCheckupTime - now) * 1000;
		}
	}

	bool timeForCheckup() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return SystemTime::get() >= nextCheckupTime;
	}

	void sendOut(const Item &item) {
		boost::unique_lock<boost::mutex> l(syncher);
		bool done = false;
		bool accepted = false;
		bool rejected = false;
		bool upServersEmpty;

		while (!done && !upServers.empty()) {
			// Pick first available server and put it on the back of the list
			// for round-robin load balancing.
			ServerPtr server = upServers.front();

			l.unlock();
			Server::SendResult result = server->send(item);
			l.lock();

			if (result == Server::SR_OK) {
				upServers.pop_front();
				upServers.push_back(server);
				accepted = true;
				done = true;
			} else if (result == Server::SR_REJECTED) {
				upServers.pop_front();
				upServers.push_back(server);
				rejected = true;
				done = true;
			} else {
				upServers.pop_front();
				downServers.push_back(server);
			}
		}

		if (!downServers.empty()) {
			// If some gateways are down then the infrastructure team
			// is likely already working on the problem, so we check
			// back in 1 minute.
			scheduleNextCheckup(1 * 60);
		}

		if (accepted) {
			packetsAccepted++;
		} else if (rejected) {
			packetsRejected++;
		} else {
			packetsDropped++;
		}

		upServersEmpty = upServers.empty();

		l.unlock();

		if (!accepted && !rejected) {
			assert(upServersEmpty);
			(void) upServersEmpty; // Avoid compiler warning

			/* If all servers went down then all items in the queue will be
			 * effectively dropped until after the next checkup has detected
			 * servers that are up.
			 */
			P_WARN("Dropping Union Station packet because no servers are"
				" available. Run `passenger-status --show=union_station` to"
				" view server status. Details of dropped packet:"
				" key=" << item.unionStationKey <<
				", node=" << item.nodeName <<
				", category=" << item.category <<
				", compressedDataSize=" << item.data.size());
		}
	}

	bool compress(const StaticString data[], unsigned int count, string &output) {
		if (count == 0) {
			StaticString newdata;
			return compress(&newdata, 1, output);
		}

		unsigned char out[128 * 1024];
		z_stream strm;
		int ret, flush;
		unsigned int i, have;

		strm.zalloc = Z_NULL;
		strm.zfree  = Z_NULL;
		strm.opaque = Z_NULL;
		ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
		if (ret != Z_OK) {
			return false;
		}

		for (i = 0; i < count; i++) {
			strm.avail_in = data[i].size();
			strm.next_in  = (unsigned char *) data[i].c_str();
			flush = (i == count - 1) ? Z_FINISH : Z_NO_FLUSH;

			do {
				strm.avail_out = sizeof(out);
				strm.next_out  = out;
				ret = deflate(&strm, flush);
				assert(ret != Z_STREAM_ERROR);
				have = sizeof(out) - strm.avail_out;
				output.append((const char *) out, have);
			} while (strm.avail_out == 0);
			assert(strm.avail_in == 0);
		}
		assert(ret == Z_STREAM_END);

		deflateEnd(&strm);
		return true;
	}

	Json::Value inspectUpServersStateAsJson() const {
		Json::Value doc(Json::arrayValue);
		foreach (const ServerPtr server, upServers) {
			doc.append(server->inspectStateAsJson());
		}
		return doc;
	}

	Json::Value inspectDownServersStateAsJson() const {
		Json::Value doc(Json::arrayValue);
		foreach (const ServerPtr server, downServers) {
			doc.append(server->inspectStateAsJson());
		}
		return doc;
	}

public:
	RemoteSender(const string &gatewayAddress, unsigned short gatewayPort,
		const string &certificate, const string &proxyAddress)
		: queue(1024)
	{
		TRACE_POINT();
		this->gatewayAddress = gatewayAddress;
		this->gatewayPort = gatewayPort;
		this->certificate = certificate;
		try {
			this->proxyInfo = prepareCurlProxy(proxyAddress);
		} catch (const ArgumentException &e) {
			throw RuntimeException("Invalid Union Station proxy address \"" +
				proxyAddress + "\": " + e.what());
		}
		lastCheckupTime = 0;
		nextCheckupTime = 0;
		packetsAccepted = 0;
		packetsRejected = 0;
		packetsDropped = 0;
		thr = new oxt::thread(
			boost::bind(&RemoteSender::threadMain, this),
			"RemoteSender thread",
			1024 * 512
		);
	}

	~RemoteSender() {
		Item item;
		item.exit = true;
		queue.add(item);
		/* Wait until the thread sends out all queued items.
		 * If this cannot be done within a short amount of time,
		 * e.g. because all servers are down, then we'll get killed
		 * by the watchdog anyway.
		 */
		thr->join();
		delete thr;
	}

	void schedule(const string &unionStationKey, const StaticString &nodeName,
		const StaticString &category, const StaticString data[],
		unsigned int count)
	{
		Item item;

		item.unionStationKey = unionStationKey;
		item.nodeName = nodeName;
		item.category = category;

		if (compress(data, count, item.data)) {
			item.compressed = true;
		} else {
			size_t size = 0;
			unsigned int i;

			for (i = 0; i < count; i++) {
				size += data[i].size();
			}
			item.data.reserve(size);
			for (i = 0; i < count; i++) {
				item.data.append(data[i].c_str(), data[i].size());
			}
		}

		P_DEBUG("Scheduling Union Station packet: key=" << unionStationKey <<
			", node=" << nodeName << ", category=" << category <<
			", compressedDataSize=" << item.data.size());

		if (!queue.tryAdd(item)) {
			P_WARN("The Union Station gateway isn't responding quickly enough; dropping packet.");
			boost::lock_guard<boost::mutex> l(syncher);
			packetsDropped++;
		}
	}

	unsigned int queued() const {
		return queue.size();
	}

	Json::Value inspectStateAsJson() const {
		Json::Value doc;
		boost::lock_guard<boost::mutex> l(syncher);
		doc["up_servers"] = inspectUpServersStateAsJson();
		doc["down_servers"] = inspectDownServersStateAsJson();
		doc["queue_size"] = queue.size();
		doc["packets_accepted"] = packetsAccepted;
		doc["packets_rejected"] = packetsRejected;
		doc["packets_dropped"] = packetsDropped;
		if (certificate.empty()) {
			doc["certificate"] = Json::nullValue;
		} else {
			doc["certificate"] = certificate;
		}
		if (lastCheckupTime == 0) {
			doc["last_server_checkup_time"] = Json::Value(Json::nullValue);
			doc["last_server_checkup_time_note"] = "not yet started";
		} else {
			doc["last_server_checkup_time"] = timeToJson(lastCheckupTime * 1000000.0);
		}
		if (nextCheckupTime == 0) {
			doc["next_server_checkup_time"] = Json::Value(Json::nullValue);
			doc["next_server_checkup_time_note"] = "not yet scheduled, waiting for first packet";
		} else {
			doc["next_server_checkup_time"] = timeToJson(nextCheckupTime * 1000000.0);
		}
		if (!lastDnsErrorMessage.empty()) {
			doc["last_dns_error_message"] = lastDnsErrorMessage;
		}
		return doc;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_REMOTE_SENDER_H_ */
