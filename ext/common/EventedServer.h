/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
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
#ifndef _PASSENGER_EVENTED_SERVER_H_
#define _PASSENGER_EVENTED_SERVER_H_

#include <ev++.h>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/macros.hpp>
#include <algorithm>
#include <string>
#include <set>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cerrno>
#include "EventedClient.h"
#include "FileDescriptor.h"
#include "StaticString.h"
#include "Logging.h"
#include "Utils/ScopeGuard.h"
#include "Utils/StrIntUtils.h"

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


/**
 * A base class for writing single-threaded, evented servers that use non-blocking I/O.
 * It uses libev for its event loop. EventedServer handles much of the situps regarding
 * client connection management and output buffering and tries to make it easy to
 * implement a zero-copy architecture.
 *
 * <h2>Basic usage</h2>
 * Derived classes can override the onClientReadable() method, which is called every time
 * a specific client becomes readable. It is passed a Client object which contains information
 * about the client, such as its file descriptor. One can use the read() system call in
 * that method to receive data from the client. Please note that client file descriptors
 * are always set to non-blocking mode so you need to handle this gracefully.
 *
 * EventedServer provides the write() method for sending data to the client. This method
 * will attempt to send the data to the client immediately; if it fails with EAGAIN then
 * EventedServer will take care of scheduling the send at a later time when the client
 * is ready again to receive data.
 *
 * To disconnect the client, call disconnect(). The connection might not be actually
 * closed until all pending outgoing data have been sent out, but all the gory details
 * is taken care of for you.
 *
 * <h2>Keeping per-client information</h2>
 * If you need to keep per-client information then you can override the createClient()
 * method and make it return an object that's a subclass of EventedServer::Client. This
 * object is passed to onClientReadable(), so in there you can just cast the client object
 * to your subclass.
 *
 * <h2>Client object life times</h2>
 * All client objects are destroyed along with the EventedServer. Disconnecting
 * a client also causes it to be destroyed after the current event handler has
 * run or a short period of time after that.
 */
class EventedServer {
protected:
	typedef set<EventedClient *> ClientSet;

	const ClientSet &getClients() const {
		return clients;
	}

	string getClientName(const EventedClient *client) const {
		return toString(client);
	}

	void logError(const EventedClient *client, const string &message) {
		P_ERROR("Error in client " << getClientName(client) << ": " << message);
	}

	void logSystemError(const EventedClient *client, const string &message, int errorCode) {
		P_ERROR("Error in client " << getClientName(client) << ": " <<
			message << ": " << strerror(errorCode) << " (" << errorCode << ")");
	}

	void logSystemError(const string &message, int errorCode) {
		P_ERROR(message << ": " << strerror(errorCode) << " (" << errorCode << ")");
	}

	virtual EventedClient *createClient(const FileDescriptor &fd) {
		return new EventedClient(loop, fd);
	}

	virtual void onNewClient(EventedClient *client) { }
	virtual void onClientReadable(EventedClient *client) { }

	/**
	 * Called when a client has been disconnected. This may either be triggered
	 * immediately by disconnect() or triggered after pending data has been sent
	 * out. This means that if you call disconnect() from onClientReadable() you
	 * need take care of the possibility that control returns to onClientReadable()
	 * after this method is done.
	 *
	 * Please note that when EventedServer is being destroyed,
	 * onClientDisconnected() is *not* triggered.
	 */
	virtual void onClientDisconnected(EventedClient *client) { }

private:
	struct ev_loop *loop;
	FileDescriptor fd;
	ev::io acceptWatcher;
	ClientSet clients;

	void removeClient(EventedClient *client) {
		clients.erase(client);
	}

	void freeAllClients() {
		ClientSet::iterator it;
		ClientSet::iterator end = clients.end();

		for (it = clients.begin(); it != end; it++) {
			(*it)->unref();
		}
		clients.clear();
	}

	static void _onReadable(EventedClient *client) {
		EventedServer *server = (EventedServer *) client->userData;
		client->ref();
		ScopeGuard guard(boost::bind(&EventedClient::unref, client));
		server->onClientReadable((EventedClient *) client);
	}

	static void _onDisconnect(EventedClient *client) {
		EventedServer *server = (EventedServer *) client->userData;
		ScopeGuard guard1(boost::bind(&EventedClient::unref, client));

		client->ref();
		ScopeGuard guard2(boost::bind(&EventedClient::unref, client));

		server->removeClient(client);
		server->onClientDisconnected((EventedClient *) client);
	}

	static void _onDetach(EventedClient *client) {
		EventedServer *server = (EventedServer *) client->userData;
		ScopeGuard guard1(boost::bind(&EventedClient::unref, client));

		client->ref();
		ScopeGuard guard2(boost::bind(&EventedClient::unref, client));

		server->removeClient(client);
	}

	static void _onSystemError(EventedClient *client, const string &message, int code) {
		EventedServer *server = (EventedServer *) client->userData;
		server->logSystemError(client, message, code);
	}

	void exceptionThrownWhileInitializingClient(EventedClient *client, ClientSet::iterator it) {
		if (!client->ioAllowed()) {
			// onNewClient() disconnected or detached the
			// client, so client refcount == 1
			client->unref();
		} else {
			// client refcount == 2
			client->unref();
			client->unref();
			clients.erase(it);
		}
		// Now client refcount == 0
	}

	void onAcceptable(ev::io &w, int revents) {
		this_thread::disable_syscall_interruption dsi;
		int i = 0;
		bool done = false;

		// Accept at most 10 connections on every accept readiness event
		// in order to give other events the chance to be processed.
		while (i < 10 && !done) {
			// Reserve enough space to hold both a Unix domain socket
			// address and an IP socket address.
			union {
				struct sockaddr_un local;
				struct sockaddr_in inet;
			} addr;
			socklen_t len = sizeof(addr);

			int clientfd = syscalls::accept(fd, (struct sockaddr *) &addr, &len);
			if (clientfd == -1) {
				if (errno != EAGAIN && errno != EWOULDBLOCK) {
					int e = errno;
					logSystemError("Cannot accept new client", e);
				}
				done = true;
			} else {
				FileDescriptor clientfdGuard(clientfd);
				int optval = 1;

				setNonBlocking(clientfdGuard);
				syscalls::setsockopt(clientfd, SOL_SOCKET, SO_KEEPALIVE,
					&optval, sizeof(optval));

				EventedClient *client = createClient(clientfdGuard);
				client->onReadable    = _onReadable;
				client->onDisconnect  = _onDisconnect;
				client->onDetach      = _onDetach;
				client->onSystemError = _onSystemError;
				client->userData      = this;
				client->notifyReads(true);

				pair<ClientSet::iterator, bool> p = clients.insert(client);

				client->ref();
				// client refcount == 2
				{
					ScopeGuard g(boost::bind(
						&EventedServer::exceptionThrownWhileInitializingClient,
						this,
						client,
						p.first));
					onNewClient(client);
					g.clear();
					// If exception occurred: client refcount == 0
				}

				/* No exception occured.
				 * If onNewClient() disconnected or detached the client:
				 *     client refcount == 1
				 *     We want the refcount to become 0.
				 * If not:
				 *     client refcount == 2
				 *     We want the refcount to become 1.
				 */
				client->unref();
			}
			i++;
		}
	}

public:
	EventedServer(struct ev_loop *_loop, FileDescriptor serverFd)
		: loop(_loop),
		  acceptWatcher(_loop)
	{
		fd = serverFd;
		setNonBlocking(serverFd);
		acceptWatcher.set<EventedServer, &EventedServer::onAcceptable>(this);
		acceptWatcher.start(fd, ev::READ);
	}

	virtual ~EventedServer() {
		freeAllClients();
	}

	struct ev_loop *getLoop() const {
		return loop;
	}

	FileDescriptor getServerFd() const {
		return fd;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_EVENTED_SERVER_H_ */
