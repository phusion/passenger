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
#ifndef _PASSENGER_EVENTED_MESSAGE_SERVER_H_
#define _PASSENGER_EVENTED_MESSAGE_SERVER_H_

#include <boost/shared_ptr.hpp>
#include <ev++.h>
#include <cstdarg>
#include "EventedServer.h"
#include "MessageReadersWriters.h"
#include "AccountsDatabase.h"
#include "Constants.h"
#include "Utils.h"

namespace Passenger {

using namespace boost;

/* This source file follows the security guidelines written in Account.h. */

class EventedMessageServer: public EventedServer {
protected:
	/******** Types ********/
	
	enum State {
		MS_READING_USERNAME,
		MS_READING_PASSWORD,
		MS_READING_MESSAGE,
		MS_PROCESSING_MESSAGE,
		MS_DISCONNECTED
	};
	
	struct Context {
		State state;
		AccountPtr account;
		
		ev::timer authenticationTimer;
		ScalarMessage scalarReader;
		ArrayMessage arrayReader;
		string username;
		
		Context(EventedServer *server)
			: authenticationTimer(server->getLoop())
		{ }
		
		~Context() {
			/* Its buffer might contain password data so make sure
			 * it's properly zeroed out. */
			scalarReader.reset(true);
		}
	};
	
	struct Client: public EventedServer::Client {
		Context messageServer;
		
		Client(EventedServer *server)
			: EventedServer::Client(server),
			  messageServer(server)
		{
			messageServer.state = MS_READING_USERNAME;
		}
		
		void _authenticationTimedOut(ev::timer &t, int revents) {
			((EventedMessageServer *) server)->authenticationTimedOut(
				static_pointer_cast<Client>(shared_from_this()));
		}
	};
	
	typedef shared_ptr<Client> ClientPtr;
	friend class Client;
	
	AccountsDatabasePtr accountsDatabase;
	
	
	/******** Overrided hooks and methods ********/
	
	virtual EventedServer::ClientPtr createClient() {
		return ClientPtr(new Client(this));
	}
	
	virtual void disconnect(const EventedServer::ClientPtr &_client, bool force = true) {
		ClientPtr client = static_pointer_cast<Client>(_client);
		EventedServer::disconnect(client, true);
		client->messageServer.state = MS_DISCONNECTED;
	}
	
	virtual void onNewClient(const EventedServer::ClientPtr &_client) {
		ClientPtr client = static_pointer_cast<Client>(_client);
		Context *context = &client->messageServer;
		
		context->authenticationTimer.set
			<Client, &Client::_authenticationTimedOut>(client.get());
		context->authenticationTimer.start(10);
		
		context->arrayReader.reserve(5);
		context->scalarReader.setMaxSize(MESSAGE_SERVER_MAX_USERNAME_SIZE);
		
		writeArrayMessage(client, "version", protocolVersion(), NULL);
	}
	
	virtual void onClientReadable(const EventedServer::ClientPtr &_client) {
		ClientPtr client = static_pointer_cast<Client>(_client);
		this_thread::disable_syscall_interruption dsi;
		int i = 0;
		bool done = false;
		
		// read() from the client at most 100 times on every read readiness event
		// in order to give other events the chance to be processed.
		while (i < 100 && !done) {
			char buf[1024 * 4];
			ssize_t ret;
			
			ret = syscalls::read(client->fd, buf, sizeof(buf));
			if (ret == -1) {
				if (errno != EAGAIN) {
					int e = errno;
					disconnect(client, true);
					logSystemError(client, "Cannot read data from client", e);
				}
				done = true;
			} else if (ret == 0) {
				done = true;
				disconnect(client);
				onEndOfStream(client);
			} else {
				onDataReceived(client, buf, ret);
			}
			i++;
			done = done || (client->state != Client::ES_CONNECTED &&
				client->state != Client::ES_WRITES_PENDING);
		}
	}
	
	
	/******** New EventedMessageServer overridable hooks and API methods ********/
	
	virtual void onClientAuthenticated(const ClientPtr &client) {
		// Do nothing.
	}
	
	virtual bool onMessageReceived(const ClientPtr &client, const vector<StaticString> &args) {
		return true;
	}
	
	virtual void onEndOfStream(const ClientPtr &client) {
		// Do nothing.
	}
	
	virtual pair<size_t, bool> onOtherDataReceived(const ClientPtr &client,
		const char *data, size_t size)
	{
		abort();
	}
	
	virtual const char *protocolVersion() const {
		return "1";
	}
	
	void writeArrayMessage(const EventedServer::ClientPtr &client, const char *name, ...) {
		va_list ap;
		unsigned int count = 0;
		
		va_start(ap, name);
		while (va_arg(ap, const char *) != NULL) {
			count++;
		}
		va_end(ap);
		
		StaticString args[count + 1];
		unsigned int i = 1;
		
		args[0] = name;
		va_start(ap, name);
		while (true) {
			const char *arg = va_arg(ap, const char *);
			if (arg != NULL) {
				args[i] = arg;
				i++;
			} else {
				break;
			}
		}
		va_end(ap);
		
		writeArrayMessage(client, args, count + 1);
	}
	
	void writeArrayMessage(const EventedServer::ClientPtr &client, StaticString args[],
		unsigned int count)
	{
		char headerBuf[sizeof(uint16_t)];
		unsigned int outSize = ArrayMessage::outputSize(count);
		StaticString out[outSize];
		
		ArrayMessage::generate(args, count, headerBuf, out, outSize);
		write(client, out, outSize);
	}

private:
	void onDataReceived(const ClientPtr &client, char *data, size_t size) {
		size_t consumed = 0;
		Context *context = &client->messageServer;
		
		while (consumed < size && context->state != MS_DISCONNECTED) {
			char *current = data + consumed;
			size_t rest = size - consumed;
			
			switch (context->state) {
			case MS_READING_USERNAME:
				consumed += context->scalarReader.feed(current, rest);
				if (context->scalarReader.hasError()) {
					writeArrayMessage(client,
						"The supplied username is too long.",
						NULL);
					disconnect(client);
				} else if (context->scalarReader.done()) {
					context->username = context->scalarReader.value();
					context->scalarReader.reset();
					context->scalarReader.setMaxSize(MESSAGE_SERVER_MAX_PASSWORD_SIZE);
					context->state = MS_READING_PASSWORD;
				}
				break;
				
			case MS_READING_PASSWORD: {
				size_t locallyConsumed;
				
				locallyConsumed = context->scalarReader.feed(current, rest);
				consumed += locallyConsumed;
				
				// The buffer contains password data so make sure we zero
				// it out when we're done.
				MemZeroGuard passwordGuard(current, locallyConsumed);
				
				if (context->scalarReader.hasError()) {
					context->scalarReader.reset(true);
					writeArrayMessage(client,
						"The supplied password is too long.",
						NULL);
					disconnect(client);
				} else if (context->scalarReader.done()) {
					context->authenticationTimer.stop();
					context->account = accountsDatabase->authenticate(
						context->username, context->scalarReader.value());
					passwordGuard.zeroNow();
					context->username.clear();
					if (context->account) {
						context->scalarReader.reset(true);
						context->state = MS_READING_MESSAGE;
						writeArrayMessage(client, "ok", NULL);
						onClientAuthenticated(client);
					} else {
						context->scalarReader.reset(true);
						writeArrayMessage(client,
							"Invalid username or password.",
							NULL);
						disconnect(client);
					}
				}
				break;
			}
			
			case MS_READING_MESSAGE:
				consumed += context->arrayReader.feed(current, rest);
				if (context->arrayReader.hasError()) {
					disconnect(client);
				} else if (context->arrayReader.done()) {
					context->state = MS_PROCESSING_MESSAGE;
					if (context->arrayReader.value().empty()) {
						logError(client, "Client sent an empty message.");
						disconnect(client);
					} else if (onMessageReceived(client, context->arrayReader.value())
					   && context->state == MS_PROCESSING_MESSAGE) {
						context->state = MS_READING_MESSAGE;
					}
					context->arrayReader.reset();
				}
				break;
			
			case MS_PROCESSING_MESSAGE: {
				pair<size_t, bool> ret = onOtherDataReceived(client, current, rest);
				consumed += ret.first;
				if (ret.second && context->state == MS_PROCESSING_MESSAGE) {
					context->state = MS_READING_MESSAGE;
				}
				break;
			}
			
			default:
				// Never reached.
				abort();
			}
		}
	}
	
	void authenticationTimedOut(const ClientPtr &client) {
		disconnect(client);
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
