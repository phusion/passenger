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
#ifndef _PASSENGER_UST_ROUTER_CONTROLLER_H_
#define _PASSENGER_UST_ROUTER_CONTROLLER_H_

#include <string>
#include <set>
#include <cassert>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/foreach.hpp>
#include <oxt/backtrace.hpp>
#include <ev++.h>
#include <SmallVector.h>
#include <ServerKit/Server.h>
#include <StaticString.h>
#include <Constants.h>
#include <Logging.h>
#include <UstRouter/Transaction.h>
#include <UstRouter/Client.h>
#include <UstRouter/FileSink.h>
#include <UstRouter/RemoteSink.h>
#include <UnionStationFilterSupport.h>
#include <MessageReadersWriters.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/StringMap.h>
#include <Utils/SystemTime.h>
#include <Utils/VariantMap.h>


namespace Passenger {
namespace UstRouter {

using namespace std;
using namespace boost;
using namespace oxt;


class Controller: public ServerKit::BaseServer<Controller, Client> {
private:
	static const unsigned int GARBAGE_COLLECTION_TIMEOUT = 60; // 1 minute
	static const unsigned int LOG_SINK_MAX_IDLE_TIME = 5 * 60; // 5 minutes
	static const unsigned int TXN_ID_MAX_SIZE =
		2 * sizeof(unsigned int) +    // max hex timestamp size
		11 +                          // space for a random identifier
		1;                            // null terminator

	friend inline struct ::ev_loop *UstRouter::Controller_getLoop(Controller *controller);
	friend inline RemoteSender &UstRouter::Controller_getRemoteSender(Controller *controller);

	typedef ServerKit::BaseServer<Controller, Client> ParentClass;
	typedef ServerKit::Channel Channel;
	typedef StringMap<TransactionPtr> TransactionMap;
	typedef StringMap<LogSinkPtr> LogSinkCache;

	string username;
	string password;
	string dumpDir;
	string defaultNodeName;
	bool devMode;

	RandomGenerator randomGenerator;
	TransactionMap transactions;
	LogSinkCache logSinkCache;
	RemoteSender remoteSender;
	StringMap<FilterSupport::FilterPtr> filters;

	ev::timer gcTimer;
	ev::timer flushTimer;
	int sinkFlushInterval;


	/****** Handshake and authentication ******/

	void beginHandshake(Client *client) {
		StaticString reply[] = {
			P_STATIC_STRING("version"),
			P_STATIC_STRING("1")
		};
		writeArrayMessage(client, reply, 2);

		// Begin reading authentication username. Control
		// continues in onAuthUsernameDataReceived().
		client->state = Client::READING_AUTH_USERNAME;
	}

	Channel::Result onAuthUsernameDataReceived(Client *client, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		size_t consumed = client->scalarReader.feed(buffer.start, buffer.size());
		if (client->scalarReader.done()) {
			processAuthUsername(client);
		}
		return Channel::Result(consumed, false);
	}

	void processAuthUsername(Client *client) {
		if (client->scalarReader.hasError()) {
			string reason = "Error parsing username: ";
			reason.append(client->scalarReader.errorString());
			sendErrorToClient(client, reason);
			if (client->connected()) {
				disconnectWithError(&client, reason);
			}
			return;
		}

		StaticString username = client->scalarReader.value();
		if (!constantTimeCompare(username, this->username)) {
			sendErrorToClient(client, "Invalid username or password");
			if (client->connected()) {
				disconnectWithError(&client, "Client sent invalid username");
			}
			return;
		}

		// Begin reading authentication password. Control continues
		// in onAuthPasswordDataReceived().
		SKC_DEBUG(client, "Username is correct");
		client->scalarReader.reset();
		client->state = Client::READING_AUTH_PASSWORD;
	}

	Channel::Result onAuthPasswordDataReceived(Client *client, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		size_t consumed = client->scalarReader.feed(buffer.start, buffer.size());
		if (client->scalarReader.done()) {
			processAuthPassword(client);
		}
		return Channel::Result(consumed, false);
	}

	void processAuthPassword(Client *client) {
		if (client->scalarReader.hasError()) {
			string reason = "Error parsing password: ";
			reason.append(client->scalarReader.errorString());
			sendErrorToClient(client, reason);
			if (client->connected()) {
				disconnectWithError(&client, reason);
			}
			return;
		}

		StaticString password = client->scalarReader.value();
		if (!constantTimeCompare(password, this->password)) {
			sendErrorToClient(client, "Invalid username or password");
			if (client->connected()) {
				disconnectWithError(&client, "Client sent invalid password");
			}
			return;
		}

		// We are now authenticated.
		client->scalarReader.reset(true);
		SKC_DEBUG(client, "Password is correct. Client fully authenticated");
		sendOkToClient(client);

		// Begin reading normal message. Control continues in onWorkDataReceived().
		client->state = Client::READING_MESSAGE;
	}


	/****** Normal message handling: parser and router ******/

	Channel::Result onMessageDataReceived(Client *client, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		size_t consumed = client->arrayReader.feed(buffer.start, buffer.size());

		if (client->arrayReader.hasError()) {
			disconnectWithError(&client,
				string("Error processing message: array message parse error: ")
				+ client->arrayReader.errorString());
			return Channel::Result(consumed, true);
		}

		if (client->arrayReader.done()) {
			// No error
			const vector<StaticString> &message = client->arrayReader.value();
			SKC_DEBUG(client, "Message received: " << toString(message));
			if (message.size() < 1) {
				disconnectWithError(&client, "Error processing message:"
					" too few parameters");
				return Channel::Result(consumed, true);
			}

			processNewMessage(client, message);
			client->arrayReader.reset();
		}
		return Channel::Result(consumed, false);
	}

	Channel::Result onMessageBodyDataReceived(Client *client, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		size_t consumed = client->scalarReader.feed(buffer.start, buffer.size());

		if (client->scalarReader.hasError()) {
			disconnectWithError(&client,
				string("Error processing message: scalar message parse error: ")
				+ client->scalarReader.errorString());
			return Channel::Result(consumed, true);
		}

		if (client->scalarReader.done()) {
			// No error
			processLogMessageBody(client, client->scalarReader.value());
			client->scalarReader.reset();
		}
		return Channel::Result(consumed, false);
	}

	void processNewMessage(Client *client, const vector<StaticString> &args) {
		try {
			if (args[0] == P_STATIC_STRING("log")) {
				processLogMessage(client, args);
			} else if (args[0] == P_STATIC_STRING("openTransaction")) {
				processOpenTransactionMessage(client, args);
			} else if (args[0] == P_STATIC_STRING("closeTransaction")) {
				processCloseTransactionMessage(client, args);
			} else if (args[0] == P_STATIC_STRING("init")) {
				processInitMessage(client, args);
			} else if (args[0] == P_STATIC_STRING("info")) {
				processInfoMessage(client, args);
			} else if (args[0] == P_STATIC_STRING("ping")) {
				processPingMessage(client, args);
			} else {
				processUnknownMessage(client, args);
			}
		} catch (const oxt::tracable_exception &e) {
			SKC_ERROR(client, "Exception: " << e.what() << "\n" << e.backtrace());
			if (client->connected()) {
				disconnect(&client);
			}
		}
	}


	/****** Individual message handlers ******/

	void processLogMessage(Client *client, const vector<StaticString> &args) {
		StaticString txnId, timestamp;
		bool ack;
		TransactionPtr transaction;
		set<string>::iterator s_it;

		if (OXT_UNLIKELY(!expectingMinArgumentsCount(client, args, 3)
		              || !expectingLoggerType(client)))
		{
			goto done;
		}

		txnId     = args[1];
		timestamp = args[2];
		ack       = getBool(args, 3, false);

		transaction = transactions.get(txnId);
		if (OXT_UNLIKELY(transaction == NULL)) {
			SKC_ERROR(client, "Cannot log data: transaction does not exist");
			if (ack) {
				sendErrorToClient(client, "Cannot log data: transaction does not exist");
				if (client->connected()) {
					disconnect(&client);
				}
			}
			goto done;
		}

		s_it = client->openTransactions.find(transaction->getTxnId());
		if (OXT_UNLIKELY(s_it == client->openTransactions.end())) {
			SKC_ERROR(client, "Cannot log data: transaction not opened in this connection");
			if (ack) {
				sendErrorToClient(client,
					"Cannot log data: transaction not opened in this connection");
				if (client->connected()) {
					disconnect(&client);
				}
			}
			goto done;
		}

		client->logCommandParams.transaction = transaction;
		client->logCommandParams.timestamp.assign(timestamp.data(), timestamp.size());
		client->logCommandParams.ack = ack;

		// Control will continue in processLogMessageBody()
		// when body is fully read.
		client->state = Client::READING_MESSAGE_BODY;

		if (ack) {
			sendOkToClient(client);
		}

		done:
		if (client != NULL && client->connected()) {
			SKC_DEBUG(client, "Done processing 'log' message");
		}
	}

	void processLogMessageBody(Client *client, const StaticString &body) {
		// In here we process the scalar message that's expected to come
		// after the "log" command.

		if (getLogLevel() == LVL_DEBUG) {
			string truncatedBody;
			if (body.size() > 97) {
				string truncatedBody = body.substr(0, 97);
				truncatedBody.append("...");
				SKC_DEBUG(client, "Processing message body (" << body.size() <<
					" bytes): " << truncatedBody);
			} else {
				SKC_DEBUG(client, "Processing message body (" << body.size() <<
					" bytes): " << body);
			}
		} else if (getLogLevel() >= LVL_DEBUG2) {
			SKC_TRACE(client, 2, "Processing message body (" << body.size() <<
				" bytes): " << body);
		}

		writeLogEntry(client,
			client->logCommandParams.transaction,
			client->logCommandParams.timestamp,
			body,
			client->logCommandParams.ack);
		client->logCommandParams.transaction.reset();
		client->logCommandParams.timestamp.clear();

		client->state = Client::READING_MESSAGE;

		if (client->connected()) {
			SKC_DEBUG(client, "Done processing 'log' message body");
		}
	}

	void processOpenTransactionMessage(Client *client, const vector<StaticString> &args) {
		if (OXT_UNLIKELY(!expectingMinArgumentsCount(client, args, 7)
		              || !expectingLoggerType(client)))
		{
			return;
		}

		StaticString txnId     = args[1];
		StaticString groupName = args[2];
		StaticString nodeName  = args[3];
		StaticString category  = args[4];
		StaticString timestamp = args[5];
		StaticString unionStationKey = args[6];
		bool         crashProtect    = getBool(args, 7, true);
		bool         ack             = getBool(args, 8, false);
		StaticString filters         = getStaticString(args, 9);

		TransactionPtr transaction;
		char autogeneratedTxnIdBuf[TXN_ID_MAX_SIZE];
		char *autogeneratedTxnIdBufEnd;
		bool autogenTxnId = txnId.empty();

		if (autogenTxnId) {
			// Autogeneration requested
			if (ack) {
				unsigned long long timestamp = SystemTime::getUsec();
				createTxnId(autogeneratedTxnIdBuf,
					&autogeneratedTxnIdBufEnd,
					timestamp);
				txnId = StaticString(autogeneratedTxnIdBuf,
					autogeneratedTxnIdBufEnd - autogeneratedTxnIdBuf);
			} else {
				SKC_ERROR(client, "Transaction autogeneration requested,"
					" but 'ack' parameter is set to false");
				goto done;
			}
		}

		if (OXT_UNLIKELY(!validTxnId(txnId))) {
			SKC_ERROR(client, "Invalid transaction ID format");
			if (ack) {
				sendErrorToClient(client, "Invalid transaction ID format");
				if (client->connected()) {
					disconnect(&client);
				}
			}
			goto done;
		}
		if (!unionStationKey.empty()
		 && OXT_UNLIKELY(!validUnionStationKey(unionStationKey)))
		{
			SKC_ERROR(client, "Invalid Union Station key format");
			if (ack) {
				sendErrorToClient(client, "Invalid Union Station key format");
				if (client->connected()) {
					disconnect(&client);
				}
			}
			goto done;
		}

		if (nodeName.empty()) {
			nodeName = client->nodeName;
		}

		transaction = transactions.get(txnId);
		if (transaction == NULL) {
			if (OXT_UNLIKELY(!supportedCategory(category))) {
				SKC_ERROR(client, "Unsupported category '" << category << "'");
				if (ack) {
					sendErrorToClient(client, "Unsupported category");
					if (client->connected()) {
						disconnect(&client);
					}
				}
				goto done;
			}

			transaction = boost::make_shared<Transaction>(
				txnId, groupName, nodeName, category,
				unionStationKey, ev_now(getLoop()), filters
			);
			transaction->enableCrashProtect(crashProtect);
			transactions.set(txnId, transaction);
		} else {
			if (OXT_UNLIKELY(client->openTransactions.find(transaction->getTxnId()) !=
				client->openTransactions.end()))
			{
				SKC_ERROR(client, "Cannot open transaction: transaction already opened in this connection");
				if (ack) {
					sendErrorToClient(client, "Cannot open transaction: transaction already opened in this connection");
					if (client->connected()) {
						disconnect(&client);
					}
				}
				goto done;
			}
			if (OXT_UNLIKELY(transaction->getCategory() != category)) {
				SKC_ERROR(client, "Cannot open transaction: transaction already opened with a different category name (" <<
						transaction->getCategory() << " vs " << category << ")");
				if (ack) {
					sendErrorToClient(client,
							"Cannot open transaction: transaction already opened with a different category name (" +
							transaction->getCategory() + " vs " + category + ")");
					if (client->connected()) {
						disconnect(&client);
					}
				}
				goto done;
			}
			if (OXT_UNLIKELY(transaction->getNodeName() != nodeName)) {
				SKC_ERROR(client, "Cannot open transaction: transaction "
					"already opened with a different node name (" <<
					transaction->getNodeName() << " vs " << nodeName << ")");
				if (ack) {
					sendErrorToClient(client, "Cannot open transaction: transaction "
							"already opened with a different node name (" +
							transaction->getNodeName() + " vs " + nodeName + ")");
					if (client->connected()) {
						disconnect(&client);
					}
				}
				goto done;
			}
			if (OXT_UNLIKELY(transaction->getUnionStationKey() != unionStationKey)) {
				SKC_ERROR(client,
					"Cannot open transaction: transaction already opened with a "
					"different key ('" << transaction->getUnionStationKey() <<
					"' vs '" << unionStationKey << "')");
				if (ack) {
					sendErrorToClient(client,
						"Cannot open transaction: transaction already opened with a "
						"different key ('" + transaction->getUnionStationKey() +
						"' vs '" + unionStationKey + "')");
					if (client->connected()) {
						disconnect(&client);
					}
				}
				goto done;
			}
		}

		client->openTransactions.insert(transaction->getTxnId());
		transaction->ref();
		writeLogEntry(client, transaction, timestamp, P_STATIC_STRING("ATTACH"), ack);

		if (client->connected() && ack) {
			if (autogenTxnId) {
				StaticString reply[] = {
					P_STATIC_STRING("status"),
					P_STATIC_STRING("ok"),
					txnId
				};
				writeArrayMessage(client, reply, 3);
			} else {
				sendOkToClient(client);
			}
		}

		done:
		if (client != NULL && client->connected()) {
			SKC_DEBUG(client, "Done processing 'openTransaction' message");
		}
	}

	void processCloseTransactionMessage(Client *client, const vector<StaticString> &args) {
		StaticString txnId, timestamp;
		bool ack;
		set<string>::const_iterator s_it;
		TransactionPtr transaction;

		if (OXT_UNLIKELY(!expectingMinArgumentsCount(client, args, 3)
		              || !expectingLoggerType(client)))
		{
			goto done;
		}

		txnId     = args[1];
		timestamp = args[2];
		ack       = getBool(args, 3, false);

		transaction = transactions.get(txnId);
		if (OXT_UNLIKELY(transaction == NULL)) {
			SKC_ERROR(client, "Cannot close transaction " << txnId <<
				": transaction does not exist");
			if (ack) {
				sendErrorToClient(client,
					"Cannot close transaction " + txnId +
					": transaction does not exist");
				if (client->connected()) {
					disconnect(&client);
				}
			}
			goto done;
		} else {
			s_it = client->openTransactions.find(transaction->getTxnId());
			if (OXT_UNLIKELY(s_it == client->openTransactions.end())) {
				SKC_ERROR(client, "Cannot close transaction " << txnId <<
					": transaction not opened in this connection");
				if (ack) {
					sendErrorToClient(client,
						"Cannot close transaction " + txnId +
						": transaction not opened in this connection");
					if (client->connected()) {
						disconnect(&client);
					}
				}
				goto done;
			}

			client->openTransactions.erase(s_it);
			writeDetachEntry(client, transaction, timestamp, ack);
			transaction->unref();
			if (transaction->getRefCount() == 0) {
				transactions.remove(txnId);
				closeTransaction(client, transaction);
			}
		}

		if (ack) {
			sendOkToClient(client);
		}

		done:
		if (client != NULL && client->connected()) {
			SKC_DEBUG(client, "Done processing 'closeTransaction' message");
		}
	}

	void processInitMessage(Client *client, const vector<StaticString> &args) {
		StaticString nodeName;

		if (OXT_UNLIKELY(client->type != Client::UNINITIALIZED)) {
			logErrorAndSendToClient(client, "Already initialized");
			if (client->connected()) {
				disconnect(&client);
			}
			goto done;
		}
		if (OXT_UNLIKELY(!expectingMinArgumentsCount(client, args, 1))) {
			goto done;
		}

		nodeName = getStaticString(args, 1);
		if (nodeName.empty()) {
			client->nodeName = defaultNodeName;
		} else {
			client->nodeName.assign(nodeName.data(), nodeName.size());
		}
		client->type = Client::LOGGER;
		sendOkToClient(client);

		done:
		if (client != NULL && client->connected()) {
			SKC_DEBUG(client, "Done processing 'init' message");
		}
	}

	void processInfoMessage(Client *client, const vector<StaticString> &args) {
		string info = inspectStateAsJson().toStyledString();

		StaticString reply[] = {
			P_STATIC_STRING("status"),
			P_STATIC_STRING("ok"),
			info
		};
		writeArrayMessage(client, reply, 3);

		if (client->connected()) {
			SKC_DEBUG(client, "Done processing 'info' message");
		}
	}

	void processPingMessage(Client *client, const vector<StaticString> &args) {
		StaticString reply = P_STATIC_STRING("pong");
		writeArrayMessage(client, &reply, 1);
		if (client->connected()) {
			SKC_DEBUG(client, "Done processing 'ping' message");
		}
	}

	void processUnknownMessage(Client *client, const vector<StaticString> &args) {
		string reason = "Unknown message: ";
		reason.append(toString(args));
		logErrorAndSendToClient(client, reason);
		if (client->connected()) {
			disconnect(&client);
		}
	}


	/****** Periodic tasks ******/

	/**
	 * A periodic task in which log sinks are garbage collected.
	 */
	void garbageCollect(ev::timer &timer, int revents) {
		P_DEBUG("Running UstRouter garbage collector");

		LogSinkCache::iterator it, end = logSinkCache.end();
		ev_tstamp threshold = ev_now(getLoop()) - LOG_SINK_MAX_IDLE_TIME;
		SmallVector<string, 8> toRemove;

		for (it = logSinkCache.begin(); it != end; it++) {
			const LogSinkPtr &sink = it->second;
			if (canGarbageCollectSink(sink, threshold)) {
				toRemove.push_back(string(it->first.data(), it->first.size()));
			}
		}

		foreach (string key, toRemove) {
			P_DEBUG("Garbage collecting UstRouter sink: " <<
				logSinkCache.get(key)->inspect());
			logSinkCache.remove(key);
		}

		P_DEBUG("Done running UstRouter garbage collector");
	}

	bool canGarbageCollectSink(const LogSinkPtr &sink, ev_tstamp threshold) const {
		return sink->isRemote()
			&& sink->opened == 0
			&& sink->lastClosed != 0
			&& sink->lastClosed < threshold;
	}

	/**
	 * A period task in which the sinks are flushed whose
	 * flush timeout have expired.
	 */
	void flushSomeSinks(ev::timer &timer, int revents) {
		P_DEBUG("Flushing sinks that need flushing");

		LogSinkCache::iterator it;
		LogSinkCache::iterator end = logSinkCache.end();
		ev_tstamp threshold = ev_now(getLoop()) - sinkFlushInterval;

		for (it = logSinkCache.begin(); it != end; it++) {
			const LogSinkPtr &sink = it->second;
			if (sink->lastFlushed < threshold) {
				// flush() method is responsible for logging
				sink->flush();
			}
		}

		P_DEBUG("Done flushing sinks that need flushing");
	}


	/****** Utility functions ******/

	void writeArrayMessage(Client *client, StaticString args[], unsigned int argsCount) {
		char headerBuf[sizeof(boost::uint16_t)];
		unsigned int outputSize = ArrayMessage::outputSize(argsCount);
		SmallVector<StaticString, 8> output;

		output.resize(outputSize);
		ArrayMessage::generate(args, argsCount, headerBuf, &output[0], outputSize);

		unsigned int bufferSize = 0;
		for (unsigned int i = 0; i < outputSize; i++) {
			bufferSize += output[i].size();
		}

		MemoryKit::mbuf buffer(mbuf_get_with_size(&getContext()->mbuf_pool, bufferSize));
		char *pos = buffer.start;
		const char *end = buffer.start + bufferSize;
		for (unsigned int i = 0; i < outputSize; i++) {
			pos = appendData(pos, end, output[i].data(), output[i].size());
		}

		client->output.feed(buffer);
	}

	void sendErrorToClient(Client *client, const StaticString &message) {
		StaticString reply[] = {
			P_STATIC_STRING("status"),
			P_STATIC_STRING("error"),
			message
		};
		writeArrayMessage(client, reply, 3);
	}

	void logErrorAndSendToClient(Client *client, const StaticString &message) {
		SKC_ERROR(client, message);
		sendErrorToClient(client, message);
	}

	void sendOkToClient(Client *client) {
		StaticString reply[] = {
			P_STATIC_STRING("status"),
			P_STATIC_STRING("ok")
		};
		writeArrayMessage(client, reply, 2);
	}

	bool expectingArgumentsCount(Client *client, const vector<StaticString> &args, unsigned int size) {
		if (args.size() == size) {
			return true;
		} else {
			SKC_ERROR(client, "Invalid number of arguments in message (expecting " <<
				size << ", got " << args.size() << ")");
			StaticString reply[] = {
				P_STATIC_STRING("status"),
				P_STATIC_STRING("error"),
				P_STATIC_STRING("Invalid number of arguments in message")
			};
			writeArrayMessage(client, reply, 3);
			disconnect(&client);
			return false;
		}
	}

	bool expectingMinArgumentsCount(Client *client, const vector<StaticString> &args, unsigned int size) {
		if (args.size() >= size) {
			return true;
		} else {
			SKC_ERROR(client, "Invalid number of arguments in message (expecting at least " <<
				size << ", got " << args.size() << ")");
			sendErrorToClient(client, P_STATIC_STRING("Invalid number of arguments in message"));
			if (client->connected()) {
				disconnect(&client);
			}
			return false;
		}
	}

	bool expectingLoggerType(Client *client) {
		if (client->type == Client::LOGGER) {
			return true;
		} else {
			logErrorAndSendToClient(client, "Client not initialized as logger");
			if (client->connected()) {
				disconnect(&client);
			}
			return false;
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
		unsigned int index, const StaticString &defaultValue = StaticString())
	{
		if (index < args.size()) {
			return args[index];
		} else {
			return defaultValue;
		}
	}

	void createTxnId(char *txnId, char **txnIdEnd, unsigned long long timestamp) {
		unsigned int timestampSize;
		char *end;
		// "[timestamp]"
		// Our timestamp is like a Unix timestamp but with minutes
		// resolution instead of seconds. 32 bits will last us for
		// about 8000 years.
		timestampSize = integerToHexatri<unsigned int>(
			timestamp / 1000000 / 60,
			txnId);
		end = txnId + timestampSize;

		// "[timestamp]-"
		*end = '-';
		end++;
		// "[timestamp]-[random id]"
		randomGenerator.generateAsciiString(end, 11);
		end += 11;
		*end = '\0';
		*txnIdEnd = end;
	}

	bool validTxnId(const StaticString &txnId) const {
		// TODO: must contain timestamp
		// TODO: must contain separator
		// TODO: must contain random id
		// TODO: must not be too large
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
		return category == P_STATIC_STRING("requests")
			|| category == P_STATIC_STRING("processes")
			|| category == P_STATIC_STRING("exceptions")
			|| category == P_STATIC_STRING("system_metrics")
			|| category == P_STATIC_STRING("internal_information");
	}

	/**
	 * Given a logSinkCache key, which may contains NULLs, converts it
	 * into something that can be represented as a JSON string. It's not
	 * a perfect representation, but probably good enough for display
	 * purposes.
	 */
	string createJsonKey(const StaticString &key) const {
		return replaceAll(key, P_STATIC_STRING("\0"), P_STATIC_STRING("__"));
	}

	LogSinkPtr openLogFile(Client *client, const StaticString &category) {
		size_t cacheKeySize =
			(sizeof("file:") - 1) +
			category.size();
		char cacheKey[cacheKeySize];
		char *pos = cacheKey;
		const char *end = cacheKey + cacheKeySize;

		pos = appendData(pos, end, P_STATIC_STRING("file:"));
		pos = appendData(pos, end, category);

		LogSinkPtr sink = logSinkCache.get(StaticString(cacheKey, cacheKeySize));
		if (sink == NULL) {
			string dumpFile = dumpDir + "/" + category;
			SKC_DEBUG(client, "Creating dump file: " << dumpFile);
			sink = boost::make_shared<FileSink>(this, dumpFile);
			sink->opened = 1;
			logSinkCache.set(StaticString(cacheKey, cacheKeySize), sink);
		} else {
			sink->opened++;
		}
		return sink;
	}

	LogSinkPtr openRemoteSink(const StaticString &unionStationKey, const string &nodeName,
		const string &category)
	{
		size_t cacheKeySize =
			(sizeof("remote:") - 1) +
			unionStationKey.size() +
			1 + // null
			nodeName.size() +
			1 + // null
			category.size();
		char cacheKey[cacheKeySize];
		char *pos = cacheKey;
		const char *end = cacheKey + cacheKeySize;

		pos = appendData(pos, end, P_STATIC_STRING("remote:"));
		pos = appendData(pos, end, unionStationKey);
		pos = appendData(pos, end, "\0", 1);
		pos = appendData(pos, end, nodeName);
		pos = appendData(pos, end, "\0", 1);
		pos = appendData(pos, end, category);

		LogSinkPtr sink(logSinkCache.get(StaticString(cacheKey, cacheKeySize)));
		if (sink == NULL) {
			sink = boost::make_shared<RemoteSink>(this, unionStationKey,
				nodeName, category);
			sink->opened = 1;
			logSinkCache.set(StaticString(cacheKey, cacheKeySize), sink);
		} else {
			sink->opened++;
		}
		return sink;
	}

	/**
	 * Close the given transaction, potentially flushing its data to a sink.
	 */
	void closeTransaction(Client *client, const TransactionPtr &transaction) {
		if (!transaction->isDiscarded() && passesFilter(transaction)) {
			LogSinkPtr logSink;
			if (devMode) {
				logSink = openLogFile(client, transaction->getCategory());
			} else {
				logSink = openRemoteSink(transaction->getUnionStationKey(),
					transaction->getNodeName(), transaction->getCategory());
			}
			P_DEBUG("Closing transaction " << transaction->getTxnId() <<
				": appending " << transaction->getBody().size() << " bytes "
				"to sink " << logSink->inspect());
			logSink->append(transaction);
			closeLogSink(logSink);
		}
	}

	/**
	 * Decrement the reference count on the given log sink. When the refcount hits 0,
	 * it's not actually deleted from memory; instead it's cached for later use. A
	 * garbage collection run periodically cleans up log sinks that have zero
	 * references.
	 */
	void closeLogSink(const LogSinkPtr &logSink) {
		logSink->opened--;
		assert(logSink->opened >= 0);
		logSink->lastClosed = ev_now(getLoop());
	}

	void writeLogEntry(Client *client, const TransactionPtr &transaction,
		const StaticString &timestamp, const StaticString &data, bool ack)
	{
		if (transaction->isDiscarded()) {
			return;
		}
		if (OXT_UNLIKELY(!validLogContent(data))) {
			SKC_ERROR(client, "Log entry data contains an invalid character");
			if (ack && client != NULL) {
				sendErrorToClient(client, "Log entry data contains an invalid character");
				disconnect(&client);
			}
			return;
		}
		if (OXT_UNLIKELY(!validTimestamp(timestamp))) {
			SKC_ERROR(client, "Log entry timestamp is invalid");
			if (ack && client != NULL) {
				sendErrorToClient(client, "Log entry timestamp is invalid");
				disconnect(&client);
			}
			return;
		}

		transaction->append(timestamp, data);
	}

	void writeDetachEntry(Client *client, const TransactionPtr &transaction, bool ack) {
		char timestamp[2 * sizeof(unsigned long long) + 1];
		// Must use System::getUsec() here instead of ev_now() because the
		// precision of the time is very important.
		unsigned int size = integerToHexatri<unsigned long long>(
			SystemTime::getUsec(), timestamp);
		writeDetachEntry(client, transaction, StaticString(timestamp, size), ack);
	}

	void writeDetachEntry(Client *client, const TransactionPtr &transaction,
		const StaticString &timestamp, bool ack)
	{
		writeLogEntry(client, transaction, timestamp, P_STATIC_STRING("DETACH"), ack);
	}

	bool passesFilter(const TransactionPtr &transaction) {
		StaticString filters(transaction->getFilters());
		if (filters.empty()) {
			return true;
		}

		StaticString body   = transaction->getBody();
		const char *current = filters.data();
		const char *end     = filters.data() + filters.size();
		bool result         = true;
		FilterSupport::ContextFromLog ctx(body);

		// 'filters' may contain multiple filter sources, separated
		// by '\1' characters. Process each.
		while (current < end && result) {
			StaticString tmp(current, end - current);
			size_t pos = tmp.find('\1');
			if (pos == string::npos) {
				pos = tmp.size();
			}

			StaticString source(current, pos);
			FilterSupport::Filter &filter = compileFilter(source);
			result = filter.run(ctx);

			current = tmp.data() + pos + 1;
		}
		return result;
	}

	FilterSupport::Filter &compileFilter(const StaticString &source) {
		// TODO: garbage collect filters based on time
		FilterSupport::FilterPtr filter = filters.get(source);
		if (filter == NULL) {
			filter = boost::make_shared<FilterSupport::Filter>(source);
			filters.set(source, filter);
		}
		return *filter;
	}

protected:
	virtual void reinitializeClient(Client *client, int fd) {
		ParentClass::reinitializeClient(client, fd);
		client->arrayReader.setMaxSize(1024 * 16);
		client->scalarReader.setMaxSize(1024 * 1024);
		client->state = Client::READING_AUTH_USERNAME;
		client->type = Client::UNINITIALIZED;
	}

	virtual void deinitializeClient(Client *client) {
		client->arrayReader.reset();
		client->scalarReader.reset();
		client->nodeName.clear();

		set<string>::const_iterator s_it;
		set<string>::const_iterator s_end = client->openTransactions.end();

		// Close any transactions that this client had opened.
		for (s_it = client->openTransactions.begin(); s_it != s_end; s_it++) {
			const string &txnId = *s_it;
			TransactionPtr transaction = transactions.get(txnId);
			if (OXT_UNLIKELY(transaction == NULL)) {
				P_BUG("client->openTransactions is not a subset of this->transactions!");
			}

			if (transaction->crashProtectEnabled()) {
				writeDetachEntry(client, transaction, false);
			} else {
				transaction->discard();
			}
			transaction->unref();
			if (transaction->getRefCount() == 0) {
				transactions.remove(transaction->getTxnId());
				closeTransaction(client, transaction);
			}
		}
		client->openTransactions.clear();

		client->logCommandParams.transaction.reset();
		client->logCommandParams.timestamp.clear();

		ParentClass::deinitializeClient(client);
	}

	virtual void onClientAccepted(Client *client) {
		beginHandshake(client);
	}

	virtual Channel::Result onClientDataReceived(Client *client, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		if (buffer.empty()) {
			disconnect(&client);
			return Channel::Result(0, true);
		}

		switch (client->state) {
		case Client::READING_AUTH_USERNAME:
			return onAuthUsernameDataReceived(client, buffer, errcode);
		case Client::READING_AUTH_PASSWORD:
			return onAuthPasswordDataReceived(client, buffer, errcode);
		case Client::READING_MESSAGE:
			return onMessageDataReceived(client, buffer, errcode);
		case Client::READING_MESSAGE_BODY:
			return onMessageBodyDataReceived(client, buffer, errcode);
		default:
			P_BUG("Unknown state " << client->state);
			return Channel::Result(0, false); // Never reached
		}
	}

	virtual void onShutdown(bool forceDisconnect) {
		gcTimer.stop();
		flushTimer.stop();
		ParentClass::onShutdown(forceDisconnect);
	}

public:
	Controller(ServerKit::Context *context, const VariantMap &options = VariantMap())
		: ServerKit::BaseServer<Controller, Client>(context),
		  username(options.get("ust_router_username", false, "")),
		  password(options.get("ust_router_password", false, "")),
		  dumpDir(options.get("ust_router_dump_dir", false, "/tmp")),
		  defaultNodeName(options.get("ust_router_default_node_name", false, "")),
		  devMode(options.getBool("ust_router_dev_mode", false, false)),
		  remoteSender(
		      options.get("union_station_gateway_address", false, DEFAULT_UNION_STATION_GATEWAY_ADDRESS),
		      options.getInt("union_station_gateway_port", false, DEFAULT_UNION_STATION_GATEWAY_PORT),
		      options.get("union_station_gateway_cert", false, ""),
		      options.get("union_station_proxy_address", false, "")),
		  gcTimer(getLoop()),
		  flushTimer(getLoop())
	{
		if (defaultNodeName.empty()) {
			defaultNodeName = getHostName();
		}

		gcTimer.set<Controller, &Controller::garbageCollect>(this);
		gcTimer.start(GARBAGE_COLLECTION_TIMEOUT, GARBAGE_COLLECTION_TIMEOUT);

		int sinkFlushTimerInterval = options.getInt("analytics_sink_flush_timer_interval", false, 5);
		sinkFlushInterval = options.getInt("analytics_sink_flush_interval", false, 0);
		flushTimer.set<Controller, &Controller::flushSomeSinks>(this);
		flushTimer.start(sinkFlushTimerInterval, sinkFlushTimerInterval);
	}

	virtual StaticString getServerName() const {
		return P_STATIC_STRING("UstRouter");
	}

	virtual unsigned int getClientName(const Client *client, char *buf, size_t size) const {
		char *pos = buf;
		const char *end = buf + size - 1;
		pos = appendData(pos, end, P_STATIC_STRING("UstRtr."));
		pos += uintToString(client->number, pos, end - pos);
		*pos = '\0';
		return pos - buf;
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc = ParentClass::inspectStateAsJson();
		doc["dev_mode"] = devMode;
		doc["log_sink_cache"] = inspectLogSinkCacheStateAsJson();
		doc["transactions"] = inspectTransactionsStateAsJson();
		if (devMode) {
			doc["dump_dir"] = dumpDir;
		} else {
			doc["remote_sender"] = remoteSender.inspectStateAsJson();
		}
		doc["default_node_name"] = defaultNodeName;
		return doc;
	}

	virtual Json::Value inspectClientStateAsJson(const Client *client) const {
		Json::Value doc = ParentClass::inspectClientStateAsJson(client);
		doc["state"] = client->getStateName();
		doc["type"] = client->getTypeName();
		doc["node_name"] = client->nodeName;
		doc["open_transactions_count"] = Json::UInt(client->openTransactions.size());

		Json::Value openTransactions(Json::arrayValue);
		foreach (string txnId, client->openTransactions) {
			openTransactions.append(txnId);
		}
		doc["open_transactions"] = openTransactions;

		return doc;
	}

	Json::Value inspectLogSinkCacheStateAsJson() const {
		Json::Value doc(Json::objectValue);
		LogSinkCache::const_iterator it;
		LogSinkCache::const_iterator end = logSinkCache.end();
		for (it = logSinkCache.begin(); it != end; it++) {
			const LogSinkPtr &logSink = it->second;
			doc[createJsonKey(it->first)] = logSink->inspectStateAsJson();
		}
		return doc;
	}

	Json::Value inspectTransactionsStateAsJson() const {
		Json::Value doc(Json::objectValue);
		TransactionMap::const_iterator it;
		TransactionMap::const_iterator end = transactions.end();
		for (it = transactions.begin(); it != end; it++) {
			const TransactionPtr &transaction = it->second;
			doc[it->first.toString()] = transaction->inspectStateAsJson();
		}
		return doc;
	}
};


inline struct ev_loop *
Controller_getLoop(Controller *controller) {
	return controller->getLoop();
}

inline RemoteSender &
Controller_getRemoteSender(Controller *controller) {
	return controller->remoteSender;
}


} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_CONTROLLER_H_ */
