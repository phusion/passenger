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
#include "EventedServer.h"
#include "MessageReadersWriters.h"
#include "Utils.h"

namespace Passenger {

using namespace boost;


class EventedMessageServer: public EventedServer {
protected:
	enum State {
		MS_READING_USERNAME,
		MS_READING_PASSWORD,
		MS_READING_MESSAGE,
		MS_PROCESSING_MESSAGE,
		MS_DISCONNECTED
	};
	
	struct Context {
		State state;
		ev::timer authenticationTimer;
		ScalarReader scalarReader;
		ArrayReader arrayReader;
		string username;
		string password;
		
		Context(EventedServer *server)
			: authenticationTimer(server->getLoop())
		{
			arrayReader.reserve(5);
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
		client->messageServer.authenticationTimer.set
			<Client, &Client::_authenticationTimedOut>(client.get());
		client->messageServer.authenticationTimer.start(10);
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
	
	void onDataReceived(const ClientPtr &client, const char *data, size_t size) {
		size_t consumed = 0;
		Context *context = &client->messageServer;
		
		while (consumed < size) {
			const char *current = data + consumed;
			size_t rest = size - consumed;
			
			switch (context->state) {
			case MS_READING_USERNAME:
				consumed += context->scalarReader.feed(current, rest);
				if (context->scalarReader.hasError()) {
					context->authenticationTimer.stop();
					disconnect(client);
				} else if (context->scalarReader.done()) {
					context->username = context->scalarReader.value();
					context->scalarReader.reset();
					context->state = MS_READING_PASSWORD;
				}
				break;
			case MS_READING_PASSWORD:
				consumed += context->scalarReader.feed(current, rest);
				if (context->scalarReader.hasError()) {
					context->authenticationTimer.stop();
					disconnect(client);
				} else if (context->scalarReader.done()) {
					context->authenticationTimer.stop();
					context->password = context->scalarReader.value();
					context->scalarReader.reset();
					if (authenticate(client)) {
						context->state = MS_READING_MESSAGE;
					} else {
						disconnect(client);
					}
				}
				break;
			case MS_READING_MESSAGE:
				consumed += context->arrayReader.feed(current, rest);
				if (context->arrayReader.hasError()) {
					disconnect(client);
				} else if (context->arrayReader.done()) {
					context->state = MS_PROCESSING_MESSAGE;
					if (onMessageReceived(client, context->arrayReader.value())
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
			case MS_DISCONNECTED:
				consumed += rest;
				break;
			default:
				abort();
			}
		}
	}
	
	virtual bool onMessageReceived(const ClientPtr &client, const vector<StaticString> &message) {
		return true;
	}
	
	virtual void onEndOfStream(const ClientPtr &client) { }
	
	virtual pair<size_t, bool> onOtherDataReceived(const ClientPtr &client,
		const char *data, size_t size)
	{
		abort();
	}

private:
	bool authenticate(const ClientPtr &client) const {
		return client->messageServer.username == "foo" &&
			client->messageServer.password == "bar";
	}
	
	void authenticationTimedOut(const ClientPtr &client) {
		disconnect(client);
	}

public:
	EventedMessageServer(struct ev_loop *loop, FileDescriptor fd)
		: EventedServer(loop, fd)
	{ }
};


} // namespace Passenger

#endif /* _PASSENGER_EVENTED_MESSAGE_SERVER_H_ */
