#ifndef _PASSENGER_REMOTE_SENDER_H_
#define _PASSENGER_REMOTE_SENDER_H_

#include <sys/types.h>
#include <ctime>
#include <cassert>
#include <curl/curl.h>
#include <zlib.h>

#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <oxt/thread.hpp>
#include <string>
#include <list>

#include "../Logging.h"
#include "../StaticString.h"
#include "../Utils/BlockingQueue.h"
#include "../Utils/SystemTime.h"
#include "../Utils/ScopeGuard.h"
#include "../Utils/Base64.h"

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
		
		CURL *curl;
		struct curl_slist *headers;
		char lastErrorMessage[CURL_ERROR_SIZE];
		string hostHeader;
		string responseBody;
		
		void resetConnection() {
			curl_easy_reset(curl);
			curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, lastErrorMessage);
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlDataReceived);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
			if (certificate.empty()) {
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
			} else {
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2);
				curl_easy_setopt(curl, CURLOPT_SSLCERT, certificate.c_str());
			}
			responseBody.clear();
		}
		
		void prepareRequest(const string &uri) {
			string url = string("https://") + ip + ":" + toString(port) + uri;
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			responseBody.clear();
		}
		
		static size_t curlDataReceived(void *buffer, size_t size, size_t nmemb, void *userData) {
			Server *self = (Server *) userData;
			self->responseBody.append((const char *) buffer, size * nmemb);
			return size * nmemb;
		}
		
	public:
		Server(const string &ip, const string &hostName, unsigned short port, const string &cert) {
			this->ip = ip;
			this->port = port;
			certificate = cert;
			
			curl = curl_easy_init();
			if (curl == NULL) {
				throw IOException("Unable to create a CURL handle");
			}
			
			hostHeader = "Host: " + hostName;
			headers = NULL;
			headers = curl_slist_append(headers, hostHeader.c_str());
			if (headers == NULL) {
				throw IOException("Unable to create a CURL linked list");
			}
			
			resetConnection();
		}
		
		~Server() {
			if (curl != NULL) {
				curl_easy_cleanup(curl);
			}
			curl_slist_free_all(headers);
		}
		
		bool ping() {
			ScopeGuard guard(boost::bind(&Server::resetConnection, this));
			prepareRequest("/ping");
			
			curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
			if (curl_easy_perform(curl) != 0) {
				P_DEBUG("Could not ping Union Station service server " << ip
					<< ": " << lastErrorMessage);
				return false;
			}
			if (responseBody == "pong") {
				guard.clear();
				return true;
			} else {
				P_DEBUG("Union Station service server " << ip <<
					" returned an unexpected ping message: " <<
					responseBody);
				return false;
			}
		}
		
		bool send(const Item &item) {
			ScopeGuard guard(boost::bind(&Server::resetConnection, this));
			prepareRequest("/sink");
			
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
			CURLcode code = curl_easy_perform(curl);
			curl_formfree(post);
			
			if (code == 0) {
				guard.clear();
				return true;
			} else {
				P_DEBUG("Could not send data to Union Station service server " << ip
					<< ": " << lastErrorMessage);
				return false;
			}
		}
	};
	
	typedef shared_ptr<Server> ServerPtr;
	
	string serviceAddress;
	unsigned short servicePort;
	string certificate;
	BlockingQueue<Item> queue;
	oxt::thread *thr;
	
	list<ServerPtr> servers;
	time_t nextCheckupTime;
	
	void threadMain() {
		ScopeGuard guard(boost::bind(&RemoteSender::freeThreadData, this));
		nextCheckupTime = 0;
		
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
		return nextCheckupTime == 0;
	}
	
	void recheckServers() {
		P_DEBUG("Rechecking Union Station service servers");
		
		vector<string> ips;
		vector<string>::const_iterator it;
		string hostName;
		bool someServersAreDown = false;
		
		ips = resolveHostname(serviceAddress, servicePort);
		
		servers.clear();
		for (it = ips.begin(); it != ips.end(); it++) {
			ServerPtr server(new Server(*it, serviceAddress, servicePort, certificate));
			if (server->ping()) {
				servers.push_back(server);
			} else {
				someServersAreDown = true;
			}
		}
		
		if (servers.empty()) {
			scheduleNextCheckup(5 * 60);
		} else if (someServersAreDown) {
			scheduleNextCheckup(60 * 60);
		} else {
			scheduleNextCheckup(3 * 60 * 60);
		}
	}
	
	void freeThreadData() {
		servers.clear(); // Invoke destructors inside this thread.
	}
	
	/**
	 * Schedules the next checkup to be run after the given number
	 * of seconds, unless there's already a checkup scheduled for
	 * earlier.
	 */
	void scheduleNextCheckup(unsigned int seconds) {
		time_t now = SystemTime::get();
		if (now > nextCheckupTime || (time_t) (now + seconds) < nextCheckupTime) {
			nextCheckupTime = now + seconds;
		}
	}
	
	unsigned int msecUntilNextCheckup() const {
		time_t now = SystemTime::get();
		if (now >= nextCheckupTime) {
			return 0;
		} else {
			return (nextCheckupTime - now) * 1000;
		}
	}
	
	bool timeForCheckup() const {
		return SystemTime::get() >= nextCheckupTime;
	}
	
	void sendOut(const Item &item) {
		bool sent = false;
		bool someServersWentDown = false;
		
		while (!sent && !servers.empty()) {
			// Pick first available server and put it on the back of the list
			// for round-robin load balancing.
			ServerPtr server = servers.front();
			servers.pop_front();
			if (server->send(item)) {
				servers.push_back(server);
				sent = true;
			} else {
				someServersWentDown = true;
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
	RemoteSender(const string &serviceAddress, unsigned short servicePort, const string &certificate)
		: queue(1024)
	{
		this->serviceAddress = serviceAddress;
		this->servicePort = servicePort;
		this->certificate = certificate;
		thr = new oxt::thread(
			boost::bind(&RemoteSender::threadMain, this),
			"RemoteSender thread",
			1024 * 64
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
		
		queue.add(item);
	}
};


} // namespace Passenger

#endif /* _PASSENGER_REMOTE_SENDER_H_ */
