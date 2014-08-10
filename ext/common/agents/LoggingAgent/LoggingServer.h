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

#include <agents/LoggingAgent/DataStoreId.h>
#include <agents/LoggingAgent/RemoteSender.h>
#include <agents/LoggingAgent/FilterSupport.h>

#include <EventedMessageServer.h>
#include <MessageReadersWriters.h>
#include <RandomGenerator.h>
#include <StaticString.h>
#include <Exceptions.h>
#include <Constants.h>
#include <Utils.h>
#include <Utils/MD5.h>
#include <Utils/IOUtils.h>
#include <Utils/MessageIO.h>
#include <Utils/VariantMap.h>
#include <Utils/StrIntUtils.h>
#include <Utils/StringMap.h>


namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


class LoggingServer: public EventedMessageServer {
private:
	static const int MAX_LOG_SINK_CACHE_SIZE = 512;
	static const int GARBAGE_COLLECTION_TIMEOUT = 4500;  // 1 hour 15 minutes

	struct LogSink;
	typedef boost::shared_ptr<LogSink> LogSinkPtr;
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

		/** The amount of data that has been written to this sink so far. */
		unsigned int writtenTo;

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
			lastFlushed = lastUsed;
			writtenTo = 0;
		}

		virtual ~LogSink() {
			// We really want to flush() here but can't call virtual
			// functions in destructor. :(
		}

		virtual bool isRemote() const {
			return false;
		}

		// Default interval at which this sink should be flushed.
		virtual unsigned int defaultFlushInterval() const {
			return 5;
		}

		virtual void append(const DataStoreId &dataStoreId,
			const StaticString &data)
		{
			writtenTo += data.size();
		}

		virtual bool flush() {
			lastFlushed = ev_now(server->getLoop());
			return true;
		}

		virtual void dump(ostream &stream) const { }
	};

	struct LogFileSink: public LogSink {
		string filename;
		FileDescriptor fd;

		LogFileSink(LoggingServer *server, const string &filename)
			: LogSink(server)
		{
			if (filename.empty()) {
				this->filename = "/dev/null";
			} else {
				this->filename = filename;
			}
			fd = syscalls::open(filename.c_str(),
				O_CREAT | O_WRONLY | O_APPEND,
				0600);
			if (fd == -1) {
				int e = errno;
				throw FileSystemException("Cannnot open file", e, filename);
			}
		}

		virtual ~LogFileSink() {
			flush();
		}

		virtual void append(const DataStoreId &dataStoreId, const StaticString &data) {
			LogSink::append(dataStoreId, data);
			syscalls::write(fd, data.data(), data.size());
		}

		virtual void dump(ostream &stream) const {
			stream << "   * Log file: " << filename << "\n";
			stream << "     Opened     : " << opened << "\n";
			stream << "     LastUsed   : " << distanceOfTimeInWords((time_t) lastUsed) << " ago\n";
			stream << "     LastFlushed: " << distanceOfTimeInWords((time_t) lastFlushed) << " ago\n";
			stream << "     WrittenTo  : " << writtenTo << "\n";
		}
	};

	typedef boost::shared_ptr<LogFileSink> LogFileSinkPtr;

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

		virtual unsigned int defaultFlushInterval() const {
			return 5;
		}

		virtual void append(const DataStoreId &dataStoreId, const StaticString &data) {
			LogSink::append(dataStoreId, data);
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

		virtual bool flush() {
			if (bufferSize > 0) {
				lastFlushed = ev_now(server->getLoop());
				StaticString data(buffer, bufferSize);
				server->remoteSender.schedule(unionStationKey, nodeName,
					category, &data, 1);
				bufferSize = 0;
				P_DEBUG("Flushed remote sink " << inspect() << ": " << bufferSize << " bytes");
				return true;
			} else {
				P_DEBUG("Flushed remote sink " << inspect() << ": 0 bytes");
				return false;
			}
		}

		string inspect() const {
			return "(key=" + unionStationKey + ", node=" + nodeName + ", category=" + category + ")";
		}

		virtual void dump(ostream &stream) const {
			stream << "   * Remote sink\n";
			stream << "     Key        : " << unionStationKey << "\n";
			stream << "     Node       : " << nodeName << "\n";
			stream << "     Category   : " << category << "\n";
			stream << "     Opened     : " << opened << "\n";
			stream << "     LastUsed   : " << distanceOfTimeInWords((time_t) lastUsed) << " ago\n";
			stream << "     LastFlushed: " << distanceOfTimeInWords((time_t) lastFlushed) << " ago\n";
			stream << "     WrittenTo  : " << writtenTo << "\n";
			stream << "     BufferSize : " << bufferSize << "\n";
		}
	};

	struct Transaction {
		LoggingServer *server;
		LogSinkPtr logSink;
		ev_tstamp createdAt;
		string txnId;
		DataStoreId dataStoreId;
		unsigned int writeCount;
		int refcount;
		bool crashProtect, discarded;
		string data;
		string filters;

		Transaction(LoggingServer *server, ev_tstamp createdAt) {
			this->server = server;
			this->createdAt = createdAt;
			data.reserve(8 * 1024);
		}

		~Transaction() {
			if (logSink != NULL) {
				if (!discarded && passesFilter()) {
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
			stream << "   * Transaction " << txnId << "\n";
			stream << "     Created at: " << distanceOfTimeInWords((time_t) createdAt) << " ago\n";
			stream << "     Group     : " << getGroupName() << "\n";
			stream << "     Node      : " << getNodeName() << "\n";
			stream << "     Category  : " << getCategory() << "\n";
			stream << "     Refcount  : " << refcount << "\n";
		}

	private:
		bool passesFilter() {
			if (filters.empty()) {
				return true;
			}

			const char *current = filters.data();
			const char *end     = filters.data() + filters.size();
			bool result         = true;
			FilterSupport::ContextFromLog ctx(data);

			// 'filters' may contain multiple filter sources, separated
			// by '\1' characters. Process each.
			while (current < end && result) {
				StaticString tmp(current, end - current);
				size_t pos = tmp.find('\1');
				if (pos == string::npos) {
					pos = tmp.size();
				}

				StaticString source(current, pos);
				FilterSupport::Filter &filter = server->compileFilter(source);
				result = filter.run(ctx);

				current = tmp.data() + pos + 1;
			}
			return result;
		}
	};

	typedef boost::shared_ptr<Transaction> TransactionPtr;

	enum ClientType {
		UNINITIALIZED,
		LOGGER
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

		template<typename Stream>
		void inspect(Stream &stream) const {
			stream << "   * Client " << (int) fd << "\n";
			stream << "     Initialized      : " << bool(type == LOGGER) << "\n";
			stream << "     Node name        : " << nodeName << "\n";
			stream << "     Open transactions: (" << openTransactions.size() << ")";
			set<string>::const_iterator it, end = openTransactions.end();
			for (it = openTransactions.begin(); it != end; it++) {
				stream << " " << *it;
			}
			stream << "\n";
			stream << "     Connection state : " << getStateName() << "\n";
			stream << "     Message state    : " << messageServer.getStateName() << "\n";
			stream << "     Outbox           : " << outbox.size() << " bytes\n";
		}
	};

	typedef boost::shared_ptr<Client> ClientPtr;
	typedef map<string, TransactionPtr> TransactionMap;

	typedef boost::shared_ptr<FilterSupport::Filter> FilterPtr;

	RemoteSender remoteSender;
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
	StringMap<FilterPtr> filters;
	RandomGenerator randomGenerator;
	bool refuseNewConnections;
	bool exitRequested;
	unsigned long long exitBeginTime;
	int sinkFlushInterval;
	string dumpFile;

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

	bool expectingMinArgumentsCount(Client *client, const vector<StaticString> &args, unsigned int size) {
		if (args.size() >= size) {
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

	static bool getBool(const vector<StaticString> &args, unsigned int index,
		bool defaultValue = false)
	{
		if (index < args.size()) {
			return args[index] == "true";
		} else {
			return defaultValue;
		}
	}

	static StaticString getStaticString(const vector<StaticString> &args,
		unsigned int index, const StaticString &defaultValue = "")
	{
		if (index < args.size()) {
			return args[index];
		} else {
			return defaultValue;
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
		// TODO: must be hexadecimal
		// TODO: must not be too large
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
		// TODO: must be hexadecimal
		// TODO: must not be too large
		return true;
	}

	bool supportedCategory(const StaticString &category) const {
		return category == "requests" || category == "processes"
			|| category == "exceptions" || category == "system_metrics";
	}

	LogSinkPtr openLogFile() {
		string cacheKey = "file:" + dumpFile;
		LogSinkPtr result;
		LogSinkCache::iterator it = logSinkCache.find(cacheKey);
		if (it == logSinkCache.end()) {
			trimLogSinkCache(MAX_LOG_SINK_CACHE_SIZE - 1);
			result = boost::make_shared<LogFileSink>(this, dumpFile);
			pair<LogSinkCache::iterator, bool> p =
				logSinkCache.insert(make_pair(cacheKey, result));
			result->cacheIterator = p.first;
			result->opened = 1;
		} else {
			result = it->second;
			result->opened++;
			if (result->opened == 1) {
				inactiveLogSinks.erase(result->inactiveLogSinksIterator);
				inactiveLogSinksCount--;
			}
		}
		return result;
	}

	LogSinkPtr openRemoteSink(const StaticString &unionStationKey, const string &nodeName,
		const string &category)
	{
		string cacheKey = "remote:";
		cacheKey.append(unionStationKey.c_str(), unionStationKey.size());
		cacheKey.append(1, '\0');
		cacheKey.append(nodeName);
		cacheKey.append(1, '\0');
		cacheKey.append(category);

		LogSinkPtr result;
		LogSinkCache::iterator it = logSinkCache.find(cacheKey);
		if (it == logSinkCache.end()) {
			trimLogSinkCache(MAX_LOG_SINK_CACHE_SIZE - 1);
			result = boost::make_shared<RemoteSink>(this, unionStationKey,
				nodeName, category);
			pair<LogSinkCache::iterator, bool> p =
				logSinkCache.insert(make_pair(cacheKey, result));
			result->cacheIterator = p.first;
			result->opened = 1;
		} else {
			result = it->second;
			result->opened++;
			if (result->opened == 1) {
				inactiveLogSinks.erase(result->inactiveLogSinksIterator);
				inactiveLogSinksCount--;
			}
		}
		return result;
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

	FilterSupport::Filter &compileFilter(const StaticString &source) {
		// TODO: garbage collect filters based on time
		FilterPtr filter = filters.get(source);
		if (filter == NULL) {
			filter = boost::make_shared<FilterSupport::Filter>(source);
			filters.set(source, filter);
		}
		return *filter;
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
		#if defined(__sun__) || defined(sun) || defined(_AIX)
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

	ev_tstamp getFlushInterval(const LogSink *sink) const {
		if (sinkFlushInterval == 0) {
			return sink->defaultFlushInterval();
		} else {
			return sinkFlushInterval;
		}
	}

	void sinkFlushTimeout(ev::timer &timer, int revents) {
		P_DEBUG("Flushing all sinks");
		LogSinkCache::iterator it;
		LogSinkCache::iterator end = logSinkCache.end();
		ev_tstamp now = ev_now(getLoop());

		for (it = logSinkCache.begin(); it != end; it++) {
			LogSink *sink = it->second.get();
			if (now - sink->lastFlushed >= getFlushInterval(sink)) {
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
			ev_break(getLoop(), EVBREAK_ONE);
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
			if (OXT_UNLIKELY( !expectingMinArgumentsCount(client, args, 7)
			               || !expectingLoggerType(client) )) {
				return true;
			}

			string       txnId     = args[1];
			StaticString groupName = args[2];
			StaticString nodeName  = args[3];
			StaticString category  = args[4];
			StaticString timestamp = args[5];
			StaticString unionStationKey = args[6];
			bool         crashProtect    = getBool(args, 7, true);
			bool         ack             = getBool(args, 8, false);
			StaticString filters         = getStaticString(args, 9);

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

				transaction = boost::make_shared<Transaction>(this, ev_now(getLoop()));
				if (unionStationKey.empty() || unionStationKey == "-") {
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

					transaction->logSink = openLogFile();
				} else {
					transaction->logSink = openRemoteSink(unionStationKey,
						client->nodeName, category);
				}
				transaction->txnId        = txnId;
				transaction->dataStoreId  = DataStoreId(groupName,
					nodeName, category);
				transaction->writeCount   = 0;
				transaction->refcount     = 0;
				transaction->crashProtect = crashProtect;
				if (!filters.empty()) {
					transaction->filters = filters;
				}
				transaction->discarded    = false;
				transactions.insert(make_pair(txnId, transaction));
			} else {
				transaction = it->second;
				if (OXT_UNLIKELY( transaction->getGroupName() != groupName )) {
					sendErrorToClient(client,
						"Cannot open transaction: transaction already opened with a "
						"different group name ('" + transaction->getGroupName() +
						"' vs '" + groupName + "')");
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

			if (ack) {
				client->writeArrayMessage("ok", NULL);
			}

		} else if (args[0] == "closeTransaction") {
			if (OXT_UNLIKELY( !expectingMinArgumentsCount(client, args, 3)
			               || !expectingLoggerType(client) )) {
				return true;
			}

			string txnId = args[1];
			StaticString timestamp = args[2];
			bool         ack       = getBool(args, 3, false);

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

			if (ack) {
				client->writeArrayMessage("ok", NULL);
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
				ev_break(getLoop(), EVBREAK_ONE);
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
		const VariantMap &options = VariantMap())
		: EventedMessageServer(loop, fd, accountsDatabase),
		  remoteSender(
		      options.get("union_station_gateway_address", false, DEFAULT_UNION_STATION_GATEWAY_ADDRESS),
		      options.getInt("union_station_gateway_port", false, DEFAULT_UNION_STATION_GATEWAY_PORT),
		      options.get("union_station_gateway_cert", false, ""),
		      options.get("union_station_proxy_address", false)),
		  garbageCollectionTimer(loop),
		  sinkFlushingTimer(loop),
		  exitTimer(loop),
		  dumpFile(options.get("analytics_dump_file", false, "/dev/null"))
	{
		int sinkFlushTimerInterval = options.getInt("analytics_sink_flush_timer_interval", false, 5);
		sinkFlushInterval = options.getInt("analytics_sink_flush_interval", false, 0);
		garbageCollectionTimer.set<LoggingServer, &LoggingServer::garbageCollect>(this);
		garbageCollectionTimer.start(GARBAGE_COLLECTION_TIMEOUT, GARBAGE_COLLECTION_TIMEOUT);
		sinkFlushingTimer.set<LoggingServer, &LoggingServer::sinkFlushTimeout>(this);
		sinkFlushingTimer.start(sinkFlushTimerInterval, sinkFlushTimerInterval);
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
		// be flushed before RemoteSender is being destroyed.
		transactions.clear();
		logSinkCache.clear();
		inactiveLogSinks.clear();
	}

	void dump(ostream &stream) const {
		TransactionMap::const_iterator it;
		TransactionMap::const_iterator end = transactions.end();

		ClientSet::const_iterator cit, cend = getClients().end();
		stream << "Clients:\n";
		stream << "  Count: " << getClients().size() << "\n";
		for (cit = getClients().begin(); cit != cend; cit++) {
			const Client *client = static_cast<Client *>(*cit);
			client->inspect(stream);
		}
		stream << "\n";

		stream << "RemoteSender:\n";
		remoteSender.inspect(stream);
		stream << "\n";

		LogSinkCache::const_iterator sit;
		LogSinkCache::const_iterator send = logSinkCache.end();
		stream << "Open log sinks:\n";
		stream << "   Count: " << logSinkCache.size() <<
			" (of which " << inactiveLogSinksCount << " inactive)\n";
		for (sit = logSinkCache.begin(); sit != send; sit++) {
			const LogSinkPtr &logSink = sit->second;
			logSink->dump(stream);
		}
		stream << "\n";

		stream << "Open transactions:\n";
		stream << "   Count: " << transactions.size() << "\n";
		for (it = transactions.begin(); it != end; it++) {
			const TransactionPtr &transaction = it->second;
			transaction->dump(stream);
		}
	}
};

typedef boost::shared_ptr<LoggingServer> LoggingServerPtr;


} // namespace Passenger

#endif /* _PASSENGER_LOGGING_SERVER_H_ */
