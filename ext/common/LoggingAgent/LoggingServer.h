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
#include <map>
#include <ev++.h>

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
#include "../Utils.h"
#include "../Utils/MD5.h"


namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


class LoggingServer: public EventedMessageServer {
private:
	struct LogFile {
		static const unsigned int BUFFER_CAPACITY = 8 * 1024;
		
		FileDescriptor fd;
		time_t lastUsed;
		char buffer[BUFFER_CAPACITY];
		unsigned int bufferSize;
		
		LogFile() {
			bufferSize = 0;
		}
		
		void append(const StaticString data[], unsigned int count) {
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
				MessageChannel(fd).writeRawGather(data2, count + 1);
				bufferSize = 0;
			} else {
				for (i = 0; i < count; i++) {
					memcpy(buffer + bufferSize, data[i].data(), data[i].size());
					bufferSize += data[i].size();
				}
			}
		}
		
		void flush() {
			if (bufferSize > 0) {
				MessageChannel(fd).writeRaw(StaticString(buffer, bufferSize));
				bufferSize = 0;
			}
		}
		
		~LogFile() {
			flush();
		}
	};
	
	typedef shared_ptr<LogFile> LogFilePtr;
	
	struct Transaction {
		LogFilePtr logFile;
		string txnId;
		string groupName;
		string category;
		unsigned int writeCount;
		unsigned int refcount;
	};
	
	typedef shared_ptr<Transaction> TransactionPtr;
	typedef map<string, LogFilePtr> LogFileCache;
	
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
	ev::timer garbageCollectionTimer;
	ev::timer logFlushingTimer;
	TransactionMap transactions;
	LogFileCache logFileCache;
	RandomGenerator randomGenerator;
	
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
		// may not be empty
		// must contain timestamp
		// must contain separator
		// must contain random id
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
	
	bool openLogFileWithCache(const string &filename, LogFilePtr &theLogFile) {
		LogFileCache::iterator it = logFileCache.find(filename);
		if (it == logFileCache.end()) {
			makeDirTree(extractDirName(filename), dirPermissions,
				USER_NOT_GIVEN, gid);
			
			LogFilePtr logFile = ptr(new LogFile());
			int ret;
			
			logFile->fd = syscalls::open(filename.c_str(),
				O_CREAT | O_WRONLY | O_APPEND,
				filePermissions);
			if (logFile->fd == -1) {
				int e = errno;
				throw FileSystemException("Cannnot open file", e, filename);
			}
			do {
				ret = fchmod(logFile->fd, filePermissions);
			} while (ret == -1 && errno == EINTR);
			logFile->lastUsed = time(NULL);
			logFileCache[filename] = logFile;
			theLogFile = logFile;
			return false;
		} else {
			theLogFile = it->second;
			theLogFile->lastUsed = time(NULL);
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
	
	void writeLogEntry(Client *client, const TransactionPtr &transaction,
		const StaticString &timestamp, const StaticString &data)
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
		transaction->logFile->append(args, sizeof(args) / sizeof(StaticString));
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
	
	void garbageCollect(ev::timer &timer, int revents) {
		LogFileCache::iterator it;
		LogFileCache::iterator end = logFileCache.end();
		time_t now = time(NULL);
		vector<string> toDelete;
		
		// Delete all cached file handles that haven't been used for more than 2 hours.
		for (it = logFileCache.begin(); it != end; it++) {
			if (now - it->second->lastUsed > 2 * 60 * 60) {
				toDelete.push_back(it->first);
			}
		}
		
		vector<string>::const_iterator it2;
		for (it2 = toDelete.begin(); it2 != toDelete.end(); it2++) {
			logFileCache.erase(*it2);
		}
	}
	
	void flushAllLogs(ev::timer &timer, int revents = 0) {
		LogFileCache::iterator it;
		LogFileCache::iterator end = logFileCache.end();
		for (it = logFileCache.begin(); it != end; it++) {
			it->second->flush();
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
				set<string>::iterator sit = client->openTransactions.find(txnId);
				if (OXT_UNLIKELY( sit == client->openTransactions.end() )) {
					writeArrayMessage(eclient, "error",
						"Cannot log data: transaction not opened in this connection",
						NULL);
					disconnect(eclient);
				} else {
					// Expecting the log data in a scalar message.
					client->currentTransaction = it->second;
					client->currentTimestamp = timestamp;
					return false;
				}
			}
			
		} else if (args[0] == "openTransaction") {
			if (OXT_UNLIKELY( !expectingArgumentsCount(eclient, args, 5)
			               || !expectingInitialized(eclient) )) {
				return true;
			}
			
			string       txnId     = args[1];
			StaticString groupName = args[2];
			StaticString category  = args[3];
			StaticString timestamp = args[4];
			
			if (OXT_UNLIKELY( !validTxnId(txnId) )) {
				sendErrorToClient(eclient, "Invalid transaction ID format");
				disconnect(eclient);
				return true;
			}
			if (OXT_UNLIKELY( client->openTransactions.find(txnId) !=
				client->openTransactions.end()))
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
				
				string filename = determineFilename(groupName, client->nodeId,
					category, txnId);
				transaction.reset(new Transaction());
				if (!openLogFileWithCache(filename, transaction->logFile)) {
					setupGroupAndNodeDir(client, groupName);
				}
				transaction->txnId      = txnId;
				transaction->groupName  = groupName;
				transaction->category   = category;
				transaction->writeCount = 0;
				transaction->refcount   = 1;
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
				
				transaction->refcount++;
			}
			client->openTransactions.insert(txnId);
			writeLogEntry(client, transaction, timestamp,
				"ATTACH");
			
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
				set<string>::const_iterator sit = client->openTransactions.find(txnId);
				if (OXT_UNLIKELY( sit == client->openTransactions.end() )) {
					sendErrorToClient(eclient,
						"Cannot close transaction " + txnId +
						": transaction not opened in this connection");
					disconnect(eclient);
				} else {
					TransactionPtr &transaction = it->second;
					writeLogEntry(client, transaction, timestamp, "DETACH");
					client->openTransactions.erase(sit);
					transaction->refcount--;
					if (transaction->refcount == 0) {
						transactions.erase(it);
					}
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
			
		} else if (args[0] == "exit") {
			if (!requireRights(eclient, Account::EXIT)) {
				disconnect(eclient);
				return true;
			}
			writeArrayMessage(eclient, "Passed security");
			writeArrayMessage(eclient, "exit command received");
			ev_unloop(getLoop(), EVUNLOOP_ONE);
			
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
	
	virtual void onClientDisconnected(const EventedServer::ClientPtr &_client) {
		Client *client = static_cast<Client *>(_client.get());
		set<string>::const_iterator sit;
		set<string>::const_iterator send = client->openTransactions.end();
		
		for (sit = client->openTransactions.begin(); sit != send; sit++) {
			const string &txnId = *sit;
			TransactionMap::iterator it = transactions.find(txnId);
			if (OXT_UNLIKELY(it == transactions.end())) {
				P_ERROR("Bug: client->openTransactions is not a subset of this->transactions!");
				abort();
			}
			
			TransactionPtr &transaction = it->second;
			char timestamp[2 * sizeof(unsigned long long) + 1];
			integerToHex<unsigned long long>(SystemTime::getUsec(), timestamp);
			
			writeLogEntry(client, transaction, timestamp, "DETACH");
			transaction->refcount--;
			if (transaction->refcount == 0) {
				transactions.erase(it);
			}
		}
		client->openTransactions.clear();
	}

public:
	LoggingServer(struct ev_loop *loop, FileDescriptor fd,
		const AccountsDatabasePtr &accountsDatabase, const string &dir,
		const string &permissions = "u=rwx,g=rx,o=rx", gid_t gid = GROUP_NOT_GIVEN)
		: EventedMessageServer(loop, fd, accountsDatabase),
		  garbageCollectionTimer(loop),
		  logFlushingTimer(loop)
	{
		this->dir = dir;
		this->gid = gid;
		dirPermissions = permissions;
		filePermissions = parseModeString(permissions) & ~(S_IXUSR | S_IXGRP | S_IXOTH);
		garbageCollectionTimer.set<LoggingServer, &LoggingServer::garbageCollect>(this);
		garbageCollectionTimer.start(60 * 60, 60 * 60);
		logFlushingTimer.set<LoggingServer, &LoggingServer::flushAllLogs>(this);
		logFlushingTimer.start(1, 1);
	}
};

typedef shared_ptr<LoggingServer> LoggingServerPtr;


} // namespace Passenger

#endif /* _PASSENGER_LOGGING_SERVER_H_ */
