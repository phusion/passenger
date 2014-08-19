/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014 Phusion
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
#ifndef _PASSENGER_SERVER_KIT_SERVER_H_
#define _PASSENGER_SERVER_KIT_SERVER_H_

#include <Utils/sysqueue.h>

#include <boost/cstdint.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/macros.hpp>
#include <vector>
#include <new>
#include <ev++.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <cstdio>

#include <Logging.h>
#include <SafeLibev.h>
#include <ServerKit/Context.h>
#include <ServerKit/Hooks.h>
#include <ServerKit/Client.h>
#include <ServerKit/ClientRef.h>
#include <Utils.h>
#include <Utils/SmallVector.h>
#include <Utils/ScopeGuard.h>
#include <Utils/IOUtils.h>

namespace Passenger {
namespace ServerKit {

using namespace std;
using namespace boost;
using namespace oxt;


// We use 'this' so that the macros work in derived template classes like HttpServer<>.
#define SKS_ERROR(expr)  P_ERROR("[" << this->getServerName() << "] " << expr)
#define SKS_WARN(expr)   P_WARN("[" << this->getServerName() << "] " << expr)
#define SKS_NOTICE(expr) P_NOTICE("[" << this->getServerName() << "] " << expr)
#define SKS_DEBUG(expr)  P_DEBUG("[" << this->getServerName() << "] " << expr)
#define SKS_TRACE(level, expr) P_TRACE(level, "[" << this->getServerName() << "] " << expr)

#define SKC_ERROR(client, expr) SKC_ERROR_FROM_STATIC(this, client, expr)
#define SKC_WARN(client, expr) SKC_WARN_FROM_STATIC(this, client, expr)
#define SKC_DEBUG(client, expr) SKC_DEBUG_FROM_STATIC(this, client, expr)
#define SKC_TRACE(client, level, expr) SKC_TRACE_FROM_STATIC(this, client, level, expr)

#define SKC_ERROR_FROM_STATIC(server, client, expr) \
	do { \
		if (Passenger::_logLevel >= LVL_ERROR) { \
			char _clientName[16]; \
			int _clientNameSize = server->getClientName((client), _clientName, sizeof(_clientName)); \
			P_ERROR("[Client " << StaticString(_clientName, _clientNameSize) << "] " << expr); \
		} \
	} while (0)
#define SKC_WARN_FROM_STATIC(server, client, expr) \
	do { \
		if (Passenger::_logLevel >= LVL_WARN) { \
			char _clientName[16]; \
			int _clientNameSize = server->getClientName((client), _clientName, sizeof(_clientName)); \
			P_WARN("[Client " << StaticString(_clientName, _clientNameSize) << "] " << expr); \
		} \
	} while (0)
#define SKC_DEBUG_FROM_STATIC(server, client, expr) \
	do { \
		if (OXT_UNLIKELY(Passenger::_logLevel >= LVL_DEBUG)) { \
			char _clientName[16]; \
			int _clientNameSize = server->getClientName((client), _clientName, sizeof(_clientName)); \
			P_DEBUG("[Client " << StaticString(_clientName, _clientNameSize) << "] " << expr); \
		} \
	} while (0)
#define SKC_TRACE_FROM_STATIC(server, client, level, expr) \
	do { \
		if (OXT_UNLIKELY(Passenger::_logLevel >= level)) { \
			char _clientName[16]; \
			int _clientNameSize = server->getClientName((client), _clientName, sizeof(_clientName)); \
			P_TRACE(level, "[Client " << StaticString(_clientName, _clientNameSize) << "] " << expr); \
		} \
	} while (0)

/*
start main server
start admin server
main loop

upon receiving exit signal on admin server:
	set all servers to shutdown mode
		- stop accepting new clients
		- allow some clients to finish their request
		- disconnect other clients
	wait until servers are done shutting down (client count == 0)
		exit main loop
*/

/**
 * A highly optimized generic base class for evented socket servers, implementing basic,
 * low-level connection management.
 *
 * ## Features
 *
 * ### Client objects
 *
 * Every connected client is represented by a client object, which inherits from
 * ServerKit::BaseClient. The client object provides input and output, and you can
 * extend with your own fields.
 *
 * Client objects are reference counted, for easy memory management.
 *
 * Creation and destruction is very efficient, because client objects are put on a
 * freelist upon destruction, so that no malloc calls are necessary next time.
 *
 * ### Zero-copy buffers
 *
 * All input is handled in a zero-copy manner, by using the mbuf system.
 *
 * ### Channel I/O abstraction
 *
 * All input is handled through the Channel abstraction, and all output is
 * handled through the FileBufferedFdOutputChannel abstraction.. This makes writing
 * evented servers very easy.
 *
 * ### Multiple listen endpoints
 *
 * The server can listen on multiple server endpoints at the same time (e.g. TCP and
 * Unix domain sockets), up to MAX_ENDPOINTS.
 *
 * ### Automatic backoff when too many file descriptors are active
 *
 * If ENFILES or EMFILES is encountered when accepting new clients, Server will stop
 * accepting new clients for a few seconds so that doesn't keep triggering the error
 * in a busy loop.
 *
 * ### Logging
 *
 * Provides basic logging macros that also log the client name.
 */
template<typename DerivedServer, typename Client = Client>
class BaseServer: public HooksImpl {
public:
	/***** Types *****/
	typedef Client ClientType;
	typedef ClientRef<DerivedServer, Client> ClientRefType;
	STAILQ_HEAD(FreeClientList, Client);
	TAILQ_HEAD(ClientList, Client);

	enum State {
		ACTIVE,
		TOO_MANY_FDS,
		SHUTTING_DOWN,
		FINISHED_SHUTDOWN
	};

	static const unsigned int MAX_ACCEPT_BURST_COUNT = 127;
	static const unsigned int MAX_ENDPOINTS = 4;

	/***** Configuration *****/
	unsigned int acceptBurstCount: 7;
	bool startReadingAfterAccept: 1;
	unsigned int minSpareClients: 12;
	unsigned int freelistLimit: 12;

	/***** Working state and statistics (do not modify) *****/
	State serverState;
	FreeClientList freeClients;
	ClientList activeClients, disconnectedClients;
	unsigned int freeClientCount, activeClientCount, disconnectedClientCount;

private:
	Context *ctx;
	unsigned int nextClientNumber: 28;
	uint8_t nEndpoints: 3;
	bool accept4Available: 1;
	ev::timer acceptResumptionWatcher;
	ev::io endpoints[MAX_ENDPOINTS];


	/***** Private methods *****/

	static void _onAcceptable(EV_P_ ev_io *io, int revents) {
		static_cast<BaseServer *>(io->data)->onAcceptable(io, revents);
	}

	void onAcceptable(ev_io *io, int revents) {
		unsigned int acceptCount = 0;
		bool error = false;
		int fd, errcode;
		Client *client;
		Client *acceptedClients[MAX_ACCEPT_BURST_COUNT];

		assert(serverState == ACTIVE);
		SKS_DEBUG("New clients can be accepted on a server socket");

		for (unsigned int i = 0; i < acceptBurstCount; i++) {
			fd = acceptNonBlockingSocket(io->fd);
			if (fd == -1) {
				error = true;
				errcode = errno;
				break;
			}

			FdGuard guard(fd);
			client = checkoutClientObject();
			TAILQ_INSERT_HEAD(&activeClients, client, nextClient.activeOrDisconnectedClient);
			acceptedClients[acceptCount] = client;
			activeClientCount++;
			acceptCount++;
			client->setConnState(Client::ACTIVE);
			client->number = getNextClientNumber();
			client->input.reinitialize(fd);
			client->output.reinitialize(fd);
			client->reinitialize(fd);
			guard.clear();
		}

		if (acceptCount > 0) {
			SKS_DEBUG(acceptCount << " new client(s) accepted; there are now " <<
				activeClientCount << " active client(s)");
		}
		if (error && errcode != EAGAIN && errcode != EWOULDBLOCK) {
			SKS_ERROR("Cannot accept client: " << strerror(errcode) <<
				" (errno=" << errcode << "). " <<
				"Stop accepting clients for 3 seconds. " <<
				"Current client count: " << activeClientCount);
			serverState = TOO_MANY_FDS;
			acceptResumptionWatcher.start();
			for (uint8_t i = 0; i < nEndpoints; i++) {
				ev_io_stop(ctx->libev->getLoop(), &endpoints[i]);
			}
		}

		onClientsAccepted(acceptedClients, acceptCount);
	}

	void onAcceptResumeTimeout(ev::timer &timer, int revents) {
		assert(serverState == TOO_MANY_FDS);
		SKS_NOTICE("Resuming accepting new clients");
		serverState = ACTIVE;
		for (uint8_t i = 0; i < nEndpoints; i++) {
			ev_io_start(ctx->libev->getLoop(), &endpoints[i]);
		}
		acceptResumptionWatcher.stop();
	}

	int acceptNonBlockingSocket(int serverFd) {
		union {
			struct sockaddr_in inaddr;
			struct sockaddr_un unaddr;
		} u;
		socklen_t addrlen = sizeof(u);

		if (accept4Available) {
			int fd = callAccept4(serverFd,
				(struct sockaddr *) &u,
				&addrlen,
				O_NONBLOCK);
			// FreeBSD returns EINVAL if accept4() is called with invalid flags.
			if (fd == -1 && (errno == ENOSYS || errno == EINVAL)) {
				accept4Available = false;
				return acceptNonBlockingSocket(serverFd);
			} else {
				return fd;
			}
		} else {
			int fd = syscalls::accept(serverFd,
				(struct sockaddr *) &u,
				&addrlen);
			FdGuard guard(fd);
			if (fd == -1) {
				return -1;
			} else {
				try {
					setNonBlocking(fd);
				} catch (const SystemException &e) {
					SKS_DEBUG("Unable to set non-blocking flag on accepted client socket: " <<
						e.what() << " (errno=" << e.code() << ")");
					errno = e.code();
					return -1;
				}
				guard.clear();
				return fd;
			}
		}
	}

	unsigned int getNextClientNumber() {
		return nextClientNumber++;
	}

	Client *checkoutClientObject() {
		// Try to obtain client object from freelist.
		if (!STAILQ_EMPTY(&freeClients)) {
			return checkoutClientObjectFromFreelist();
		} else {
			return createNewClientObject();
		}
	}

	Client *checkoutClientObjectFromFreelist() {
		assert(freeClientCount > 0);
		SKS_TRACE(3, "Checking out client object from freelist (" <<
			freeClientCount << " -> " << (freeClientCount - 1) << ")");
		Client *client = STAILQ_FIRST(&freeClients);
		assert(client->getConnState() == Client::IN_FREELIST);
		client->refcount.store(2, boost::memory_order_relaxed);
		freeClientCount--;
		STAILQ_REMOVE_HEAD(&freeClients, nextClient.freeClient);
		return client;
	}

	Client *createNewClientObject() {
		Client *client;
		SKS_TRACE(3, "Creating new client object");
		try {
			client = new Client(this);
		} catch (const std::bad_alloc &) {
			return NULL;
		}
		onClientObjectCreated(client);
		return client;
	}

	void clientReachedZeroRefcount(Client *client) {
		assert(disconnectedClientCount > 0);
		assert(!TAILQ_EMPTY(&disconnectedClients));

		SKC_TRACE(client, 3, "Client object reached a reference count of 0");
		TAILQ_REMOVE(&disconnectedClients, client, nextClient.activeOrDisconnectedClient);
		disconnectedClientCount--;

		if (addClientToFreelist(client)) {
			SKC_TRACE(client, 3, "Client object added to freelist (" <<
				(freeClientCount - 1) << " -> " << freeClientCount << ")");
		} else {
			SKC_TRACE(client, 3, "Client object destroyed; not added to freelist " <<
				"because it's full (" << freeClientCount << ")");
			delete client;
		}

		if (serverState == SHUTTING_DOWN
		 && activeClientCount == 0
		 && disconnectedClientCount == 0)
		{
			finishShutdown();
		}
	}

	bool addClientToFreelist(Client *client) {
		if (freeClientCount < freelistLimit) {
			STAILQ_INSERT_HEAD(&freeClients, client, nextClient.freeClient);
			freeClientCount++;
			client->refcount.store(2, boost::memory_order_relaxed);
			client->setConnState(Client::IN_FREELIST);
			return true;
		} else {
			return false;
		}
	}

	void passClientToEventLoopThread(Client *client) {
		// The shutdown procedure waits until all ACTIVE and DISCONNECTED
		// clients are gone before destroying a Server, so we know for sure
		// that this async callback outlives the Server.
		ctx->libev->runLater(boost::bind(&BaseServer::passClientToEventLoopThreadCallback,
			this, ClientRefType(client)));
	}

	void passClientToEventLoopThreadCallback(ClientRefType clientRef) {
		// Do nothing. Once this method returns, the reference count of the
		// client drops to 0, and clientReachedZeroRefcount() is called.
	}

	void finishShutdown() {
		SKS_NOTICE("Shutdown finished");
		serverState = FINISHED_SHUTDOWN;
	}

	void logClientDataReceived(Client *client, const MemoryKit::mbuf &buffer, int errcode) {
		if (errcode == 0 && !buffer.empty()) {
			SKC_TRACE(client, 3, "Processing " << buffer.size() << " bytes of client data");
		} else if (buffer.empty()) {
			SKC_TRACE(client, 2, "Client sent EOF");
		} else {
			SKC_TRACE(client, 2, "Error reading from client socket: " <<
				strerror(errcode) << " (errno=" << errcode << ")");
		}
	}

	static Channel::Result _onClientDataReceived(FdInputChannel *channel,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		Client *client = static_cast<Client *>(channel->getHooks()->userData);
		BaseServer *server = static_cast<BaseServer *>(client->getServer());
		server->logClientDataReceived(client, buffer, errcode);
		return server->onClientDataReceived(client, buffer, errcode);
	}

	static void _onClientOutputError(FileBufferedFdOutputChannel *channel, int errcode) {
		Client *client = static_cast<Client *>(channel->getHooks()->userData);
		BaseServer *server = static_cast<BaseServer *>(client->getServer());
		server->onClientOutputError(client, errcode);
	}

protected:
	/***** Protected API *****/

	/** Get a thread-safe reference to the client. As long as the client
	 * has a reference, it will never be added to the freelist.
	 */
	ClientRefType getClientRef(Client *client) {
		return ClientRefType(client);
	}

	/** Increase client reference count. */
	void refClient(Client *client) {
		int oldRefcount = client->refcount.fetch_add(1, boost::memory_order_relaxed);
		SKC_TRACE(client, 3, "Refcount increased; it is now " << (oldRefcount + 1));
	}

	/** Decrease client reference count. Adds client to the
	 * freelist if reference count drops to 0.
	 */
	void unrefClient(Client *client) {
		int oldRefcount = client->refcount.fetch_sub(1, boost::memory_order_release);
		assert(oldRefcount >= 1);

		SKC_TRACE(client, 3, "Refcount decreased; it is now " << (oldRefcount - 1));
		if (oldRefcount == 1) {
			boost::atomic_thread_fence(boost::memory_order_acquire);

			if (ctx->libev->onEventLoopThread()) {
				assert(client->getConnState() != Client::IN_FREELIST);
				/* As long as the client is still in the ACTIVE state, it has at least
				 * one reference, namely from the Server itself. Therefore it's impossible
				 * to get to a zero reference count without having disconnected a client. */
				assert(client->getConnState() == Client::DISCONNECTED);
				clientReachedZeroRefcount(client);
			} else {
				// Let the event loop handle the client reaching the 0 refcount.
				SKC_TRACE(client, 3, "Passing client object to event loop thread");
				passClientToEventLoopThread(client);
			}
		}
	}


	/***** Hooks *****/

	virtual void onClientObjectCreated(Client *client) {
		client->hooks.impl        = this;
		client->hooks.userData    = client;

		client->input.setContext(ctx);
		client->input.setHooks(&client->hooks);
		client->input.setDataCallback(_onClientDataReceived);

		client->output.setContext(ctx);
		client->output.setHooks(&client->hooks);
		client->output.errorCallback = _onClientOutputError;
	}

	virtual void onClientsAccepted(Client **clients, unsigned int size) {
		unsigned int i;

		for (i = 0; i < size; i++) {
			Client *client = clients[i];

			onClientAccepted(client);
			if (client->connected()) {
				if (startReadingAfterAccept) {
					client->input.startReading();
				} else {
					client->input.startReadingInNextTick();
				}
			}
			// A Client object starts with a refcount of 2 so that we can
			// be sure it won't be destroyted while we're looping inside this
			// function. But we also need an extra unref here.
			unrefClient(client);
		}
	}

	virtual void onClientAccepted(Client *client) {
		// Do nothing.
	}

	virtual void onClientDisconnecting(Client *client) {
		// Do nothing.
	}

	virtual void onClientDisconnected(Client *client) {
		// Do nothing.
	}

	virtual bool shouldDisconnectClientOnShutdown(Client *client) {
		return true;
	}

	virtual Channel::Result onClientDataReceived(Client *client, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		if (buffer.empty()) {
			disconnect(&client);
		}
		return Channel::Result(0, true);
	}

	virtual void onClientOutputError(Client *client, int errcode) {
		char message[1024];
		int ret = snprintf(message, sizeof(message),
			"client socket write error: %s (errno=%d)",
			strerror(errcode), errcode);
		disconnectWithError(&client, StaticString(message, ret));
	}

public:
	/***** Public methods *****/

	BaseServer(Context *context)
		: acceptBurstCount(32),
		  startReadingAfterAccept(true),
		  minSpareClients(128),
		  freelistLimit(1024),
		  serverState(ACTIVE),
		  freeClientCount(0),
		  activeClientCount(0),
		  disconnectedClientCount(0),
		  ctx(context),
		  nextClientNumber(1),
		  nEndpoints(0),
		  accept4Available(true)
	{
		STAILQ_INIT(&freeClients);
		TAILQ_INIT(&activeClients);
		TAILQ_INIT(&disconnectedClients);
		acceptResumptionWatcher.set(context->libev->getLoop());
		acceptResumptionWatcher.set(0, 3);
		acceptResumptionWatcher.set<
			BaseServer<DerivedServer, Client>,
			&BaseServer<DerivedServer, Client>::onAcceptResumeTimeout>(this);
	}

	virtual ~BaseServer() {
		assert(serverState == FINISHED_SHUTDOWN);
	}


	/***** Initialization, listening and shutdown *****/

	// Pre-create multiple client objects so that they get allocated
	// near each other in memory. Hopefully increases CPU cache locality.
	void createSpareClients() {
		for (unsigned int i = 0; i < minSpareClients; i++) {
			Client *client = createNewClientObject();
			client->setConnState(Client::IN_FREELIST);
			STAILQ_INSERT_HEAD(&freeClients, client, nextClient.freeClient);
			freeClientCount++;
		}
	}

	void listen(int fd) {
		assert(nEndpoints < MAX_ENDPOINTS);
		setNonBlocking(fd);
		ev_io_init(&endpoints[nEndpoints], _onAcceptable, fd, EV_READ);
		endpoints[nEndpoints].data = this;
		ev_io_start(ctx->libev->getLoop(), &endpoints[nEndpoints]);
		nEndpoints++;
	}

	void shutdown(bool forceDisconnect = false) {
		if (serverState != ACTIVE) {
			return;
		}

		vector<Client *> clients;
		Client *client;
		typename vector<Client *>::iterator v_it, v_end;

		serverState = SHUTTING_DOWN;

		// Stop listening on all endpoints.
		acceptResumptionWatcher.stop();
		for (uint8_t i = 0; i < nEndpoints; i++) {
			ev_io_stop(ctx->libev->getLoop(), &endpoints[i]);
		}

		if (activeClientCount == 0 && disconnectedClientCount == 0) {
			finishShutdown();
			return;
		}

		// Once we've set serverState to SHUTTING_DOWN, `activeClientCount` will no
		// longer grow, but may change due to hooks and callbacks.
		// So we make a copy of the client list here and operate on that.
		clients.reserve(activeClientCount);
		TAILQ_FOREACH (client, &activeClients, nextClient.activeOrDisconnectedClient) {
			assert(client->getConnState() == Client::ACTIVE);
			refClient(client);
			clients.push_back(client);
		}

		// Disconnect each active client.
		v_end = clients.end();
		for (v_it = clients.begin(); v_it != v_end; v_it++) {
			client = (Client *) *v_it;
			if (forceDisconnect || shouldDisconnectClientOnShutdown(client)) {
				Client *c = client;
				disconnectWithError(&client, "server is shutting down");
				unrefClient(c);
			}
		}

		// When all active and disconnected clients are gone,
		// shutdownCompleted() will be called to set state to FINISHED_SHUTDOWN.
	}


	/***** Client management *****/

	virtual int getClientName(Client *client, char *buf, size_t size) const {
		return snprintf(buf, size, "%03x", client->number);
	}

	vector<ClientRefType> getActiveClients() {
		vector<ClientRefType> result;
		Client *client;

		TAILQ_FOREACH (client, &activeClients, nextClient.activeOrDisconnectedClient) {
			assert(client->getConnState() == Client::ACTIVE);
			result.push_back(ClientRefType(client));
		}
		return result;
	}

	Client *lookupClient(int fd) {
		Client *client;

		TAILQ_FOREACH (client, &activeClients, nextClient.activeOrDisconnectedClient) {
			assert(client->getConnState() == Client::ACTIVE);
			if (client->fd == fd) {
				return client;
			}
		}
		return NULL;
	}

	bool disconnect(int fd) {
		assert(serverState != FINISHED_SHUTDOWN);
		Client *client = lookupClient(fd);
		if (client != NULL) {
			return disconnect(&client);
		} else {
			return false;
		}
	}

	bool disconnect(Client **client) {
		Client *c = *client;
		if (c->getConnState() != Client::ACTIVE) {
			return false;
		}

		int fdnum = c->getFd();
		SKC_TRACE(c, 2, "Disconnecting; there are now " << (activeClientCount - 1) <<
			" active clients");
		onClientDisconnecting(c);

		c->setConnState(ClientType::DISCONNECTED);
		TAILQ_REMOVE(&activeClients, c, nextClient.activeOrDisconnectedClient);
		activeClientCount--;
		TAILQ_INSERT_HEAD(&disconnectedClients, c, nextClient.activeOrDisconnectedClient);
		disconnectedClientCount++;

		c->input.deinitialize();
		c->output.deinitialize();
		c->deinitialize();
		try {
			safelyClose(fdnum);
		} catch (SystemException &e) {
			SKC_WARN(c, "An error occurred while closing the client file descriptor: " <<
				e.what() << " (errno=" << e.code() << ")");
		}

		*client = NULL;
		onClientDisconnected(c);
		unrefClient(c);
		return true;
	}

	void disconnectWithError(Client **client, const StaticString &message) {
		SKC_WARN(*client, "Disconnecting with error: " << message);
		disconnect(client);
	}


	/***** Introspection *****/

	OXT_FORCE_INLINE
	Context *getContext() {
		return ctx;
	}

	virtual StaticString getServerName() const {
		return StaticString("Server", sizeof("Server") - 1);
	}

	void configure(void *json) { }
	void inspectStateAsJson() { }


	/***** Friend-public methods and hook implementations *****/

	void _refClient(Client *client) {
		refClient(client);
	}

	void _unrefClient(Client *client) {
		unrefClient(client);
	}

	virtual bool hook_isConnected(Hooks *hooks, void *source) {
		Client *client = static_cast<Client *>(hooks->userData);
		return client->connected();
	}

	virtual void hook_ref(Hooks *hooks, void *source) {
		Client *client = static_cast<Client *>(hooks->userData);
		refClient(client);
	}

	virtual void hook_unref(Hooks *hooks, void *source) {
		Client *client = static_cast<Client *>(hooks->userData);
		unrefClient(client);
	}
};


template <typename Client = Client>
class Server: public BaseServer<Server<Client>, Client> {
public:
	Server(Context *context)
		: BaseServer<Server, Client>(context)
		{ }
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_SERVER_H_ */
