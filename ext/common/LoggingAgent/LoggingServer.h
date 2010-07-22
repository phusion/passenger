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

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <grp.h>
#include <cstring>
#include <ctime>

#include "RemoteSender.h"
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
	
	struct LogSink {
		time_t lastUsed;
		time_t lastFlushed;
		
		LogSink() {
			lastUsed = time(NULL);
			lastFlushed = 0;
		}
		
		virtual ~LogSink() {
			// We really want to flush() here but can't call virtual
			// functions in destructor. :(
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
		
		virtual ~LogFile() {
			flush();
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
		
		LoggingServer *server;
		string unionStationKey;
		string nodeName;
		string category;
		char buffer[BUFFER_CAPACITY];
		unsigned int bufferSize;
		
		RemoteSink(LoggingServer *server, const string &unionStationKey,
			const string &nodeName, const string &category)
		{
			this->server = server;
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
				server->remoteSender.schedule(unionStationKey, nodeName,
					category, data2, count + 1);
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
				server->remoteSender.schedule(unionStationKey, nodeName,
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
		bool crashProtect, discarded;
		string data;
		
		Transaction() {
			data.reserve(8 * 1024);
		}
		
		~Transaction() {
			if (!discarded) {
				StaticString data = this->data;
				logSink->append(&data, 1);
			}
		}
		
		void discard() {
			data.clear();
			discarded = true;
		}
		
		void dump(stringstream &stream) const {
			stream << "   Transaction " << txnId << ":\n";
			stream << "      Group: " << groupName << "\n";
			stream << "      Category: " << category << "\n";
			stream << "      Refcount: " << refcount << "\n";
		}
	};
	
	typedef shared_ptr<Transaction> TransactionPtr;
	typedef map<string, LogSinkPtr> LogSinkCache;
	
	struct Client: public EventedMessageServer::Client {
		string nodeName;
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
	RemoteSender remoteSender;
	ev::timer garbageCollectionTimer;
	ev::timer logFlushingTimer;
	ev::timer exitTimer;
	TransactionMap transactions;
	LogSinkCache logSinkCache;
	RandomGenerator randomGenerator;
	bool exitRequested;
	
	void sendErrorToClient(const EventedServer::ClientPtr &client, const string &message) {
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
			trimLogSinkCache(MAX_LOG_SINK_CACHE_SIZE - 1);
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
	
	void trimLogSinkCache(unsigned int size) {
		while (logSinkCache.size() > size) {
			LogSinkCache::iterator it = logSinkCache.begin();
			LogSinkCache::iterator end = logSinkCache.end();
			LogSinkCache::iterator smallest_it = it;
			
			// Find least recently used log sink and remove it.
			for (it++; it != end; it++) {
				if (it->second->lastUsed < smallest_it->second->lastUsed) {
					smallest_it = it;
				}
			}
			logSinkCache.erase(smallest_it);
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
	
	bool writeLogEntry(const EventedServer::ClientPtr &eclient,
		const TransactionPtr &transaction, const StaticString &timestamp,
		const StaticString &data)
	{
		if (OXT_UNLIKELY( !validLogContent(data) )) {
			if (eclient != NULL) {
				sendErrorToClient(eclient, "Log entry data contains an invalid character.");
				disconnect(eclient);
			}
			return false;
		}
		if (OXT_UNLIKELY( !validTimestamp(timestamp) )) {
			if (eclient != NULL) {
				sendErrorToClient(eclient, "Log entry timestamp is invalid.");
				disconnect(eclient);
			}
			return false;
		}
		
		char writeCountStr[sizeof(unsigned int) * 2 + 1];
		integerToHex(transaction->writeCount, writeCountStr);
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
	
	void writeDetachEntry(const EventedServer::ClientPtr &eclient,
		const TransactionPtr &transaction)
	{
		char timestamp[2 * sizeof(unsigned long long) + 1];
		integerToHex<unsigned long long>(SystemTime::getUsec(), timestamp);
		writeDetachEntry(eclient, transaction, timestamp);
	}
	
	void writeDetachEntry(const EventedServer::ClientPtr &eclient,
		const TransactionPtr &transaction, const StaticString &timestamp)
	{
		writeLogEntry(eclient, transaction, timestamp, "DETACH");
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
	
	void garbageCollect(ev::timer &timer, int revents) {
		time_t now = time(NULL);
		P_DEBUG("Garbage collection time");
		releaseStaleLogSinks(now);
	}
	
	void flushAllLogs(ev::timer &timer, int revents = 0) {
		LogSinkCache::iterator it;
		LogSinkCache::iterator end = logSinkCache.end();
		time_t now = time(NULL);
		
		// Flush log files every 5 seconds, remote sinks every 30 seconds.
		for (it = logSinkCache.begin(); it != end; it++) {
			LogSink *sink = it->second.get();
			
			if (sink->isRemote()) {
				if (now - sink->lastFlushed >= 30) {
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
				set<string>::iterator sit = client->openTransactions.find(txnId);
				if (OXT_UNLIKELY( sit == client->openTransactions.end() )) {
					writeArrayMessage(eclient, "error",
						"Cannot log data: transaction not opened in this connection",
						NULL);
					disconnect(eclient);
					return true;
				}
				// Expecting the log data in a scalar message.
				client->currentTransaction = it->second;
				client->currentTimestamp = timestamp;
				return false;
			}
			
		} else if (args[0] == "openTransaction") {
			if (OXT_UNLIKELY( !expectingArgumentsCount(eclient, args, 7)
			               || !expectingInitialized(eclient) )) {
				return true;
			}
			
			string       txnId     = args[1];
			StaticString groupName = args[2];
			StaticString category  = args[3];
			StaticString timestamp = args[4];
			StaticString unionStationKey = args[5];
			bool         crashProtect    = args[6] == "true";
			
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
			if (OXT_UNLIKELY( client->openTransactions.find(txnId) !=
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
				transaction->txnId        = txnId;
				transaction->groupName    = groupName;
				transaction->category     = category;
				transaction->writeCount   = 0;
				transaction->refcount     = 0;
				transaction->crashProtect = crashProtect;
				transaction->discarded    = false;
				transactions[txnId]       = transaction;
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
			
			client->openTransactions.insert(txnId);
			transaction->refcount++;
			writeLogEntry(eclient, transaction, timestamp, "ATTACH");
			
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
				
				set<string>::const_iterator sit = client->openTransactions.find(txnId);
				if (OXT_UNLIKELY( sit == client->openTransactions.end() )) {
					sendErrorToClient(eclient,
						"Cannot close transaction " + txnId +
						": transaction not opened in this connection");
					disconnect(eclient);
					return true;
				} else {
					client->openTransactions.erase(sit);
				}
				
				writeDetachEntry(eclient, transaction, timestamp);
				transaction->refcount--;
				if (transaction->refcount == 0) {
					transactions.erase(it);
				}
			}
		
		} else if (args[0] == "init") {
			if (OXT_UNLIKELY( client->initialized )) {
				sendErrorToClient(eclient, "Already initialized");
				disconnect(eclient);
				return true;
			}
			if (OXT_UNLIKELY( !expectingArgumentsCount(eclient, args, 2) )) {
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
			sendErrorToClient(eclient, "Unknown command '" + args[0] + "'");
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
			writeLogEntry(_client,
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
		
		// Close any transactions that this client had opened.
		for (sit = client->openTransactions.begin(); sit != send; sit++) {
			const string &txnId = *sit;
			TransactionMap::iterator it = transactions.find(txnId);
			if (OXT_UNLIKELY(it == transactions.end())) {
				P_ERROR("Bug: client->openTransactions is not a subset of this->transactions!");
				abort();
			}
			
			TransactionPtr &transaction = it->second;
			if (transaction->crashProtect) {
				writeDetachEntry(_client, transaction);
			} else {
				transaction->discard();
			}
			transaction->refcount--;
			if (transaction->refcount == 0) {
				transactions.erase(it);
			}
		}
		client->openTransactions.clear();
		
		// Possibly start exit timer.
		if (exitRequested && getClients().empty()) {
			exitTimer.start();
		}
	}

public:
	LoggingServer(struct ev_loop *loop,
		FileDescriptor fd,
		const AccountsDatabasePtr &accountsDatabase,
		const string &dir,
		const string &permissions = DEFAULT_ANALYTICS_LOG_PERMISSIONS,
		gid_t gid = GROUP_NOT_GIVEN,
		const string &unionStationServiceAddress = DEFAULT_UNION_STATION_SERVICE_ADDRESS,
		unsigned short unionStationServicePort = DEFAULT_UNION_STATION_SERVICE_PORT,
		const string &unionStationServiceCert = "")
		: EventedMessageServer(loop, fd, accountsDatabase),
		  remoteSender(unionStationServiceAddress,
		               unionStationServicePort,
		               unionStationServiceCert),
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
		logFlushingTimer.start(5, 5);
		exitTimer.set<LoggingServer, &LoggingServer::stopLoop>(this);
		exitTimer.set(5, 0);
		exitRequested = false;
	}
	
	~LoggingServer() {
		TransactionMap::iterator it, end = transactions.end();
		for (it = transactions.begin(); it != end; it++) {
			TransactionPtr &transaction = it->second;
			if (transaction->crashProtect) {
				writeDetachEntry(EventedServer::ClientPtr(), transaction);
			} else {
				transaction->discard();
			}
		}
		
		// Invoke destructors, causing all transactions and log sinks to
		// be flushed before RemoteSender is being destroyed.
		transactions.clear();
		logSinkCache.clear();
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
