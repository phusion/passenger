/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2014 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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

#include <Logging.h>
#include <StaticString.h>
#include <Utils.h>
#include <Utils/BlockingQueue.h>
#include <Utils/SystemTime.h>
#include <Utils/ScopeGuard.h>
#include <Utils/Base64.h>
#include <Utils/json.h>
#include <Utils/Curl.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


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
	private:
		string ip;
		unsigned short port;
		string certificate;
		const CurlProxyInfo *proxyInfo;

		CURL *curl;
		struct curl_slist *headers;
		char lastErrorMessage[CURL_ERROR_SIZE];
		string hostHeader;
		string responseBody;

		string pingURL;
		string sinkURL;

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
			curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, lastErrorMessage);
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

		bool handleSendResponse() {
			Json::Reader reader;
			Json::Value response;
			long httpCode = -1;

			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

			if (!reader.parse(responseBody, response, false) || !validateResponse(response)) {
				P_ERROR("The Union Station gateway server " << ip << " encountered an error "
					"while processing sent analytics data. It sent an invalid response "
					"(parse error: " << reader.getFormattedErrorMessages().c_str() <<
					"; HTTP code: " << httpCode <<
					"; data: \"" << cEscapeString(responseBody) << "\").");
				return false;
			} else if (response["status"].asString() == "ok") {
				if (httpCode == 200) {
					P_DEBUG("The Union Station gateway server " << ip << " accepted the packet.");
					return true;
				} else {
					P_ERROR("The Union Station gateway server " << ip << " encountered an error "
						"while processing sent analytics data. It sent an invalid response "
						"(HTTP code: " << httpCode <<
						"; data: \"" << cEscapeString(responseBody) << "\").");
					return false;
				}
			} else {
				// response == error
				P_ERROR("The Union Station gateway server " << ip << " did not accept the "
					"sent analytics data. Error message: " << response["message"].asString());
				// Return value of true is intentional. See comment for send().
				return true;
			}
		}

		void handleSendError() {
			P_ERROR("Could not send data to Union Station gateway server " << ip
				<< ": " << lastErrorMessage);
		}

		static size_t curlDataReceived(void *buffer, size_t size, size_t nmemb, void *userData) {
			Server *self = (Server *) userData;
			self->responseBody.append((const char *) buffer, size * nmemb);
			return size * nmemb;
		}

	public:
		Server(const string &ip, const string &hostName, unsigned short port, const string &cert,
			const CurlProxyInfo *proxyInfo)
		{
			this->ip = ip;
			this->port = port;
			certificate = cert;
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
			P_DEBUG("Pinging Union Station gateway " << ip << ":" << port);
			ScopeGuard guard(boost::bind(&Server::resetConnection, this));
			prepareRequest(pingURL);

			curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
			if (curl_easy_perform(curl) != 0) {
				P_DEBUG("Could not ping Union Station gateway server " << ip
					<< ": " << lastErrorMessage);
				return false;
			}
			if (responseBody == "pong") {
				guard.clear();
				return true;
			} else {
				P_DEBUG("Union Station gateway server " << ip <<
					" returned an unexpected ping message: " <<
					responseBody);
				return false;
			}
		}

		/** Returns true if the server is up, false if the server is down.
		 * The return value does NOT indicate whether the server accepted the data!
		 * Thus, if (for example) the Union Station key is invalid or disabled,
		 * but the connection is fine, then this method still returns true.
		 * This is because the return value is used to determine whether a different
		 * gateway server should be used. If the server is up but rejects the data
		 * then we'll want the code to keep sending future packets.
		 */
		bool send(const Item &item) {
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
			if (item.compressed) {
				base64_data = Base64::encode(item.data);
				curl_formadd(&post, &last,
					CURLFORM_PTRNAME, "data",
					CURLFORM_PTRCONTENTS, base64_data.c_str(),
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
				return handleSendResponse();
			} else {
				handleSendError();
				return false;
			}
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
	list<ServerPtr> servers;
	time_t nextCheckupTime;
	unsigned int packetsSent, packetsDropped;

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
		list<ServerPtr> servers;
		string hostName;
		bool someServersAreDown = false;

		ips = resolveHostname(gatewayAddress, gatewayPort);
		P_INFO(ips.size() << " Union Station gateway servers found");

		for (it = ips.begin(); it != ips.end(); it++) {
			ServerPtr server = boost::make_shared<Server>(*it, gatewayAddress, gatewayPort,
				certificate, &proxyInfo);
			if (server->ping()) {
				servers.push_back(server);
			} else {
				someServersAreDown = true;
			}
		}
		P_INFO(servers.size() << " Union Station gateway servers are up");

		if (servers.empty()) {
			scheduleNextCheckup(5 * 60);
		} else if (someServersAreDown) {
			scheduleNextCheckup(60 * 60);
		} else {
			scheduleNextCheckup(3 * 60 * 60);
		}

		boost::lock_guard<boost::mutex> l(syncher);
		this->servers = servers;
	}

	void freeThreadData() {
		boost::lock_guard<boost::mutex> l(syncher);
		servers.clear(); // Invoke destructors inside this thread.
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
		bool sent = false;
		bool someServersWentDown = false;

		while (!sent && !servers.empty()) {
			// Pick first available server and put it on the back of the list
			// for round-robin load balancing.
			ServerPtr server = servers.front();
			l.unlock();
			if (server->send(item)) {
				l.lock();
				servers.pop_front();
				servers.push_back(server);
				sent = true;
				packetsSent++;
			} else {
				l.lock();
				servers.pop_front();
				someServersWentDown = true;
				packetsDropped++;
			}
		}

		if (someServersWentDown) {
			if (servers.empty()) {
				scheduleNextCheckup(5 * 60);
			} else {
				scheduleNextCheckup(60 * 60);
			}
		}

		/* If all servers went down then all items in the queue will be
		 * effectively dropped until after the next checkup has detected
		 * servers that are up.
		 */
		if (!sent) {
			P_WARN("Dropping Union Station packet because no servers are available: "
				"key=" << item.unionStationKey <<
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

public:
	RemoteSender(const string &gatewayAddress, unsigned short gatewayPort, const string &certificate,
		const string &proxyAddress)
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
		nextCheckupTime = 0;
		packetsSent = 0;
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

	template<typename Stream>
	void inspect(Stream &stream) const {
		boost::lock_guard<boost::mutex> l(syncher);
		stream << "  Available servers (" << servers.size() << "): ";
		foreach (const ServerPtr server, servers) {
			stream << server->name() << " ";
		}
		stream << "\n";
		stream << "  Items in queue: " << queue.size() << "\n";
		stream << "  Packets sent out so far: " << packetsSent << "\n";
		stream << "  Packets dropped out so far: " << packetsDropped << "\n";
		stream << "  Next server checkup time: ";
		if (nextCheckupTime == 0) {
			stream << "not yet scheduled, waiting for first packet\n";
		} else {
			stream << "in " << distanceOfTimeInWords(nextCheckupTime) << "\n";
		}
	}
};


} // namespace Passenger

#endif /* _PASSENGER_REMOTE_SENDER_H_ */
