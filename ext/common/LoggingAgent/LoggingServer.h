/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_LOGGING_SERVER_H_
#define _PASSENGER_LOGGING_SERVER_H_

#include <oxt/system_calls.hpp>
#include <oxt/macros.hpp>
#include <boost/shared_ptr.hpp>
#include <string>
#include <sstream>
#include <map>
#include <ev++.h>
#include <curl/curl.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <grp.h>
#include <cstring>
#include <ctime>

#include "../EventedMessageServer.h"
#include "../MessageReadersWriters.h"
#include "../StaticString.h"
#include "../Exceptions.h"
#include "../MessageChannel.h"
#include "../Constants.h"
#include "../Utils.h"
#include "../Utils/MD5.h"
#include "../Utils/IOUtils.h"
#include "../Utils/StrIntUtils.h"


namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


#define UNION_STATION_SERVICE_HOSTNAME "service.unionstationapp.com"


class LoggingServer: public EventedMessageServer {
private:
	struct LogSink {
		time_t lastUsed;
		time_t lastFlushed;
		
		LogSink() {
			lastUsed = time(NULL);
			lastFlushed = 0;
		}
		
		virtual ~LogSink() {
			flush();
		}
		
		virtual bool isRemote() const {
			return false;
		}
		
		virtual void append(const StaticString data[], unsigned int count) = 0;
		virtual void flush() { }
		virtual void dump(stringstream &stream) const { };
	};
	
	typedef shared_ptr<LogSink> LogSinkPtr;
	
	struct LogFile: public LogSink {
		static const unsigned int BUFFER_CAPACITY = 8 * 1024;
		
		string filename;
		FileDescriptor fd;
		char buffer[BUFFER_CAPACITY];
		unsigned int bufferSize;
		
		LogFile(const string &filename, mode_t filePermissions)
			: LogSink()
		{
			int ret;
			
			bufferSize = 0;
			
			this->filename = filename;
			fd = syscalls::open(filename.c_str(),
				O_CREAT | O_WRONLY | O_APPEND,
				filePermissions);
			if (fd == -1) {
				int e = errno;
				throw FileSystemException("Cannnot open file", e, filename);
			}
			do {
				ret = fchmod(fd, filePermissions);
			} while (ret == -1 && errno == EINTR);
		}
		
		virtual void append(const StaticString data[], unsigned int count) {
			size_t totalSize = 0;
			unsigned int i;
			
			for (i = 0; i < count; i++) {
				totalSize += data[i].size();
			}
			if (bufferSize + totalSize > BUFFER_CAPACITY) {
				StaticString data2[count + 1];
				
				data2[0] = StaticString(buffer, bufferSize);
				for (i = 0; i < count; i++) {
					data2[i + 1] = data[i];
				}
				lastFlushed = time(NULL);
				gatheredWrite(fd, data2, count + 1);
				bufferSize = 0;
			} else {
				for (i = 0; i < count; i++) {
					memcpy(buffer + bufferSize, data[i].data(), data[i].size());
					bufferSize += data[i].size();
				}
			}
		}
		
		virtual void flush() {
			if (bufferSize > 0) {
				lastFlushed = time(NULL);
				MessageChannel(fd).writeRaw(StaticString(buffer, bufferSize));
				bufferSize = 0;
			}
		}
		
		virtual void dump(stringstream &stream) const {
			stream << "   Log file: file=" << filename << ", "
				"age = " << (lastUsed - time(NULL)) << "\n";
		}
	};
	
	typedef shared_ptr<LogFile> LogFilePtr;
	
	struct RemoteSink: public LogSink {
		static const unsigned int BUFFER_CAPACITY = 8 * 1024;
		
		LoggingServer *server;
		char buffer[BUFFER_CAPACITY];
		unsigned int bufferSize;
		string unionStationKey;
		string nodeName;
		string category;
		
		RemoteSink(LoggingServer *server, const string &unionStationKey,
			const string &nodeName, const string &category)
		{
			this->server = server;
			this->bufferSize = 0;
			this->unionStationKey = unionStationKey;
			this->nodeName = nodeName;
			this->category = category;
		}
		
		virtual bool isRemote() const {
			return true;
		}
		
		virtual void append(const StaticString data[], unsigned int count) {
			size_t totalSize = 0;
			unsigned int i;
			
			for (i = 0; i < count; i++) {
				totalSize += data[i].size();
			}
			if (bufferSize + totalSize > BUFFER_CAPACITY) {
				StaticString data2[count + 1];
				
				data2[0] = StaticString(buffer, bufferSize);
				for (i = 0; i < count; i++) {
					data2[i + 1] = data[i];
				}
				lastFlushed = time(NULL);
				server->sendToRemoteServer(unionStationKey, nodeName, category,
					data2, count + 1);
				bufferSize = 0;
			} else {
				for (i = 0; i < count; i++) {
					memcpy(buffer + bufferSize, data[i].data(), data[i].size());
					bufferSize += data[i].size();
				}
			}
		}
		
		virtual void flush() {
			if (bufferSize > 0) {
				lastFlushed = time(NULL);
				StaticString data(buffer, bufferSize);
				server->sendToRemoteServer(unionStationKey, nodeName,
					category, &data, 1);
				bufferSize = 0;
			}
		}
		
		virtual void dump(stringstream &stream) const {
			stream << "   Remote sink: "
				"key=" << unionStationKey << ", "
				"node=" << nodeName << ", "
				"category=" << category << ", "
				"age=" << (lastUsed - time(NULL)) << "\n";
		}
	};
	
	struct Transaction {
		LogSinkPtr logSink;
		string txnId;
		string groupName;
		string category;
		unsigned int writeCount;
		unsigned int refcount;
		/** Number of stateful clients that current have this Transaction open.
		 * @invariant statefulRefcount <= refcount
		 */
		unsigned int statefulRefcount;
		/** Last time this Transaction was used. Hint for garbage collector. */
		time_t lastUsed;
		
		bool garbageCollectable() const {
			return statefulRefcount == 0;
		}
		
		bool stale(time_t currentTime) const {
			return age(currentTime) >= 60 * 60;
		}
		
		time_t age(time_t currentTime) const {
			return currentTime - lastUsed;
		}
		
		void dump(stringstream &stream) const {
			stream << "   Transaction " << txnId << ":\n";
			stream << "      Group: " << groupName << "\n";
			stream << "      Category: " << category << "\n";
			stream << "      Refcount: " << refcount << "\n";
			stream << "      StatefulRefcount: " << statefulRefcount << "\n";
			stream << "      Age: " << age(time(NULL)) << "\n";
		}
	};
	
	typedef shared_ptr<Transaction> TransactionPtr;
	typedef map<string, LogSinkPtr> LogSinkCache;
	
	struct RemoteServer {
		string ip;
		unsigned int port;
		CURL *curl;
		struct curl_slist *headers;
		char lastErrorMessage[CURL_ERROR_SIZE];
		string responseBody;
		
		RemoteServer(const string &ip, unsigned int port) {
			TRACE_POINT();
			this->ip   = ip;
			this->port = port;
			curl = curl_easy_init();
			if (curl == NULL) {
				throw IOException("Unable to create a CURL handle");
			}
			headers = NULL;
			headers = curl_slist_append(headers, "Host: " UNION_STATION_SERVICE_HOSTNAME);
			if (headers == NULL) {
				throw IOException("Unable to create a CURL linked list");
			}
			resetConnection();
		}
		
		~RemoteServer() {
			if (curl != NULL) {
				curl_easy_cleanup(curl);
			}
			curl_slist_free_all(headers);
		}
		
		void resetConnection() {
			curl_easy_reset(curl);
			curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, lastErrorMessage);
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlDataReceived);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
		}
		
		bool sendData(const string &unionStationKey, const StaticString &nodeName,
			const StaticString &category, const StaticString data[],
			unsigned int count)
		{
			unsigned int fullSize = 0;
			unsigned int i;
			for (i = 0; i < count; i++) {
				fullSize += data[i].size();
			}
			
			string fullData;
			fullData.reserve(fullSize);
			for (i = 0; i < count; i++) {
				fullData.append(data[i].c_str(), data[i].size());
			}
			
			ScopeGuard guard(boost::bind(&RemoteServer::resetConnection, this));
			prepareRequest("/sink");
			
			struct curl_httppost *post = NULL;
			struct curl_httppost *last = NULL;
			curl_formadd(&post, &last,
				CURLFORM_PTRNAME, "key",
				CURLFORM_PTRCONTENTS, unionStationKey.c_str(),
				CURLFORM_END);
			curl_formadd(&post, &last,
				CURLFORM_PTRNAME, "node_name",
				CURLFORM_PTRCONTENTS, nodeName.c_str(),
				CURLFORM_CONTENTSLENGTH, (long) nodeName.size(),
				CURLFORM_END);
			curl_formadd(&post, &last,
				CURLFORM_PTRNAME, "category",
				CURLFORM_PTRCONTENTS, category.c_str(),
				CURLFORM_CONTENTSLENGTH, (long) category.size(),
				CURLFORM_END);
			curl_formadd(&post, &last,
				CURLFORM_PTRNAME, "data",
				CURLFORM_PTRCONTENTS, fullData.c_str(),
				CURLFORM_CONTENTSLENGTH, (long) fullData.size(),
				CURLFORM_END);
			
			curl_easy_setopt(curl, CURLOPT_HTTPGET, 0);
			curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);
			CURLcode code = curl_easy_perform(curl);
			curl_formfree(post);
			
			if (code == 0) {
				guard.success();
				return true;
			} else {
				P_DEBUG("Could not send data to the Union Station service server: " <<
					lastErrorMessage);
				return false;
			}
		}
		
		bool ping() {
			ScopeGuard guard(boost::bind(&RemoteServer::resetConnection, this));
			prepareRequest("/ping");
			curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
			if (curl_easy_perform(curl) != 0) {
				P_DEBUG("Could not ping Union Station service server: " <<
					lastErrorMessage);
				return false;
			}
			if (responseBody == "pong") {
				guard.success();
				return true;
			} else {
				P_DEBUG("Union Station service server returned an "
					"unexpected ping message: " << responseBody);
				return false;
			}
		}
	
	private:
		class ScopeGuard {
		private:
			function<void ()> func;
		public:
			ScopeGuard(const function<void()> &func) {
				this->func = func;
			}
			
			~ScopeGuard() {
				if (func) {
					func();
				}
			}
			
			void success() {
				func = function<void()>();
			}
		};
		
		void prepareRequest(const string &uri) {
			string url = string("https://") + ip + ":" + toString(port) + uri;
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			responseBody.clear();
		}
		
		static size_t curlDataReceived(void *buffer, size_t size, size_t nmemb, void *userData) {
			RemoteServer *self = (RemoteServer *) userData;
			self->responseBody.append((const char *) buffer, size * nmemb);
			return size * nmemb;
		}
	};
	
	typedef shared_ptr<RemoteServer> RemoteServerPtr;
	
	struct Client: public EventedMessageServer::Client {
		string nodeName;
		bool stateless;
		bool initialized;
		char nodeId[MD5_HEX_SIZE];
		/**
		 * Set of transaction IDs opened by this client.
		 * @invariant This is a subset of the transaction IDs in the 'transactions' member.
		 */
		set<string> openTransactions;
		ScalarMessage dataReader;
		TransactionPtr currentTransaction;
		string currentTimestamp;
		
		Client(LoggingServer *server)
			: EventedMessageServer::Client(server)
		{
			stateless = false;
			initialized = false;
			dataReader.setMaxSize(1024 * 128);
		}
	};
	
	typedef shared_ptr<Client> ClientPtr;
	typedef map<string, TransactionPtr> TransactionMap;
	
	string dir;
	gid_t gid;
	string dirPermissions;
	mode_t filePermissions;
	ev::timer garbageCollectionTimer;
	ev::timer logFlushingTimer;
	ev::timer exitTimer;
	TransactionMap transactions;
	LogSinkCache logSinkCache;
	RandomGenerator randomGenerator;
	bool exitRequested;
	
	boost::mutex remoteServersLock;
	bool remoteServersFirstQueried;
	bool remoteServerWentDown;
	condition_variable remoteServersChanged;
	unsigned int currentRemoteServer;
	vector<RemoteServerPtr> remoteServers;
	shared_ptr<oxt::thread> remoteServerCheckThread;
	string unionStationServiceIp;
	unsigned short unionStationServicePort;
	
	void sendErrorToClient(const EventedMessageServer::ClientPtr &client, const string &message) {
		writeArrayMessage(client, "error", message.c_str(), NULL);
		logError(client, message);
	}
	
	bool expectingArgumentsCount(const EventedMessageServer::ClientPtr &client, const vector<StaticString> &args, unsigned int size) {
		if (args.size() == size) {
			return true;
		} else {
			sendErrorToClient(client, "Invalid number of arguments");
			disconnect(client);
			return false;
		}
	}
	
	bool expectingInitialized(const EventedMessageServer::ClientPtr &eclient) {
		Client *client = static_cast<Client *>(eclient.get());
		if (client->initialized) {
			return true;
		} else {
			sendErrorToClient(eclient, "Not yet initialized");
			disconnect(eclient);
			return false;
		}
	}
	
	bool validTxnId(const StaticString &txnId) const {
		// must contain timestamp
		// must contain separator
		// must contain random id
		// must not be too large
		return !txnId.empty();
	}
	
	bool validUnionStationKey(const StaticString &key) const {
		// must be hexadecimal
		// must not be too large
		return !key.empty();
	}
	
	bool supportedCategory(const StaticString &category) const {
		return category == "requests" || category == "processes" || category == "exceptions";
	}
	
	time_t extractTimestamp(const StaticString &txnId) const {
		const char *timestampEnd = (const char *) memchr(txnId.c_str(), '-', txnId.size());
		if (timestampEnd == NULL) {
			return 0;
		} else {
			time_t timestamp = hexToULL(
				StaticString(txnId.c_str(), timestampEnd - txnId.c_str())
			);
			return timestamp * 60;
		}
	}
	
	void appendVersionAndGroupId(string &output, const StaticString &groupName) const {
		md5_state_t state;
		md5_byte_t  digest[MD5_SIZE];
		char        checksum[MD5_HEX_SIZE];
		
		output.append("/1/", 3);
		
		md5_init(&state);
		md5_append(&state, (const md5_byte_t *) groupName.data(), groupName.size());
		md5_finish(&state, digest);
		toHex(StaticString((const char *) digest, MD5_SIZE), checksum);
		output.append(checksum, MD5_HEX_SIZE);
	}
	
	string determineFilename(const StaticString &groupName, const char *nodeId,
		const StaticString &category, const StaticString &txnId) const
	{
		time_t timestamp;
		struct tm tm;
		char time_str[14];
		
		timestamp = extractTimestamp(txnId);
		gmtime_r(&timestamp, &tm);
		strftime(time_str, sizeof(time_str), "%Y/%m/%d/%H", &tm);
		
		string filename;
		filename.reserve(dir.size()
			+ (3 + MD5_HEX_SIZE) // version and group ID
			+ 1                  // "/"
			+ MD5_HEX_SIZE       // node ID
			+ 1                  // "/"
			+ category.size()
			+ 1                  // "/"
			+ sizeof(time_str)   // including null terminator, which we use as space for "/"
			+ sizeof("log.txt")
		);
		filename.append(dir);
		appendVersionAndGroupId(filename, groupName);
		filename.append(1, '/');
		filename.append(nodeId, MD5_HEX_SIZE);
		filename.append(1, '/');
		filename.append(category.c_str(), category.size());
		filename.append(1, '/');
		filename.append(time_str);
		filename.append("/log.txt");
		return filename;
	}
	
	bool openLogFileWithCache(const string &filename, LogSinkPtr &theLogSink) {
		string cacheKey = "file:" + filename;
		LogSinkCache::iterator it = logSinkCache.find(cacheKey);
		if (it == logSinkCache.end()) {
			makeDirTree(extractDirName(filename), dirPermissions,
				USER_NOT_GIVEN, gid);
			LogFilePtr logFile(new LogFile(filename, filePermissions));
			theLogSink = logSinkCache[cacheKey] = logFile;
			return false;
		} else {
			theLogSink = it->second;
			theLogSink->lastUsed = time(NULL);
			return true;
		}
	}
	
	void setupGroupAndNodeDir(Client *client, const StaticString &groupName) {
		string filename, groupDir, nodeDir;
		
		filename.append(dir);
		appendVersionAndGroupId(filename, groupName);
		groupDir = filename;
		
		filename.append("/");
		filename.append(client->nodeId, MD5_HEX_SIZE);
		nodeDir = filename;
		
		createFile(groupDir + "/group_name.txt", groupName,
			filePermissions, USER_NOT_GIVEN, GROUP_NOT_GIVEN,
			false);
		if (getFileType(groupDir + "/uuid.txt") == FT_NONEXISTANT) {
			createFile(groupDir + "/uuid.txt",
				randomGenerator.generateAsciiString(24),
				filePermissions, USER_NOT_GIVEN, GROUP_NOT_GIVEN,
				false);
		}
		
		createFile(nodeDir + "/node_name.txt", client->nodeName,
			filePermissions, USER_NOT_GIVEN, GROUP_NOT_GIVEN,
			false);
		if (getFileType(nodeDir + "/uuid.txt") == FT_NONEXISTANT) {
			createFile(nodeDir + "/uuid.txt",
				randomGenerator.generateAsciiString(24),
				filePermissions, USER_NOT_GIVEN, GROUP_NOT_GIVEN,
				false);
		}
	}
	
	void openRemoteSink(const StaticString &unionStationKey, const string &nodeName,
		const string &category, LogSinkPtr &theLogSink)
	{
		string cacheKey = "remote:";
		cacheKey.append(unionStationKey.c_str(), unionStationKey.size());
		cacheKey.append(1, '\0');
		cacheKey.append(nodeName);
		cacheKey.append(1, '\0');
		cacheKey.append(category);
		
		LogSinkCache::iterator it = logSinkCache.find(cacheKey);
		if (it == logSinkCache.end()) {
			theLogSink = ptr(new RemoteSink(this, unionStationKey,
				nodeName, category));
			logSinkCache[cacheKey] = theLogSink;
		} else {
			theLogSink = it->second;
			theLogSink->lastUsed = time(NULL);
		}
	}
	
	void sendToRemoteServer(const string &unionStationKey, const StaticString &nodeName,
		const StaticString &category, const StaticString data[], unsigned int count)
	{
		TRACE_POINT();
		unique_lock<boost::mutex> l(remoteServersLock);
		while (!remoteServersFirstQueried) {
			remoteServersChanged.wait(l);
		}
		
		UPDATE_TRACE_POINT();
		bool sent = false;
		
		// Try to send the data to whichever server is up.
		while (remoteServers.size() > 0 && !sent) {
			currentRemoteServer = currentRemoteServer % remoteServers.size();
			RemoteServerPtr server = remoteServers[currentRemoteServer];
			if (server->sendData(unionStationKey, nodeName, category, data, count)) {
				// Have the next send command send to another
				// server for load balancing.
				currentRemoteServer++;
				sent = true;
			} else {
				// If the server is down then remove it.
				remoteServers.erase(remoteServers.begin() + currentRemoteServer);
				remoteServerWentDown = true;
				remoteServersChanged.notify_all();
			}
		}
		
		// If no servers are up then discard the data.
	}
	
	void writeLogEntry(const TransactionPtr &transaction, const StaticString &timestamp,
		const StaticString &data)
	{
		// TODO: validate timestamp
		// TODO: validate contents: must be valid ascii and containing no newlines
		char writeCountStr[sizeof(unsigned int) * 2 + 1];
		integerToHex(transaction->writeCount, writeCountStr);
		StaticString args[] = {
			transaction->txnId,
			" ",
			timestamp,
			" ",
			writeCountStr,
			" ",
			data,
			"\n"
		};
		transaction->writeCount++;
		transaction->logSink->append(args, sizeof(args) / sizeof(StaticString));
	}
	
	void writeDetachEntry(const TransactionPtr &transaction) {
		char timestamp[2 * sizeof(unsigned long long) + 1];
		integerToHex<unsigned long long>(SystemTime::getUsec(), timestamp);
		writeDetachEntry(transaction, timestamp);
	}
	
	void writeDetachEntry(const TransactionPtr &transaction, const StaticString &timestamp) {
		writeLogEntry(transaction, timestamp, "DETACH");
	}
	
	bool requireRights(const EventedMessageServer::ClientPtr &eclient, Account::Rights rights) {
		Client *client = (Client *) eclient.get();
		if (client->messageServer.account->hasRights(rights)) {
			return true;
		} else {
			P_TRACE(2, "Security error: insufficient rights to execute this command.");
			writeArrayMessage(eclient, "SecurityException",
				"Insufficient rights to execute this command.",
				NULL);
			disconnect(eclient);
			return false;
		}
	}
	
	/* Release all log sinks that haven't been used for more than 2 hours. */
	void releaseStaleLogSinks(time_t now) {
		LogSinkCache::iterator it;
		LogSinkCache::iterator end = logSinkCache.end();
		vector<string> toDelete;
		
		for (it = logSinkCache.begin(); it != end; it++) {
			if (now - it->second->lastUsed > 2 * 60 * 60) {
				toDelete.push_back(it->first);
			}
		}
		
		vector<string>::const_iterator it2;
		for (it2 = toDelete.begin(); it2 != toDelete.end(); it2++) {
			logSinkCache.erase(*it2);
		}
	}
	
	void releaseStaleTransactions(time_t now) {
		TransactionMap::iterator it;
		TransactionMap::iterator end = transactions.end();
		
		P_TRACE(2, "Currently open transactions: " << transactions.size());
		for (it = transactions.begin(); it != end; it++) {
			TransactionPtr &transaction = it->second;
			if (transaction->garbageCollectable() && transaction->stale(now)) {
				P_DEBUG("Garbage collecting transaction " << it->first);
				it--;
				transactions.erase(it);
				writeDetachEntry(transaction);
			} else {
				P_TRACE(2, "Transaction" << it->first <<
					": not garbage collectible; " <<
					transaction->age(now) << " secs old");
			}
		}
	}
	
	void garbageCollect(ev::timer &timer, int revents) {
		time_t now = time(NULL);
		P_DEBUG("Garbage collection time");
		releaseStaleLogSinks(now);
		releaseStaleTransactions(now);
	}
	
	void flushAllLogs(ev::timer &timer, int revents = 0) {
		LogSinkCache::iterator it;
		LogSinkCache::iterator end = logSinkCache.end();
		time_t now = time(NULL);
		
		// Flush log files every second, remote sinks every 10 seconds.
		for (it = logSinkCache.begin(); it != end; it++) {
			LogSink *sink = it->second.get();
			
			if (sink->isRemote()) {
				if (now - sink->lastFlushed >= 10) {
					sink->flush();
				}
			} else {
				sink->flush();
			}
		}
	}
	
	void stopLoop(ev::timer &timer, int revents = 0) {
		ev_unloop(getLoop(), EVUNLOOP_ONE);
	}
	
	void remoteServerCheckThreadMain() {
		vector<string> ips;
		vector<string>::const_iterator it;
		
		P_DEBUG("Initial attempt to resolve Union Station service host names");
		if (unionStationServiceIp.empty()) {
			ips = resolveHostname(UNION_STATION_SERVICE_HOSTNAME,
				unionStationServicePort);
		} else {
			ips.push_back(unionStationServiceIp);
		}
		
		unique_lock<boost::mutex> l(remoteServersLock);
		for (it = ips.begin(); it != ips.end(); it++) {
			RemoteServerPtr server(new RemoteServer(*it, unionStationServicePort));
			remoteServers.push_back(server);
		}
		remoteServersFirstQueried = true;
		remoteServersChanged.notify_all();
		P_DEBUG("Initial attempt to resolve Union Station service host names succeeded: " <<
			toString(ips));
		
		while (!this_thread::interruption_requested()) {
			bool timedOut = false;
			
			while (!this_thread::interruption_requested() && !remoteServerWentDown && !timedOut) {
				timedOut = !remoteServersChanged.timed_wait(l,
					posix_time::seconds(6 * 60 * 60));
			}
			if (this_thread::interruption_requested()) {
				break;
			} else {
				vector<RemoteServerPtr> remoteServers;
				
				l.unlock();
				P_DEBUG("Re-resolving Union Station service host names");
				if (unionStationServiceIp.empty()) {
					ips = resolveHostname(UNION_STATION_SERVICE_HOSTNAME,
						unionStationServicePort);
				} else {
					ips.clear();
					ips.push_back(unionStationServiceIp);
				}
				for (it = ips.begin(); it != ips.end(); it++) {
					RemoteServerPtr server(new RemoteServer(*it, unionStationServicePort));
					P_DEBUG("Pinging Union Station server " << *it);
					if (server->ping()) {
						P_DEBUG("Union Station server " << *it << " up");
						remoteServers.push_back(server);
					} else {
						P_DEBUG("Union Station server " << *it << " down");
					}
				}
				l.lock();
				this->remoteServers = remoteServers;
				if (remoteServerWentDown) {
					remoteServerWentDown = false;
					// We were woken up because a Union Station server went down,
					// so don't recheck for 1 minute.
					l.unlock();
					P_DEBUG("Sleeping for 1 minute before re-checking Union Station servers.");
					syscalls::sleep(60);
					l.lock();
				}
			}
		}
	}
	
protected:
	virtual EventedServer::ClientPtr createClient() {
		return ClientPtr(new Client(this));
	}
	
	virtual bool onMessageReceived(const EventedMessageServer::ClientPtr &eclient, const vector<StaticString> &args) {
		Client *client = static_cast<Client *>(eclient.get());
		
		if (args[0] == "log") {
			if (OXT_UNLIKELY( !expectingArgumentsCount(eclient, args, 3)
			               || !expectingInitialized(eclient) )) {
				return true;
			}
			
			string txnId     = args[1];
			string timestamp = args[2];
			
			TransactionMap::iterator it = transactions.find(txnId);
			if (OXT_UNLIKELY( it == transactions.end() )) {
				writeArrayMessage(eclient, "error",
					"Cannot log data: transaction does not exist",
					NULL);
				disconnect(eclient);
			} else {
				if (!client->stateless) {
					set<string>::iterator sit = client->openTransactions.find(txnId);
					if (OXT_UNLIKELY( sit == client->openTransactions.end() )) {
						writeArrayMessage(eclient, "error",
							"Cannot log data: transaction not opened in this connection",
							NULL);
						disconnect(eclient);
						return true;
					}
				}
				// Expecting the log data in a scalar message.
				client->currentTransaction = it->second;
				client->currentTimestamp = timestamp;
				it->second->lastUsed = time(NULL);
				return false;
			}
			
		} else if (args[0] == "openTransaction") {
			if (OXT_UNLIKELY( !expectingArgumentsCount(eclient, args, 6)
			               || !expectingInitialized(eclient) )) {
				return true;
			}
			
			string       txnId     = args[1];
			StaticString groupName = args[2];
			StaticString category  = args[3];
			StaticString timestamp = args[4];
			StaticString unionStationKey = args[5];
			
			if (OXT_UNLIKELY( !validTxnId(txnId) )) {
				sendErrorToClient(eclient, "Invalid transaction ID format");
				disconnect(eclient);
				return true;
			}
			if (!unionStationKey.empty()
			 && OXT_UNLIKELY( !validUnionStationKey(unionStationKey) )) {
				sendErrorToClient(eclient, "Invalid Union Station key format");
				disconnect(eclient);
				return true;
			}
			if (!client->stateless
			 && OXT_UNLIKELY( client->openTransactions.find(txnId) !=
				client->openTransactions.end() ))
			{
				sendErrorToClient(eclient, "Cannot open transaction: transaction already opened in this connection");
				disconnect(eclient);
				return true;
			}
			
			TransactionPtr transaction = transactions[txnId];
			if (transaction == NULL) {
				if (OXT_UNLIKELY( !supportedCategory(category) )) {
					sendErrorToClient(eclient, "Unsupported category");
					disconnect(eclient);
					return true;
				}
				
				transaction.reset(new Transaction());
				if (unionStationKey.empty()) {
					string filename = determineFilename(groupName, client->nodeId,
						category, txnId);
					if (!openLogFileWithCache(filename, transaction->logSink)) {
						setupGroupAndNodeDir(client, groupName);
					}
				} else {
					openRemoteSink(unionStationKey, client->nodeName,
						category, transaction->logSink);
				}
				transaction->txnId      = txnId;
				transaction->groupName  = groupName;
				transaction->category   = category;
				transaction->writeCount = 0;
				transaction->refcount   = 0;
				transaction->statefulRefcount = 0;
				transactions[txnId]     = transaction;
			} else {
				if (OXT_UNLIKELY( groupName != transaction->groupName )) {
					sendErrorToClient(eclient, "Cannot open transaction: transaction already opened with a different group name");
					disconnect(eclient);
					return true;
				}
				if (OXT_UNLIKELY( category != transaction->category )) {
					sendErrorToClient(eclient, "Cannot open transaction: transaction already opened with a different category name");
					disconnect(eclient);
					return true;
				}
			}
			
			if (!client->stateless) {
				client->openTransactions.insert(txnId);
				transaction->statefulRefcount++;
			}
			transaction->refcount++;
			transaction->lastUsed = time(NULL);
			writeLogEntry(transaction, timestamp, "ATTACH");
			
		} else if (args[0] == "closeTransaction") {
			if (OXT_UNLIKELY( !expectingArgumentsCount(eclient, args, 3)
			               || !expectingInitialized(eclient) )) {
				return true;
			}
			
			string txnId = args[1];
			StaticString timestamp = args[2];
			
			TransactionMap::iterator it = transactions.find(txnId);
			if (OXT_UNLIKELY( it == transactions.end() )) {
				sendErrorToClient(eclient,
					"Cannot close transaction " + txnId +
					": transaction does not exist");
				disconnect(eclient);
			} else {
				TransactionPtr &transaction = it->second;
				
				if (!client->stateless) {
					set<string>::const_iterator sit = client->openTransactions.find(txnId);
					if (OXT_UNLIKELY( sit == client->openTransactions.end() )) {
						sendErrorToClient(eclient,
							"Cannot close transaction " + txnId +
							": transaction not opened in this connection");
						disconnect(eclient);
						return true;
					} else {
						client->openTransactions.erase(sit);
						transaction->statefulRefcount--;
					}
				}
				
				writeDetachEntry(transaction, timestamp);
				transaction->refcount--;
				if (transaction->refcount == 0) {
					transactions.erase(it);
				} else {
					transaction->lastUsed = time(NULL);
				}
			}
		
		} else if (args[0] == "init") {
			if (OXT_UNLIKELY( client->initialized )) {
				sendErrorToClient(eclient, "Already initialized");
				disconnect(eclient);
				return true;
			}
			if (OXT_UNLIKELY( !expectingArgumentsCount(eclient, args, 3) )) {
				return true;
			}
			
			StaticString nodeName = args[1];
			client->nodeName = nodeName;
			client->stateless = args[2] == "true";
			
			md5_state_t state;
			md5_byte_t  digest[MD5_SIZE];
			md5_init(&state);
			md5_append(&state, (const md5_byte_t *) nodeName.data(), nodeName.size());
			md5_finish(&state, digest);
			toHex(StaticString((const char *) digest, MD5_SIZE), client->nodeId);
			
			client->initialized = true;
			
		} else if (args[0] == "flush") {
			flushAllLogs(logFlushingTimer);
			writeArrayMessage(eclient, "ok", NULL);
			
		} else if (args[0] == "info") {
			stringstream stream;
			dump(stream);
			writeArrayMessage(eclient, "info", stream.str().c_str(), NULL);
		
		} else if (args[0] == "exit") {
			if (!requireRights(eclient, Account::EXIT)) {
				disconnect(eclient);
				return true;
			}
			if (args.size() == 2 && args[1] == "immediately") {
				ev_unloop(getLoop(), EVUNLOOP_ONE);
			} else {
				writeArrayMessage(eclient, "Passed security", NULL);
				writeArrayMessage(eclient, "exit command received", NULL);
				// We shut down a few seconds after the last client has exited.
				exitRequested = true;
			}
			
		} else {
			sendErrorToClient(eclient, "Unknown command");
			disconnect(eclient);
		}
		
		return true;
	}
	
	virtual pair<size_t, bool> onOtherDataReceived(const EventedMessageServer::ClientPtr &_client,
		const char *data, size_t size)
	{
		// In here we read the scalar message that's expected to come
		// after the "log" command.
		Client *client = static_cast<Client *>(_client.get());
		size_t consumed = client->dataReader.feed(data, size);
		if (client->dataReader.done()) {
			writeLogEntry(client->currentTransaction,
				client->currentTimestamp,
				client->dataReader.value());
			client->currentTransaction.reset();
			client->dataReader.reset();
			return make_pair(consumed, true);
		} else {
			return make_pair(consumed, false);
		}
	}
	
	virtual void onNewClient(const EventedServer::ClientPtr &client) {
		EventedMessageServer::onNewClient(client);
		if (exitRequested) {
			exitTimer.stop();
		}
	}
	
	virtual void onClientDisconnected(const EventedServer::ClientPtr &_client) {
		EventedMessageServer::onClientDisconnected(_client);
		Client *client = static_cast<Client *>(_client.get());
		set<string>::const_iterator sit;
		set<string>::const_iterator send = client->openTransactions.end();
		
		// Close any transactions that this client had opened. If the client is in
		// stateless mode then this loop does nothing because openTransactions is empty.
		for (sit = client->openTransactions.begin(); sit != send; sit++) {
			const string &txnId = *sit;
			TransactionMap::iterator it = transactions.find(txnId);
			if (OXT_UNLIKELY(it == transactions.end())) {
				P_ERROR("Bug: client->openTransactions is not a subset of this->transactions!");
				abort();
			}
			
			TransactionPtr &transaction = it->second;
			writeDetachEntry(transaction);
			transaction->refcount--;
			if (transaction->refcount == 0) {
				transactions.erase(it);
			} else {
				transaction->statefulRefcount--;
				transaction->lastUsed = time(NULL);
			}
		}
		client->openTransactions.clear();
		
		// Possibly start exit timer.
		if (exitRequested && getClients().empty()) {
			exitTimer.start();
		}
	}

public:
	LoggingServer(struct ev_loop *loop, FileDescriptor fd,
		const AccountsDatabasePtr &accountsDatabase, const string &dir,
		const string &permissions = "u=rwx,g=rx,o=rx", gid_t gid = GROUP_NOT_GIVEN,
		const string &unionStationServiceIp = "",
		unsigned short unionStationServicePort = DEFAULT_UNION_STATION_SERVICE_PORT)
		: EventedMessageServer(loop, fd, accountsDatabase),
		  garbageCollectionTimer(loop),
		  logFlushingTimer(loop),
		  exitTimer(loop)
	{
		this->dir = dir;
		this->gid = gid;
		dirPermissions = permissions;
		filePermissions = parseModeString(permissions) & ~(S_IXUSR | S_IXGRP | S_IXOTH);
		garbageCollectionTimer.set<LoggingServer, &LoggingServer::garbageCollect>(this);
		garbageCollectionTimer.start(60 * 60, 60 * 60);
		logFlushingTimer.set<LoggingServer, &LoggingServer::flushAllLogs>(this);
		logFlushingTimer.start(1, 1);
		exitTimer.set<LoggingServer, &LoggingServer::stopLoop>(this);
		exitTimer.set(5, 0);
		exitRequested = false;
		
		this->unionStationServiceIp   = unionStationServiceIp;
		this->unionStationServicePort = unionStationServicePort;
		remoteServersFirstQueried = false;
		remoteServerWentDown = false;
		remoteServerCheckThread.reset(new oxt::thread(
			boost::bind(&LoggingServer::remoteServerCheckThreadMain, this),
			"Remote server checking thread",
			1024 * 128
		));
	}
	
	~LoggingServer() {
		remoteServerCheckThread->interrupt_and_join();
		
		TransactionMap::const_iterator it, end = transactions.end();
		for (it = transactions.begin(); it != end; it++) {
			writeDetachEntry(it->second);
		}
		transactions.clear();
	}
	
	void dump(stringstream &stream) const {
		TransactionMap::const_iterator it;
		TransactionMap::const_iterator end = transactions.end();
		
		stream << "Number of clients: " << getClients().size() << "\n";
		stream << "Open transactions: " << transactions.size() << "\n";
		for (it = transactions.begin(); it != end; it++) {
			const TransactionPtr &transaction = it->second;
			transaction->dump(stream);
		}
		
		LogSinkCache::const_iterator sit;
		LogSinkCache::const_iterator send = logSinkCache.end();
		stream << "Log sinks: " << logSinkCache.size() << "\n";
		for (sit = logSinkCache.begin(); sit != send; sit++) {
			const LogSinkPtr &logSink = sit->second;
			logSink->dump(stream);
		}
	}
};

typedef shared_ptr<LoggingServer> LoggingServerPtr;


} // namespace Passenger

#endif /* _PASSENGER_LOGGING_SERVER_H_ */
