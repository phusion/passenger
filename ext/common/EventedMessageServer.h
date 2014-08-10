/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010, 2011, 2012 Phusion
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
#ifndef _PASSENGER_EVENTED_MESSAGE_SERVER_H_
#define _PASSENGER_EVENTED_MESSAGE_SERVER_H_

#include <boost/shared_ptr.hpp>
#include <ev++.h>
#include <cstdarg>
#include <cstdlib>
#include <EventedServer.h>
#include <MessageReadersWriters.h>
#include <AccountsDatabase.h>
#include <Constants.h>
#include <Utils.h>
#include <Utils/SmallVector.h>

namespace Passenger {

using namespace boost;

/* This source file follows the security guidelines written in Account.h. */

struct EventedMessageClientContext {
	enum State {
		MS_READING_USERNAME,
		MS_READING_PASSWORD,
		MS_READING_MESSAGE,
		MS_PROCESSING_MESSAGE
	};

	State state;
	AccountPtr account;

	ev::timer authenticationTimer;
	ScalarMessage scalarReader;
	ArrayMessage arrayReader;
	string username;

	EventedMessageClientContext() {
		state = MS_READING_USERNAME;
	}

	~EventedMessageClientContext() {
		/* Its buffer might contain password data so make sure
		 * it's properly zeroed out. */
		scalarReader.reset(true);
	}

	const char *getStateName() const {
		switch (state) {
		case MS_READING_USERNAME:
			return "MS_READING_USERNAME";
		case MS_READING_PASSWORD:
			return "MS_READING_PASSWORD";
		case MS_READING_MESSAGE:
			return "MS_READING_MESSAGE";
		case MS_PROCESSING_MESSAGE:
			return "MS_PROCESSING_MESSAGE";
		default:
			return "unknown";
		}
	}
};

class EventedMessageClient: public EventedClient {
public:
	EventedMessageClientContext messageServer;

	EventedMessageClient(struct ev_loop *loop, const FileDescriptor &fd)
		: EventedClient(loop, fd)
	{
		messageServer.authenticationTimer.set(loop);
	}

	void writeArrayMessage(const char *name, ...) {
		va_list ap;
		SmallVector<StaticString, 10> args;
		const char *arg;

		args.push_back(name);
		va_start(ap, name);
		while ((arg = va_arg(ap, const char *)) != NULL) {
			args.push_back(arg);
		}
		va_end(ap);

		writeArrayMessage(&args[0], args.size());
	}

	void writeArrayMessage(StaticString args[], unsigned int count) {
		char headerBuf[sizeof(uint16_t)];
		unsigned int outSize = ArrayMessage::outputSize(count);
		SmallVector<StaticString, 10> out;
		out.reserve(outSize);

		ArrayMessage::generate(args, count, headerBuf, &out[0], outSize);
		write(&out[0], outSize);
	}
};

/**
 * Note when overriding onNewClient: call the parent method first! It does
 * some initialization but might disconnect the client if that initialization
 * fails. The override should check for this.
 */
class EventedMessageServer: public EventedServer {
protected:
	AccountsDatabasePtr accountsDatabase;


	/******** Overrided hooks and methods ********/

	virtual EventedClient *createClient(const FileDescriptor &fd) {
		return new EventedMessageClient(getLoop(), fd);
	}

	virtual void onNewClient(EventedClient *_client) {
		EventedMessageClient *client = (EventedMessageClient *) _client;
		EventedMessageClientContext *context = &client->messageServer;

		context->authenticationTimer.set
			<&EventedMessageServer::onAuthenticationTimeout>(client);
		context->authenticationTimer.start(10);

		context->arrayReader.reserve(5);
		context->scalarReader.setMaxSize(MESSAGE_SERVER_MAX_USERNAME_SIZE);

		client->writeArrayMessage("version", protocolVersion(), NULL);
	}

	virtual void onClientReadable(EventedClient *_client) {
		EventedMessageClient *client = (EventedMessageClient *) _client;
		this_thread::disable_syscall_interruption dsi;
		int i = 0;
		bool done = false;

		// read() from the client at most 10 times on every read readiness event
		// in order to give other events the chance to be processed.
		while (i < 10 && !done) {
			char buf[1024 * 8];
			ssize_t ret;

			ret = syscalls::read(client->fd, buf, sizeof(buf));
			if (ret == -1) {
				if (errno != EAGAIN) {
					int e = errno;
					client->disconnect(true);
					logSystemError(client, "Cannot read data from client", e);
				}
				done = true;
			} else if (ret == 0) {
				done = true;
				ScopeGuard guard(boost::bind(&EventedClient::disconnect,
					client, false));
				onEndOfStream(client);
			} else {
				onDataReceived(client, buf, ret);
			}
			i++;
			done = done || !client->ioAllowed();
		}
	}


	/******** New EventedMessageServer overridable hooks and API methods ********/

	virtual void onClientAuthenticated(EventedMessageClient *client) {
		// Do nothing.
	}

	virtual bool onMessageReceived(EventedMessageClient *client, const vector<StaticString> &args) {
		return true;
	}

	virtual void onEndOfStream(EventedMessageClient *client) {
		// Do nothing.
	}

	virtual pair<size_t, bool> onOtherDataReceived(EventedMessageClient *client,
		const char *data, size_t size)
	{
		abort();
	}

	virtual const char *protocolVersion() const {
		return "1";
	}

	void discardReadData() {
		readDataDiscarded = true;
	}

private:
	bool readDataDiscarded;

	static void onAuthenticationTimeout(ev::timer &t, int revents) {
		EventedMessageClient *client = (EventedMessageClient *) t.data;
		client->disconnect();
	}

	void onDataReceived(EventedMessageClient *client, char *data, size_t size) {
		EventedMessageClientContext *context = &client->messageServer;
		size_t consumed = 0;

		readDataDiscarded = false;
		while (consumed < size && client->ioAllowed() && !readDataDiscarded) {
			char *current = data + consumed;
			size_t rest = size - consumed;

			switch (context->state) {
			case EventedMessageClientContext::MS_READING_USERNAME:
				consumed += context->scalarReader.feed(current, rest);
				if (context->scalarReader.hasError()) {
					client->writeArrayMessage(
						"The supplied username is too long.",
						NULL);
					client->disconnect();
				} else if (context->scalarReader.done()) {
					context->username = context->scalarReader.value();
					context->scalarReader.reset();
					context->scalarReader.setMaxSize(MESSAGE_SERVER_MAX_PASSWORD_SIZE);
					context->state = EventedMessageClientContext::MS_READING_PASSWORD;
				}
				break;

			case EventedMessageClientContext::MS_READING_PASSWORD: {
				size_t locallyConsumed;

				locallyConsumed = context->scalarReader.feed(current, rest);
				consumed += locallyConsumed;

				// The buffer contains password data so make sure we zero
				// it out when we're done.
				MemZeroGuard passwordGuard(current, locallyConsumed);

				if (context->scalarReader.hasError()) {
					context->scalarReader.reset(true);
					client->writeArrayMessage(
						"The supplied password is too long.",
						NULL);
					client->disconnect();
				} else if (context->scalarReader.done()) {
					context->authenticationTimer.stop();
					context->account = accountsDatabase->authenticate(
						context->username, context->scalarReader.value());
					passwordGuard.zeroNow();
					context->username.clear();
					if (context->account) {
						context->scalarReader.reset(true);
						context->state = EventedMessageClientContext::MS_READING_MESSAGE;
						client->writeArrayMessage("ok", NULL);
						onClientAuthenticated(client);
					} else {
						context->scalarReader.reset(true);
						client->writeArrayMessage(
							"Invalid username or password.",
							NULL);
						client->disconnect();
					}
				}
				break;
			}

			case EventedMessageClientContext::MS_READING_MESSAGE:
				consumed += context->arrayReader.feed(current, rest);
				if (context->arrayReader.hasError()) {
					client->disconnect();
				} else if (context->arrayReader.done()) {
					context->state = EventedMessageClientContext::MS_PROCESSING_MESSAGE;
					if (context->arrayReader.value().empty()) {
						logError(client, "Client sent an empty message.");
						client->disconnect();
					} else if (onMessageReceived(client, context->arrayReader.value())
					   && context->state == EventedMessageClientContext::MS_PROCESSING_MESSAGE) {
						context->state = EventedMessageClientContext::MS_READING_MESSAGE;
					}
					context->arrayReader.reset();
				}
				break;

			case EventedMessageClientContext::MS_PROCESSING_MESSAGE: {
				pair<size_t, bool> ret = onOtherDataReceived(client, current, rest);
				consumed += ret.first;
				if (ret.second && context->state == EventedMessageClientContext::MS_PROCESSING_MESSAGE) {
					context->state = EventedMessageClientContext::MS_READING_MESSAGE;
				}
				break;
			}

			default:
				// Never reached.
				abort();
			}
		}
	}

public:
	EventedMessageServer(struct ev_loop *loop, FileDescriptor fd,
		const AccountsDatabasePtr &accountsDatabase)
		: EventedServer(loop, fd)
	{
		this->accountsDatabase = accountsDatabase;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_EVENTED_MESSAGE_SERVER_H_ */
