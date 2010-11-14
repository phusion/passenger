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
#include <ostream>
#include <sstream>
#include <map>
#include <ev++.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <grp.h>
#include <cstring>
#include <ctime>
#include <cassert>

#include "DataStoreId.h"
#include "RemoteSender.h"
#include "ChangeNotifier.h"
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


class LoggingServer: public EventedMessageServer {
private:
	static const int MAX_LOG_SINK_CACHE_SIZE = 512;
	static const int GARBAGE_COLLECTION_TIMEOUT = (int) (1.25 * 60 * 60);  // 1 hour 15 minutes
	
	struct LogSink;
	typedef shared_ptr<LogSink> LogSinkPtr;
	typedef map<string, LogSinkPtr> LogSinkCache;
	
	struct LogSink {
		LoggingServer *server;
		
		/**
		 * Marks how many times this LogSink is currently opened, i.e. the
		 * number of Transaction objects currently referencing this LogSink.
		 * @invariant
		 *    (opened == 0) == (this LogSink is in LoggingServer.inactiveLogSinks)
		 */
		int opened;
		
		/** Last time this LogSink hit an open count of 0. */
		ev_tstamp lastUsed;
		
		/** Last time data was actually written to the underlying storage device. */
		ev_tstamp lastFlushed;
		
		/**
		 * This LogSink's iterator inside LoggingServer.logSinkCache.
		 */
		LogSinkCache::iterator cacheIterator;
		
		/**
		 * This LogSink's iterator inside LoggingServer.inactiveLogSinks.
		 * Only valid when opened == 0.
		 */
		list<LogSinkPtr>::iterator inactiveLogSinksIterator;
		
		LogSink(LoggingServer *_server) {
			server = _server;
			opened = 0;
			lastUsed = ev_now(server->getLoop());
			lastFlushed = 0;
		}
		
		virtual ~LogSink() {
			// We really want to flush() here but can't call virtual
			// functions in destructor. :(
		}
		
		virtual bool isRemote() const {
			return false;
		}
		
		virtual void append(const DataStoreId &dataStoreId,
			const StaticString &data) = 0;
		virtual void flush() { }
		virtual void dump(ostream &stream) const { }
	};
	
	struct LogFile: public LogSink {
		static const unsigned int BUFFER_CAPACITY = 8 * 1024;
		
		string filename;
		FileDescriptor fd;
		char buffer[BUFFER_CAPACITY];
		unsigned int bufferSize;
		
		/**
		 * Contains every (groupName, nodeName, category) tuple for
		 * which their data is currently buffered in this sink.
		 */
		set<DataStoreId> dataStoreIds;
		
		LogFile(LoggingServer *server, const string &filename, mode_t filePermissions)
			: LogSink(server)
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
		
		virtual ~LogFile() {
			flush();
		}
		
		void notifyChanges() {
			if (server->changeNotifier != NULL) {
				set<DataStoreId>::const_iterator it;
				set<DataStoreId>::const_iterator end = dataStoreIds.end();
			
				for (it = dataStoreIds.begin(); it != dataStoreIds.end(); it++) {
					server->changeNotifier->changed(*it);
				}
			}
			dataStoreIds.clear();
		}
		
		virtual void append(const DataStoreId &dataStoreId, const StaticString &data) {
			if (server->changeNotifier != NULL) {
				dataStoreIds.insert(dataStoreId);
			}
			if (bufferSize + data.size() > BUFFER_CAPACITY) {
				StaticString data2[2];
				data2[0] = StaticString(buffer, bufferSize);
				data2[1] = data;
				
				gatheredWrite(fd, data2, 2);
				lastFlushed = ev_now(server->getLoop());
				bufferSize = 0;
				notifyChanges();
			} else {
				memcpy(buffer + bufferSize, data.data(), data.size());
				bufferSize += data.size();
			}
		}
		
		virtual void flush() {
			if (bufferSize > 0) {
				lastFlushed = ev_now(server->getLoop());
				writeExact(fd, buffer, bufferSize);
				bufferSize = 0;
				notifyChanges();
			}
		}
		
		virtual void dump(ostream &stream) const {
			stream << "   Log file: file=" << filename << ", "
				"opened=" << opened << ", "
				"age=" << long(ev_now(server->getLoop()) - lastUsed) << "\n";
		}
	};
	
	typedef shared_ptr<LogFile> LogFilePtr;
	
	struct RemoteSink: public LogSink {
		/* RemoteSender compresses the data with zlib before sending it
		 * to the server. Even including Base64 and URL encoding overhead,
		 * this compresses the data to about 25% of its original size.
		 * Therefore we set a buffer capacity of a little less than 4 times
		 * the TCP maximum segment size so that we can send as much
		 * data as possible to the server in a single TCP segment.
		 * With the "little less" we take into account:
		 * - HTTPS overhead. This can be as high as 2 KB.
		 * - The fact that RemoteSink.append() might try to flush the
		 *   current buffer the current data. Empirical evidence has
		 *   shown that the data for a request transaction is usually
		 *   less than 5 KB.
		 */
		static const unsigned int BUFFER_CAPACITY =
			4 * 64 * 1024 -
			16 * 1024;
		
		string unionStationKey;
		string nodeName;
		string category;
		char buffer[BUFFER_CAPACITY];
		unsigned int bufferSize;
		
		RemoteSink(LoggingServer *server, const string &unionStationKey,
			const string &nodeName, const string &category)
			: LogSink(server)
		{
			this->unionStationKey = unionStationKey;
			this->nodeName = nodeName;
			this->category = category;
			this->bufferSize = 0;
		}
		
		virtual ~RemoteSink() {
			flush();
		}
		
		virtual bool isRemote() const {
			return true;
		}
		
		virtual void append(const DataStoreId &dataStoreId, const StaticString &data) {
			if (bufferSize + data.size() > BUFFER_CAPACITY) {
				StaticString data2[2];
				data2[0] = StaticString(buffer, bufferSize);
				data2[1] = data;
				
				server->remoteSender.schedule(unionStationKey, nodeName,
					category, data2, 2);
				lastFlushed = ev_now(server->getLoop());
				bufferSize = 0;
			} else {
				memcpy(buffer + bufferSize, data.data(), data.size());
				bufferSize += data.size();
			}
		}
		
		virtual void flush() {
			if (bufferSize > 0) {
				lastFlushed = ev_now(server->getLoop());
				StaticString data(buffer, bufferSize);
				server->remoteSender.schedule(unionStationKey, nodeName,
					category, &data, 1);
				bufferSize = 0;
			}
		}
		
		virtual void dump(ostream &stream) const {
			stream << "   Remote sink: "
				"key=" << unionStationKey << ", "
				"node=" << nodeName << ", "
				"category=" << category << ", "
				"opened=" << opened << ", "
				"age=" << long(ev_now(server->getLoop()) - lastUsed) << ", "
				"bufferSize=" << bufferSize <<
				"\n";
		}
	};
	
	struct Transaction {
		LoggingServer *server;
		LogSinkPtr logSink;
		string txnId;
		DataStoreId dataStoreId;
		unsigned int writeCount;
		int refcount;
		bool crashProtect, discarded;
		string data;
		
		Transaction(LoggingServer *server) {
			this->server = server;
			data.reserve(8 * 1024);
		}
		
		~Transaction() {
			if (logSink != NULL) {
				if (!discarded) {
					logSink->append(dataStoreId, data);
				}
				server->closeLogSink(logSink);
			}
		}
		
		StaticString getGroupName() const {
			return dataStoreId.getGroupName();
		}
		
		StaticString getNodeName() const {
			return dataStoreId.getNodeName();
		}
		
		StaticString getCategory() const {
			return dataStoreId.getCategory();
		}
		
		void discard() {
			data.clear();
			discarded = true;
		}
		
		void dump(ostream &stream) const {
			stream << "   Transaction " << txnId << ":\n";
			stream << "      Group   : " << getGroupName() << "\n";
			stream << "      Node    : " << getNodeName() << "\n";
			stream << "      Category: " << getCategory() << "\n";
			stream << "      Refcount: " << refcount << "\n";
		}
	};
	
	typedef shared_ptr<Transaction> TransactionPtr;
	
	enum ClientType {
		UNINITIALIZED,
		LOGGER,
		WATCHER
	};
	
	struct Client: public EventedMessageClient {
		string nodeName;
		ClientType type;
		char nodeId[MD5_HEX_SIZE];
		/**
		 * Set of transaction IDs opened by this client.
		 * @invariant This is a subset of the transaction IDs in the 'transactions' member.
		 */
		set<string> openTransactions;
		ScalarMessage dataReader;
		TransactionPtr currentTransaction;
		string currentTimestamp;
		
		Client(struct ev_loop *loop, const FileDescriptor &fd)
			: EventedMessageClient(loop, fd)
		{
			type = UNINITIALIZED;
			dataReader.setMaxSize(1024 * 128);
		}
	};
	
	typedef shared_ptr<Client> ClientPtr;
	typedef map<string, TransactionPtr> TransactionMap;
	
	string dir;
	gid_t gid;
	string dirPermissions;
	mode_t filePermissions;
	RemoteSender remoteSender;
	ChangeNotifierPtr changeNotifier;
	ev::timer garbageCollectionTimer;
	ev::timer sinkFlushingTimer;
	ev::timer exitTimer;
	TransactionMap transactions;
	LogSinkCache logSinkCache;
	/**
	 * @invariant
	 *    inactiveLogSinks is sorted from oldest to youngest (by lastTime member).
	 *    for all s in inactiveLogSinks:
	 *       s.opened == 0
	 *    inactiveLogSinks.size() == inactiveLogSinksCount
	 */
	list<LogSinkPtr> inactiveLogSinks;
	int inactiveLogSinksCount;
	RandomGenerator randomGenerator;
	bool refuseNewConnections;
	bool exitRequested;
	unsigned long long exitBeginTime;
	
	void sendErrorToClient(Client *client, const string &message) {
		client->writeArrayMessage("error", message.c_str(), NULL);
		logError(client, message);
	}
	
	bool expectingArgumentsCount(Client *client, const vector<StaticString> &args, unsigned int size) {
		if (args.size() == size) {
			return true;
		} else {
			sendErrorToClient(client, "Invalid number of arguments");
			client->disconnect();
			return false;
		}
	}
	
	bool expectingLoggerType(Client *client) {
		if (client->type == LOGGER) {
			return true;
		} else {
			sendErrorToClient(client, "Client not initialized as logger");
			client->disconnect();
			return false;
		}
	}
	
	bool checkWhetherConnectionAreAcceptable(Client *client) {
		if (refuseNewConnections) {
			client->writeArrayMessage("server shutting down", NULL);
			client->disconnect();
			return false;
		} else {
			return true;
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
	
	bool validLogContent(const StaticString &data) const {
		const char *current = data.c_str();
		const char *end = current + data.size();
		while (current < end) {
			char c = *current;
			if ((c < 1 && c > 126) || c == '\n' || c == '\r') {
				return false;
			}
			current++;
		}
		return true;
	}
	
	bool validTimestamp(const StaticString &timestamp) const {
		// must be hexadecimal
		// must not be too large
		return true;
	}
	
	bool supportedCategory(const StaticString &category) const {
		return category == "requests" || category == "processes" || category == "exceptions";
	}
	
	time_t extractTimestamp(const StaticString &txnId) const {
		const char *timestampEnd = (const char *) memchr(txnId.c_str(), '-', txnId.size());
		if (timestampEnd == NULL) {
			return 0;
		} else {
			time_t timestamp = hexatriToULL(
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
		const StaticString &category, const StaticString &txnId = "") const
	{
		time_t timestamp;
		struct tm tm;
		char time_str[14];
		
		if (!txnId.empty()) {
			timestamp = extractTimestamp(txnId);
			gmtime_r(&timestamp, &tm);
			strftime(time_str, sizeof(time_str), "%Y/%m/%d/%H", &tm);
		}
		
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
		if (!txnId.empty()) {
			filename.append(1, '/');
			filename.append(time_str);
			filename.append("/log.txt");
		}
		return filename;
	}
	
	void setupGroupAndNodeDir(const StaticString &groupName, const StaticString &nodeName,
		const char *nodeId)
	{
		string filename, groupDir, nodeDir;
		
		filename.append(dir);
		appendVersionAndGroupId(filename, groupName);
		groupDir = filename;
		
		filename.append("/");
		filename.append(nodeId, MD5_HEX_SIZE);
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
		
		createFile(nodeDir + "/node_name.txt", nodeName,
			filePermissions, USER_NOT_GIVEN, GROUP_NOT_GIVEN,
			false);
		if (getFileType(nodeDir + "/uuid.txt") == FT_NONEXISTANT) {
			createFile(nodeDir + "/uuid.txt",
				randomGenerator.generateAsciiString(24),
				filePermissions, USER_NOT_GIVEN, GROUP_NOT_GIVEN,
				false);
		}
	}
	
	bool openLogFileWithCache(const string &filename, LogSinkPtr &theLogSink) {
		string cacheKey = "file:" + filename;
		LogSinkCache::iterator it = logSinkCache.find(cacheKey);
		if (it == logSinkCache.end()) {
			trimLogSinkCache(MAX_LOG_SINK_CACHE_SIZE - 1);
			makeDirTree(extractDirName(filename), dirPermissions,
				USER_NOT_GIVEN, gid);
			theLogSink.reset(new LogFile(this, filename, filePermissions));
			pair<LogSinkCache::iterator, bool> p =
				logSinkCache.insert(make_pair(cacheKey, theLogSink));
			theLogSink->cacheIterator = p.first;
			theLogSink->opened = 1;
			return false;
		} else {
			theLogSink = it->second;
			theLogSink->opened++;
			if (theLogSink->opened == 1) {
				inactiveLogSinks.erase(theLogSink->inactiveLogSinksIterator);
				inactiveLogSinksCount--;
			}
			return true;
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
			trimLogSinkCache(MAX_LOG_SINK_CACHE_SIZE - 1);
			theLogSink.reset(new RemoteSink(this, unionStationKey,
				nodeName, category));
			pair<LogSinkCache::iterator, bool> p =
				logSinkCache.insert(make_pair(cacheKey, theLogSink));
			theLogSink->cacheIterator = p.first;
			theLogSink->opened = 1;
		} else {
			theLogSink = it->second;
			theLogSink->opened++;
			if (theLogSink->opened == 1) {
				inactiveLogSinks.erase(theLogSink->inactiveLogSinksIterator);
				inactiveLogSinksCount--;
			}
		}
	}
	
	/**
	 * 'Closes' the given log sink. It's not actually deleted from memory;
	 * instead it's marked as inactive and cached for later use. May be
	 * deleted later when resources are low.
	 *
	 * No need to call this manually. Automatically called by Transaction's
	 * destructor.
	 */
	void closeLogSink(const LogSinkPtr &logSink) {
		logSink->opened--;
		assert(logSink->opened >= 0);
		logSink->lastUsed = ev_now(getLoop());
		if (logSink->opened == 0) {
			inactiveLogSinks.push_back(logSink);
			logSink->inactiveLogSinksIterator = inactiveLogSinks.end();
			logSink->inactiveLogSinksIterator--;
			inactiveLogSinksCount++;
			trimLogSinkCache(MAX_LOG_SINK_CACHE_SIZE);
		}
	}
	
	/** Try to reduce the log sink cache size to the given size. */
	void trimLogSinkCache(unsigned int size) {
		while (!inactiveLogSinks.empty() && logSinkCache.size() > size) {
			const LogSinkPtr logSink = inactiveLogSinks.front();
			inactiveLogSinks.pop_front();
			inactiveLogSinksCount--;
			logSinkCache.erase(logSink->cacheIterator);
		}
	}
	
	bool writeLogEntry(Client *client, const TransactionPtr &transaction,
		const StaticString &timestamp, const StaticString &data)
	{
		if (transaction->discarded) {
			return true;
		}
		if (OXT_UNLIKELY( !validLogContent(data) )) {
			if (client != NULL) {
				sendErrorToClient(client, "Log entry data contains an invalid character.");
				client->disconnect();
			}
			return false;
		}
		if (OXT_UNLIKELY( !validTimestamp(timestamp) )) {
			if (client != NULL) {
				sendErrorToClient(client, "Log entry timestamp is invalid.");
				client->disconnect();
			}
			return false;
		}
		
		char writeCountStr[sizeof(unsigned int) * 2 + 1];
		integerToHexatri(transaction->writeCount, writeCountStr);
		transaction->writeCount++;
		transaction->data.reserve(transaction->data.size() +
			transaction->txnId.size() +
			1 +
			timestamp.size() +
			1 +
			strlen(writeCountStr) +
			1 +
			data.size() +
			1);
		transaction->data.append(transaction->txnId);
		transaction->data.append(" ");
		transaction->data.append(timestamp);
		transaction->data.append(" ");
		transaction->data.append(writeCountStr);
		transaction->data.append(" ");
		transaction->data.append(data);
		transaction->data.append("\n");
		return true;
	}
	
	void writeDetachEntry(Client *client, const TransactionPtr &transaction) {
		char timestamp[2 * sizeof(unsigned long long) + 1];
		// Must use System::getUsec() here instead of ev_now() because the
		// precision of the time is very important.
		integerToHexatri<unsigned long long>(SystemTime::getUsec(), timestamp);
		writeDetachEntry(client, transaction, timestamp);
	}
	
	void writeDetachEntry(Client *client, const TransactionPtr &transaction,
		const StaticString &timestamp)
	{
		writeLogEntry(client, transaction, timestamp, "DETACH");
	}
	
	bool requireRights(Client *client, Account::Rights rights) {
		if (client->messageServer.account->hasRights(rights)) {
			return true;
		} else {
			P_TRACE(2, "Security error: insufficient rights to execute this command.");
			client->writeArrayMessage("SecurityException",
				"Insufficient rights to execute this command.",
				NULL);
			client->disconnect();
			return false;
		}
	}
	
	bool isDirectory(const string &dir, struct dirent *entry) const {
		#ifdef __sun__
			string path = dir;
			path.append("/");
			path.append(entry->d_name);
			return getFileType(path) == FT_DIRECTORY;
		#else
			return entry->d_type == DT_DIR;
		#endif
	}
	
	bool looksLikeNumber(const char *str) const {
		const char *current = str;
		while (*current != '\0') {
			char c = *current;
			if (!(c >= '0' && c <= '9')) {
				return false;
			}
			current++;
		}
		return true;
	}
	
	bool getLastEntryInDirectory(const string &path, string &result) const {
		DIR *dir = opendir(path.c_str());
		struct dirent *entry;
		vector<string> subdirs;
		
		if (dir == NULL) {
			int e = errno;
			throw FileSystemException("Cannot open directory " + path,
				e, path);
		}
		while ((entry = readdir(dir)) != NULL) {
			if (isDirectory(path, entry) && looksLikeNumber(entry->d_name)) {
				subdirs.push_back(entry->d_name);
			}
		}
		closedir(dir);
		
		if (subdirs.empty()) {
			return false;
		}
		
		vector<string>::const_iterator it = subdirs.begin();
		vector<string>::const_iterator end = subdirs.end();
		vector<string>::const_iterator largest_it = subdirs.begin();
		int largest = atoi(subdirs[0]);
		for (it++; it != end; it++) {
			const string &subdir = *it;
			int number = atoi(subdir.c_str());
			if (number > largest) {
				largest_it = it;
				largest = number;
			}
		}
		result = *largest_it;
		return true;
	}
	
	static void pendingDataFlushed(EventedClient *_client) {
		Client *client = (Client *) _client;
		LoggingServer *self = (LoggingServer *) client->userData;
		
		client->onPendingDataFlushed = NULL;
		if (OXT_UNLIKELY( client->type != WATCHER )) {
			P_WARN("BUG: pendingDataFlushed() called even though client type is not WATCHER.");
			client->disconnect();
		} else if (self->changeNotifier != NULL) {
			self->changeNotifier->addClient(client->detach());
		} else {
			client->disconnect();
		}
	}
	
	/* Release all inactive log sinks that have been inactive for more than
	 * GARBAGE_COLLECTION_TIMEOUT seconds.
	 */
	void releaseInactiveLogSinks(ev_tstamp now) {
		bool done = false;
		
		while (!done && !inactiveLogSinks.empty()) {
			const LogSinkPtr logSink = inactiveLogSinks.front();
			if (now - logSink->lastUsed >= GARBAGE_COLLECTION_TIMEOUT) {
				inactiveLogSinks.pop_front();
				inactiveLogSinksCount--;
				logSinkCache.erase(logSink->cacheIterator);
			} else {
				done = true;
			}
		}
	}
	
	void garbageCollect(ev::timer &timer, int revents) {
		P_DEBUG("Garbage collection time");
		releaseInactiveLogSinks(ev_now(getLoop()));
	}
	
	void sinkFlushTimeout(ev::timer &timer, int revents) {
		P_TRACE(2, "Flushing all sinks (periodic action)");
		LogSinkCache::iterator it;
		LogSinkCache::iterator end = logSinkCache.end();
		ev_tstamp now = ev_now(getLoop());
		
		for (it = logSinkCache.begin(); it != end; it++) {
			LogSink *sink = it->second.get();
			
			// Flush log file sinks every 15 seconds,
			// remote sinks every 60 seconds.
			if (sink->isRemote()) {
				if (now - sink->lastFlushed >= 60) {
					sink->flush();
				}
			} else {
				sink->flush();
			}
		}
	}
	
	void flushAllSinks() {
		P_TRACE(2, "Flushing all sinks");
		LogSinkCache::iterator it;
		LogSinkCache::iterator end = logSinkCache.end();
		
		for (it = logSinkCache.begin(); it != end; it++) {
			LogSink *sink = it->second.get();
			sink->flush();
		}
	}
	
	void exitTimerTimeout(ev::timer &timer, int revents) {
		if (SystemTime::getMsec() >= exitBeginTime + 5000) {
			exitTimer.stop();
			exitRequested = false;
			refuseNewConnections = false;
			ev_unloop(getLoop(), EVUNLOOP_ONE);
		}
	}
	
protected:
	virtual EventedClient *createClient(const FileDescriptor &fd) {
		return new Client(getLoop(), fd);
	}
	
	virtual bool onMessageReceived(EventedMessageClient *_client, const vector<StaticString> &args) {
		Client *client = (Client *) _client;
		
		if (args[0] == "log") {
			if (OXT_UNLIKELY( !expectingArgumentsCount(client, args, 3)
			               || !expectingLoggerType(client) )) {
				return true;
			}
			
			string txnId     = args[1];
			string timestamp = args[2];
			
			TransactionMap::iterator it = transactions.find(txnId);
			if (OXT_UNLIKELY( it == transactions.end() )) {
				sendErrorToClient(client, "Cannot log data: transaction does not exist");
				client->disconnect();
			} else {
				set<string>::iterator sit = client->openTransactions.find(txnId);
				if (OXT_UNLIKELY( sit == client->openTransactions.end() )) {
					sendErrorToClient(client,
						"Cannot log data: transaction not opened in this connection");
					client->disconnect();
					return true;
				}
				// Expecting the log data in a scalar message.
				client->currentTransaction = it->second;
				client->currentTimestamp = timestamp;
				return false;
			}
			
		} else if (args[0] == "openTransaction") {
			if (OXT_UNLIKELY( !expectingArgumentsCount(client, args, 8)
			               || !expectingLoggerType(client) )) {
				return true;
			}
			
			string       txnId     = args[1];
			StaticString groupName = args[2];
			StaticString nodeName  = args[3];
			StaticString category  = args[4];
			StaticString timestamp = args[5];
			StaticString unionStationKey = args[6];
			bool         crashProtect    = args[7] == "true";
			
			if (OXT_UNLIKELY( !validTxnId(txnId) )) {
				sendErrorToClient(client, "Invalid transaction ID format");
				client->disconnect();
				return true;
			}
			if (!unionStationKey.empty()
			 && OXT_UNLIKELY( !validUnionStationKey(unionStationKey) )) {
				sendErrorToClient(client, "Invalid Union Station key format");
				client->disconnect();
				return true;
			}
			if (OXT_UNLIKELY( client->openTransactions.find(txnId) !=
				client->openTransactions.end() ))
			{
				sendErrorToClient(client, "Cannot open transaction: transaction already opened in this connection");
				client->disconnect();
				return true;
			}
			
			const char *nodeId;
			
			if (nodeName.empty()) {
				nodeName = client->nodeName;
				nodeId = client->nodeId;
			} else {
				nodeId = NULL;
			}
			
			TransactionMap::iterator it = transactions.find(txnId);
			TransactionPtr transaction;
			if (it == transactions.end()) {
				if (OXT_UNLIKELY( !supportedCategory(category) )) {
					sendErrorToClient(client, "Unsupported category");
					client->disconnect();
					return true;
				}
				
				transaction.reset(new Transaction(this));
				if (unionStationKey.empty()) {
					char tempNodeId[MD5_HEX_SIZE];
					
					if (nodeId == NULL) {
						md5_state_t state;
						md5_byte_t  digest[MD5_SIZE];

						md5_init(&state);
						md5_append(&state,
							(const md5_byte_t *) nodeName.data(),
							nodeName.size());
						md5_finish(&state, digest);
						toHex(StaticString((const char *) digest, MD5_SIZE),
							tempNodeId);
						nodeId = tempNodeId;
					}
					
					string filename = determineFilename(groupName, nodeId,
						category, txnId);
					if (!openLogFileWithCache(filename, transaction->logSink)) {
						setupGroupAndNodeDir(groupName, nodeName, nodeId);
					}
				} else {
					openRemoteSink(unionStationKey, client->nodeName,
						category, transaction->logSink);
				}
				transaction->txnId        = txnId;
				transaction->dataStoreId  = DataStoreId(groupName,
					nodeName, category);
				transaction->writeCount   = 0;
				transaction->refcount     = 0;
				transaction->crashProtect = crashProtect;
				transaction->discarded    = false;
				transactions.insert(make_pair(txnId, transaction));
			} else {
				transaction = it->second;
				if (OXT_UNLIKELY( transaction->getGroupName() != groupName )) {
					sendErrorToClient(client,
						"Cannot open transaction: transaction already opened with a different group name");
					client->disconnect();
					return true;
				}
				if (OXT_UNLIKELY( transaction->getNodeName() != nodeName )) {
					sendErrorToClient(client,
						"Cannot open transaction: transaction already opened with a different node name");
					client->disconnect();
					return true;
				}
				if (OXT_UNLIKELY( transaction->getCategory() != category )) {
					sendErrorToClient(client,
						"Cannot open transaction: transaction already opened with a different category name");
					client->disconnect();
					return true;
				}
			}
			
			client->openTransactions.insert(txnId);
			transaction->refcount++;
			writeLogEntry(client, transaction, timestamp, "ATTACH");
			
		} else if (args[0] == "closeTransaction") {
			if (OXT_UNLIKELY( !expectingArgumentsCount(client, args, 3)
			               || !expectingLoggerType(client) )) {
				return true;
			}
			
			string txnId = args[1];
			StaticString timestamp = args[2];
			
			TransactionMap::iterator it = transactions.find(txnId);
			if (OXT_UNLIKELY( it == transactions.end() )) {
				sendErrorToClient(client,
					"Cannot close transaction " + txnId +
					": transaction does not exist");
				client->disconnect();
			} else {
				TransactionPtr &transaction = it->second;
				
				set<string>::const_iterator sit = client->openTransactions.find(txnId);
				if (OXT_UNLIKELY( sit == client->openTransactions.end() )) {
					sendErrorToClient(client,
						"Cannot close transaction " + txnId +
						": transaction not opened in this connection");
					client->disconnect();
					return true;
				} else {
					client->openTransactions.erase(sit);
				}
				
				writeDetachEntry(client, transaction, timestamp);
				transaction->refcount--;
				assert(transaction->refcount >= 0);
				if (transaction->refcount == 0) {
					transactions.erase(it);
				}
			}
		
		} else if (args[0] == "init") {
			if (OXT_UNLIKELY( client->type != UNINITIALIZED )) {
				sendErrorToClient(client, "Already initialized");
				client->disconnect();
				return true;
			}
			if (OXT_UNLIKELY( !expectingArgumentsCount(client, args, 2) )) {
				return true;
			}
			if (OXT_UNLIKELY( !checkWhetherConnectionAreAcceptable(client) )) {
				return true;
			}
			
			StaticString nodeName = args[1];
			client->nodeName = nodeName;
			
			md5_state_t state;
			md5_byte_t  digest[MD5_SIZE];
			md5_init(&state);
			md5_append(&state, (const md5_byte_t *) nodeName.data(), nodeName.size());
			md5_finish(&state, digest);
			toHex(StaticString((const char *) digest, MD5_SIZE), client->nodeId);
			
			client->type = LOGGER;
			client->writeArrayMessage("ok", NULL);
			
		} else if (args[0] == "watchChanges") {
			if (OXT_UNLIKELY( !checkWhetherConnectionAreAcceptable(client) )) {
				return true;
			}
			if (OXT_UNLIKELY( client->type != UNINITIALIZED )) {
				sendErrorToClient(client, "This command cannot be invoked "
					"if the 'init' command is already invoked.");
				client->disconnect();
				return true;
			}
			
			client->type = WATCHER;
			client->notifyReads(false);
			discardReadData();
			
			// Add to the change notifier after all pending data
			// has been written out.
			client->onPendingDataFlushed = pendingDataFlushed;
			client->writeArrayMessage("ok", NULL);
			
		} else if (args[0] == "flush") {
			flushAllSinks();
			client->writeArrayMessage("ok", NULL);
			
		} else if (args[0] == "info") {
			stringstream stream;
			dump(stream);
			client->writeArrayMessage("info", stream.str().c_str(), NULL);
		
		} else if (args[0] == "ping") {
			client->writeArrayMessage("pong", NULL);
		
		} else if (args[0] == "exit") {
			if (!requireRights(client, Account::EXIT)) {
				client->disconnect();
				return true;
			}
			if (args.size() == 2 && args[1] == "immediately") {
				// Immediate exit.
				ev_unloop(getLoop(), EVUNLOOP_ONE);
			} else if (args.size() == 2 && args[1] == "semi-gracefully") {
				// Semi-graceful exit: refuse new connections, shut down
				// a few seconds after the last client has disconnected.
				refuseNewConnections = true;
				exitRequested = true;
			} else {
				// Graceful exit: shut down a few seconds after the
				// last client has disconnected.
				client->writeArrayMessage("Passed security", NULL);
				client->writeArrayMessage("exit command received", NULL);
				exitRequested = true;
			}
			client->disconnect();
			
		} else {
			sendErrorToClient(client, "Unknown command '" + args[0] + "'");
			client->disconnect();
		}
		
		return true;
	}
	
	virtual pair<size_t, bool> onOtherDataReceived(EventedMessageClient *_client,
		const char *data, size_t size)
	{
		// In here we read the scalar message that's expected to come
		// after the "log" command.
		Client *client = (Client *) _client;
		size_t consumed = client->dataReader.feed(data, size);
		if (client->dataReader.done()) {
			writeLogEntry(client,
				client->currentTransaction,
				client->currentTimestamp,
				client->dataReader.value());
			client->currentTransaction.reset();
			client->dataReader.reset();
			return make_pair(consumed, true);
		} else {
			return make_pair(consumed, false);
		}
	}
	
	virtual void onNewClient(EventedClient *client) {
		if (exitRequested && exitTimer.is_active()) {
			exitTimer.stop();
		}
		EventedMessageServer::onNewClient(client);
	}
	
	virtual void onClientDisconnected(EventedClient *_client) {
		EventedMessageServer::onClientDisconnected(_client);
		Client *client = (Client *) _client;
		set<string>::const_iterator sit;
		set<string>::const_iterator send = client->openTransactions.end();
		
		// Close any transactions that this client had opened.
		for (sit = client->openTransactions.begin(); sit != send; sit++) {
			const string &txnId = *sit;
			TransactionMap::iterator it = transactions.find(txnId);
			if (OXT_UNLIKELY( it == transactions.end() )) {
				P_ERROR("Bug: client->openTransactions is not a subset of this->transactions!");
				abort();
			}
			
			TransactionPtr &transaction = it->second;
			if (transaction->crashProtect) {
				writeDetachEntry(client, transaction);
			} else {
				transaction->discard();
			}
			transaction->refcount--;
			assert(transaction->refcount >= 0);
			if (transaction->refcount == 0) {
				transactions.erase(it);
			}
		}
		client->openTransactions.clear();
		
		// Possibly start exit timer.
		if (exitRequested && getClients().empty()) {
			exitTimer.start();
			/* Using SystemTime here instead of setting a correct
			 * timeout directly on the timer, so that we can
			 * manipulate the clock in LoggingServer unit tests.
			 */
			exitBeginTime = SystemTime::getMsec();
		}
	}

public:
	LoggingServer(struct ev_loop *loop,
		FileDescriptor fd,
		const AccountsDatabasePtr &accountsDatabase,
		const string &dir,
		const string &permissions = DEFAULT_ANALYTICS_LOG_PERMISSIONS,
		gid_t gid = GROUP_NOT_GIVEN,
		const string &unionStationGatewayAddress = DEFAULT_UNION_STATION_GATEWAY_ADDRESS,
		unsigned short unionStationGatewayPort = DEFAULT_UNION_STATION_GATEWAY_PORT,
		const string &unionStationGatewayCert = "")
		: EventedMessageServer(loop, fd, accountsDatabase),
		  remoteSender(unionStationGatewayAddress,
		               unionStationGatewayPort,
		               unionStationGatewayCert),
		  garbageCollectionTimer(loop),
		  sinkFlushingTimer(loop),
		  exitTimer(loop)
	{
		this->dir = dir;
		this->gid = gid;
		dirPermissions = permissions;
		filePermissions = parseModeString(permissions) & ~(S_IXUSR | S_IXGRP | S_IXOTH);
		garbageCollectionTimer.set<LoggingServer, &LoggingServer::garbageCollect>(this);
		garbageCollectionTimer.start(GARBAGE_COLLECTION_TIMEOUT, GARBAGE_COLLECTION_TIMEOUT);
		sinkFlushingTimer.set<LoggingServer, &LoggingServer::sinkFlushTimeout>(this);
		sinkFlushingTimer.start(15, 15);
		exitTimer.set<LoggingServer, &LoggingServer::exitTimerTimeout>(this);
		exitTimer.set(0.05, 0.05);
		refuseNewConnections = false;
		exitRequested = false;
		inactiveLogSinksCount = 0;
	}
	
	~LoggingServer() {
		TransactionMap::iterator it, end = transactions.end();
		for (it = transactions.begin(); it != end; it++) {
			TransactionPtr &transaction = it->second;
			if (transaction->crashProtect) {
				writeDetachEntry(NULL, transaction);
			} else {
				transaction->discard();
			}
		}
		
		// Invoke destructors, causing all transactions and log sinks to
		// be flushed before RemoteSender and ChangeNotifier are being
		// destroyed.
		transactions.clear();
		logSinkCache.clear();
		inactiveLogSinks.clear();
	}
	
	void setChangeNotifier(const ChangeNotifierPtr &_changeNotifier) {
		changeNotifier = _changeNotifier;
		changeNotifier->getLastPos = boost::bind(&LoggingServer::getLastPos,
			this, _1, _2, _3);
	}
	
	string getLastPos(const StaticString &groupName, const StaticString &nodeName,
		const StaticString &category) const
	{
		md5_state_t state;
		md5_byte_t  digest[MD5_SIZE];
		char        nodeId[MD5_HEX_SIZE];
		md5_init(&state);
		md5_append(&state, (const md5_byte_t *) nodeName.data(), nodeName.size());
		md5_finish(&state, digest);
		toHex(StaticString((const char *) digest, MD5_SIZE), nodeId);
		
		string dir = determineFilename(groupName, nodeId, category);
		string subdir, component;
		subdir.reserve(13); // It's a string that looks like: "2010/06/24/12"
		
		try {
			// Loop 4 times to process year, month, day, hour.
			for (int i = 0; i < 4; i++) {
				bool found = getLastEntryInDirectory(dir, component);
				if (!found) {
					return string();
				}
				dir.append("/");
				dir.append(component);
				if (i != 0) {
					subdir.append("/");
				}
				subdir.append(component);
			}
			// After the loop, new dir == old dir + "/" + subdir
		} catch (const SystemException &e) {
			if (e.code() == ENOENT) {
				return string();
			} else {
				throw;
			}
		}
		
		string &filename = dir;
		filename.append("/log.txt");
		
		struct stat buf;
		if (stat(filename.c_str(), &buf) == -1) {
			if (errno == ENOENT) {
				return string();
			} else {
				int e = errno;
				throw FileSystemException("Cannot stat() " + filename, e,
					filename);
			}
		} else {
			return subdir + "/" + toString(buf.st_size);
		}
	}
	
	void dump(ostream &stream) const {
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
		stream << "Log sinks: " << logSinkCache.size() <<
			" (" << inactiveLogSinksCount << " inactive)\n";
		for (sit = logSinkCache.begin(); sit != send; sit++) {
			const LogSinkPtr &logSink = sit->second;
			logSink->dump(stream);
		}
	}
};

typedef shared_ptr<LoggingServer> LoggingServerPtr;


} // namespace Passenger

#endif /* _PASSENGER_LOGGING_SERVER_H_ */
