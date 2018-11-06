/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SERVER_KIT_SERVER_H_
#define _PASSENGER_SERVER_KIT_SERVER_H_

#include <psg_sysqueue.h>

#include <boost/cstdint.hpp>
#include <boost/config.hpp>
#include <boost/scoped_ptr.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/macros.hpp>
#include <vector>
#include <new>
#include <ev++.h>

// for std::swap()
#if __cplusplus >= 201103L
	#include <utility>
#else
	#include <algorithm>
#endif
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <cstdio>
#include <jsoncpp/json.h>

#include <LoggingKit/LoggingKit.h>
#include <SafeLibev.h>
#include <Constants.h>
#include <ServerKit/Context.h>
#include <ServerKit/Errors.h>
#include <ServerKit/Hooks.h>
#include <ServerKit/Client.h>
#include <ServerKit/ClientRef.h>
#include <ConfigKit/ConfigKit.h>
#include <Algorithms/MovingAverage.h>
#include <Utils.h>
#include <Utils/ScopeGuard.h>
#include <StrIntTools/StrIntUtils.h>
#include <IOTools/IOUtils.h>
#include <SystemTools/SystemTime.h>

namespace Passenger {
namespace ServerKit {

using namespace std;
using namespace boost;
using namespace oxt;


// We use 'this' so that the macros work in derived template classes like HttpServer<>.
#define SKS_LOG(level, file, line, expr)  P_LOG(Passenger::LoggingKit::context, level, file, line, "[" << this->getServerName() << "] " << expr)
#define SKS_ERROR(expr)  P_ERROR("[" << this->getServerName() << "] " << expr)
#define SKS_WARN(expr)   P_WARN("[" << this->getServerName() << "] " << expr)
#define SKS_INFO(expr)   P_INFO("[" << this->getServerName() << "] " << expr)
#define SKS_NOTICE(expr) P_NOTICE("[" << this->getServerName() << "] " << expr)
#define SKS_DEBUG(expr)  P_DEBUG("[" << this->getServerName() << "] " << expr)
#define SKS_TRACE(level, expr) P_TRACE(level, "[" << this->getServerName() << "] " << expr)

#define SKS_NOTICE_FROM_STATIC(server, expr) P_NOTICE("[" << server->getServerName() << "] " << expr)

#define SKC_LOG(client, level, expr) SKC_LOG_FROM_STATIC(this, client, level, expr)
#define SKC_ERROR(client, expr) SKC_ERROR_FROM_STATIC(this, client, expr)
#define SKC_WARN(client, expr) SKC_WARN_FROM_STATIC(this, client, expr)
#define SKC_NOTICE(client, expr) SKC_NOTICE_FROM_STATIC(this, client, expr)
#define SKC_INFO(client, expr) SKC_INFO_FROM_STATIC(this, client, expr)
#define SKC_DEBUG(client, expr) SKC_DEBUG_FROM_STATIC(this, client, expr)
#define SKC_DEBUG_WITH_POS(client, file, line, expr) \
	SKC_DEBUG_FROM_STATIC_WITH_POS(this, client, file, line, expr)
#define SKC_TRACE(client, level, expr) SKC_TRACE_FROM_STATIC(this, client, level, expr)
#define SKC_TRACE_WITH_POS(client, level, file, line, expr) \
	SKC_TRACE_FROM_STATIC_WITH_POS(this, client, level, file, line, expr)

#define SKC_LOG_FROM_STATIC(server, client, level, expr) \
	do { \
		if (Passenger::LoggingKit::getLevel() >= level) { \
			char _clientName[16]; \
			int _clientNameSize = server->getClientName((client), _clientName, sizeof(_clientName)); \
			P_LOG(LoggingKit::context, level, __FILE__, __LINE__, \
				"[Client " << StaticString(_clientName, _clientNameSize) << "] " << expr); \
		} \
	} while (0)
#define SKC_ERROR_FROM_STATIC(server, client, expr) \
	SKC_LOG_FROM_STATIC(server, client, Passenger::LoggingKit::ERROR, expr)
#define SKC_WARN_FROM_STATIC(server, client, expr) \
	SKC_LOG_FROM_STATIC(server, client, Passenger::LoggingKit::WARN, expr)
#define SKC_NOTICE_FROM_STATIC(server, client, expr) \
	SKC_LOG_FROM_STATIC(server, client, Passenger::LoggingKit::NOTICE, expr)
#define SKC_INFO_FROM_STATIC(server, client, expr) \
	SKC_LOG_FROM_STATIC(server, client, Passenger::LoggingKit::INFO, expr)
#define SKC_DEBUG_FROM_STATIC(server, client, expr) \
	SKC_LOG_FROM_STATIC(server, client, Passenger::LoggingKit::DEBUG, expr)
#define SKC_DEBUG_FROM_STATIC_WITH_POS(server, client, file, line, expr) \
	do { \
		if (OXT_UNLIKELY(Passenger::LoggingKit::getLevel() >= Passenger::LoggingKit::DEBUG)) { \
			char _clientName[16]; \
			int _clientNameSize = server->getClientName((client), _clientName, sizeof(_clientName)); \
			P_DEBUG_WITH_POS(file, line, \
				"[Client " << StaticString(_clientName, _clientNameSize) << "] " << expr); \
		} \
	} while (0)
#define SKC_TRACE_FROM_STATIC(server, client, level, expr) \
	SKC_TRACE_FROM_STATIC_WITH_POS(server, client, level, __FILE__, __LINE__, expr)
#define SKC_TRACE_FROM_STATIC_WITH_POS(server, client, level, file, line, expr) \
	do { \
		if (OXT_UNLIKELY(Passenger::LoggingKit::getLevel() >= Passenger::LoggingKit::INFO + level)) { \
			char _clientName[16]; \
			int _clientNameSize = server->getClientName((client), _clientName, sizeof(_clientName)); \
			P_TRACE_WITH_POS(level, file, line, \
				"[Client " << StaticString(_clientName, _clientNameSize) << "] " << expr); \
		} \
	} while (0)

#define SKC_LOG_EVENT(klass, client, eventName) SKC_LOG_EVENT_FROM_STATIC(this, klass, client, eventName)
#define SKC_LOG_EVENT_FROM_STATIC(server, klass, client, eventName) \
	TRACE_POINT_WITH_DATA_FUNCTION(klass::_getClientNameFromTracePoint, client); \
	SKC_TRACE_FROM_STATIC(server, client, 3, "Event: " eventName)


/*
 * BEGIN ConfigKit schema: Passenger::ServerKit::BaseServerSchema
 * (do not edit: following text is automatically generated
 * by 'rake configkit_schemas_inline_comments')
 *
 *   accept_burst_count           unsigned integer   -   default(32)
 *   client_freelist_limit        unsigned integer   -   default(0)
 *   min_spare_clients            unsigned integer   -   default(0)
 *   start_reading_after_accept   boolean            -   default(true)
 *
 * END
 */
class BaseServerSchema: public ConfigKit::Schema {
private:
	void initialize() {
		using namespace ConfigKit;

		add("accept_burst_count", UINT_TYPE, OPTIONAL, 32);
		add("start_reading_after_accept", BOOL_TYPE, OPTIONAL, true);
		add("min_spare_clients", UINT_TYPE, OPTIONAL, 0);
		add("client_freelist_limit", UINT_TYPE, OPTIONAL, 0);
	}

public:
	BaseServerSchema() {
		initialize();
		finalize();
	}

	BaseServerSchema(bool _subclassing) {
		initialize();
	}
};

struct BaseServerConfigRealization {
	unsigned int acceptBurstCount: 7;
	bool startReadingAfterAccept: 1;
	unsigned int minSpareClients: 12;
	unsigned int clientFreelistLimit: 12;

	BaseServerConfigRealization(const ConfigKit::Store &config)
		: acceptBurstCount(config["accept_burst_count"].asUInt()),
		  startReadingAfterAccept(config["start_reading_after_accept"].asBool()),
		  minSpareClients(config["min_spare_clients"].asUInt()),
		  clientFreelistLimit(config["client_freelist_limit"].asUInt())
		{ }

	void swap(BaseServerConfigRealization &other) BOOST_NOEXCEPT_OR_NOTHROW {
		#define SWAP_BITFIELD(Type, name) \
			do { \
				Type tmp = name; \
				name = other.name; \
				other.name = tmp; \
			} while (false)

		SWAP_BITFIELD(unsigned int, acceptBurstCount);
		SWAP_BITFIELD(bool, startReadingAfterAccept);
		SWAP_BITFIELD(unsigned int, minSpareClients);
		SWAP_BITFIELD(unsigned int, clientFreelistLimit);

		#undef SWAP_BITFIELD
	}
};

struct BaseServerConfigChangeRequest {
	boost::scoped_ptr<ConfigKit::Store> config;
	boost::scoped_ptr<BaseServerConfigRealization> configRlz;
};


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
 * handled through the FileBufferedFdSinkChannel abstraction.. This makes writing
 * evented servers very easy.
 *
 * ### Multiple listen endpoints
 *
 * The server can listen on multiple server endpoints at the same time (e.g. TCP and
 * Unix domain sockets), up to SERVER_KIT_MAX_SERVER_ENDPOINTS.
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
	typedef BaseServer BaseClass;
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

	typedef void (*Callback)(DerivedServer *server);
	typedef BaseServerConfigChangeRequest ConfigChangeRequest;

	/***** Configuration *****/
	ConfigKit::Store config;
	BaseServerConfigRealization configRlz;
	Callback shutdownFinishCallback;

	/***** Working state and statistics (do not modify) *****/
	State serverState;
	FreeClientList freeClients;
	ClientList activeClients, disconnectedClients;
	unsigned int freeClientCount, activeClientCount, disconnectedClientCount;
	unsigned int peakActiveClientCount;
	unsigned long totalClientsAccepted, lastTotalClientsAccepted;
	unsigned long long totalBytesConsumed;
	ev_tstamp lastStatisticsUpdateTime;
	double clientAcceptSpeed1m, clientAcceptSpeed1h;

private:
	Context *ctx;
	unsigned int nextClientNumber: 28;
	uint8_t nEndpoints: 3;
	bool accept4Available: 1;
	ev::timer acceptResumptionWatcher;
	ev::timer statisticsUpdateWatcher;
	ev::io endpoints[SERVER_KIT_MAX_SERVER_ENDPOINTS];


	/***** Private methods *****/

	void preinitialize(Context *context) {
		STAILQ_INIT(&freeClients);
		TAILQ_INIT(&activeClients);
		TAILQ_INIT(&disconnectedClients);

		acceptResumptionWatcher.set(context->libev->getLoop());
		acceptResumptionWatcher.set<
			BaseServer<DerivedServer, Client>,
			&BaseServer<DerivedServer, Client>::onAcceptResumeTimeout>(this);

		statisticsUpdateWatcher.set(context->libev->getLoop());
		statisticsUpdateWatcher.set<
			BaseServer<DerivedServer, Client>,
			&BaseServer<DerivedServer, Client>::onStatisticsUpdateTimeout>(this);
	}

	static void _onAcceptable(EV_P_ ev_io *io, int revents) {
		static_cast<BaseServer *>(io->data)->onAcceptable(io, revents);
	}

	void onAcceptable(ev_io *io, int revents) {
		TRACE_POINT();
		unsigned int acceptCount = 0;
		bool error = false;
		int fd, errcode = 0;
		Client *client;
		Client *acceptedClients[MAX_ACCEPT_BURST_COUNT];

		P_ASSERT_EQ(serverState, ACTIVE);
		SKS_DEBUG("New clients can be accepted on a server socket");

		for (unsigned int i = 0; i < configRlz.acceptBurstCount; i++) {
			fd = acceptNonBlockingSocket(io->fd);
			if (fd == -1) {
				error = true;
				errcode = errno;
				break;
			}

			FdGuard guard(fd, NULL, 0);
			client = checkoutClientObject();
			TAILQ_INSERT_HEAD(&activeClients, client, nextClient.activeOrDisconnectedClient);
			acceptedClients[acceptCount] = client;
			activeClientCount++;
			acceptCount++;
			totalClientsAccepted++;
			client->number = getNextClientNumber();
			reinitializeClient(client, fd);
			P_LOG_FILE_DESCRIPTOR_PURPOSE(fd, "Server " << getServerName()
				<< ", client " << getClientName(client));
			guard.clear();
		}

		if (acceptCount > 0) {
			SKS_DEBUG(acceptCount << " new client(s) accepted; there are now " <<
				activeClientCount << " active client(s)");
		}
		if (error && errcode != EAGAIN && errcode != EWOULDBLOCK) {
			SKS_ERROR("Cannot accept client: " << getErrorDesc(errcode) <<
				" (errno=" << errcode << "). " <<
				"Stop accepting clients for 3 seconds. " <<
				"Current client count: " << activeClientCount);
			serverState = TOO_MANY_FDS;
			acceptResumptionWatcher.set(3, 0);
			acceptResumptionWatcher.start();
			for (uint8_t i = 0; i < nEndpoints; i++) {
				ev_io_stop(ctx->libev->getLoop(), &endpoints[i]);
			}
		}

		onClientsAccepted(acceptedClients, acceptCount);
	}

	void onAcceptResumeTimeout(ev::timer &timer, int revents) {
		TRACE_POINT();
		P_ASSERT_EQ(serverState, TOO_MANY_FDS);
		SKS_NOTICE("Resuming accepting new clients");
		serverState = ACTIVE;
		for (uint8_t i = 0; i < nEndpoints; i++) {
			ev_io_start(ctx->libev->getLoop(), &endpoints[i]);
		}
	}

	int acceptNonBlockingSocket(int serverFd) {
		union {
			struct sockaddr_in inaddr;
			struct sockaddr_in6 inaddr6;
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
			FdGuard guard(fd, __FILE__, __LINE__);
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

	void onStatisticsUpdateTimeout(ev::timer &timer, int revents) {
		TRACE_POINT();

		this->onUpdateStatistics();
		this->onFinalizeStatisticsUpdate();

		timer.repeat = timeToNextMultipleD(5, ev_now(this->getLoop()));
		timer.again();
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
		P_ASSERT_EQ(client->getConnState(), Client::IN_FREELIST);
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
		TRACE_POINT();
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
		if (freeClientCount < configRlz.clientFreelistLimit) {
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
			this, ClientRefType(client, __FILE__, __LINE__)));
	}

	void passClientToEventLoopThreadCallback(ClientRefType clientRef) {
		// Do nothing. Once this method returns, the reference count of the
		// client drops to 0, and clientReachedZeroRefcount() is called.
	}

	const char *getServerStateString() const {
		switch (serverState) {
		case ACTIVE:
			return "ACTIVE";
		case TOO_MANY_FDS:
			return "TOO_MANY_FDS";
		case SHUTTING_DOWN:
			return "SHUTTING_DOWN";
		case FINISHED_SHUTDOWN:
			return "FINISHED_SHUTDOWN";
		default:
			return "UNKNOWN";
		}
	}

	void finishShutdown() {
		TRACE_POINT();
		compact(LoggingKit::INFO);

		acceptResumptionWatcher.stop();
		statisticsUpdateWatcher.stop();

		SKS_NOTICE("Shutdown finished");
		serverState = FINISHED_SHUTDOWN;
		if (shutdownFinishCallback) {
			shutdownFinishCallback(static_cast<DerivedServer *>(this));
		}
	}

	void logClientDataReceived(Client *client, const MemoryKit::mbuf &buffer, int errcode) {
		if (buffer.size() > 0) {
			SKC_TRACE(client, 3, "Processing " << buffer.size() << " bytes of client data");
		} else if (errcode == 0) {
			SKC_TRACE(client, 2, "Client sent EOF");
		} else {
			SKC_TRACE(client, 2, "Error reading from client socket: " <<
				getErrorDesc(errcode) << " (errno=" << errcode << ")");
		}
	}

	static Channel::Result _onClientDataReceived(Channel *_channel,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		FdSourceChannel *channel = reinterpret_cast<FdSourceChannel *>(_channel);
		Client *client = static_cast<Client *>(static_cast<BaseClient *>(
			channel->getHooks()->userData));
		BaseServer *server = getServerFromClient(client);
		const size_t bufferSize = buffer.size();

		server->logClientDataReceived(client, buffer, errcode);
		Channel::Result result = server->onClientDataReceived(client, buffer, errcode);

		// This counter is mostly useful for unit tests, so it's too much hassle to
		// support cases where result.consumed < 1.
		size_t consumed = std::max<size_t>(0,
			std::min<size_t>(result.consumed, bufferSize));
		server->totalBytesConsumed += consumed;
		SKC_TRACE_FROM_STATIC(server, client, 2,
			consumed << " bytes of client data consumed in this callback");

		return result;
	}

	static void _onClientOutputError(FileBufferedFdSinkChannel *_channel, int errcode) {
		FileBufferedFdSinkChannel *channel =
			reinterpret_cast<FileBufferedFdSinkChannel *>(_channel);
		Client *client = static_cast<Client *>(static_cast<BaseClient *>(
			channel->getHooks()->userData));
		BaseServer *server = getServerFromClient(client);
		server->onClientOutputError(client, errcode);
	}

protected:
	/***** Hooks *****/

	virtual void onClientObjectCreated(Client *client) {
		TRACE_POINT();

		client->hooks.impl        = this;
		client->hooks.userData    = static_cast<BaseClient *>(client);

		client->input.setContext(ctx);
		client->input.setHooks(&client->hooks);
		client->input.setDataCallback(_onClientDataReceived);

		client->output.setContext(ctx);
		client->output.setHooks(&client->hooks);
		client->output.errorCallback = _onClientOutputError;
	}

	virtual void onClientsAccepted(Client **clients, unsigned int size) {
		unsigned int i;

		peakActiveClientCount = std::max(peakActiveClientCount, activeClientCount);

		for (i = 0; i < size; i++) {
			Client *client = clients[i];

			onClientAccepted(client);
			if (client->connected()) {
				if (configRlz.startReadingAfterAccept) {
					client->input.startReading();
				} else {
					client->input.startReadingInNextTick();
				}
			}
			// A Client object starts with a refcount of 2 so that we can
			// be sure it won't be destroyted while we're looping inside this
			// function. But we also need an extra unref here.
			unrefClient(client, __FILE__, __LINE__);
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
		return false;
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
		SKC_LOG_EVENT(DerivedServer, client, "onClientOutputError");
		char message[1024];
		int ret = snprintf(message, sizeof(message),
			"client socket write error: %s (errno=%d)",
			getErrorDesc(errcode), errcode);
		disconnectWithError(&client, StaticString(message, ret),
			getClientOutputErrorDisconnectionLogLevel(client, errcode));
	}

	virtual LoggingKit::Level getClientOutputErrorDisconnectionLogLevel(
		Client *client, int errcode) const
	{
		return LoggingKit::WARN;
	}

	virtual void onUpdateStatistics() {
		SKS_DEBUG("Updating statistics");
		ev_tstamp now = ev_now(this->getLoop());
		ev_tstamp duration = now - lastStatisticsUpdateTime;

		// Statistics are updated about every 5 seconds, so about 12 updates
		// per minute. We want the old average to decay to 5% after 1 minute
		// and 1 hour, respectively, so:
		// 1 minute: 1 - exp(ln(0.05) / 12) = 0.22092219194555585
		// 1 hour  : 1 - exp(ln(0.05) / (60 * 12)) = 0.0041520953856636345
		clientAcceptSpeed1m = expMovingAverage(clientAcceptSpeed1m,
			(totalClientsAccepted - lastTotalClientsAccepted) / duration,
			0.22092219194555585);
		clientAcceptSpeed1h = expMovingAverage(clientAcceptSpeed1h,
			(totalClientsAccepted - lastTotalClientsAccepted) / duration,
			0.0041520953856636345);
	}

	virtual void onFinalizeStatisticsUpdate() {
		lastTotalClientsAccepted = totalClientsAccepted;
		lastStatisticsUpdateTime = ev_now(this->getLoop());
	}

	virtual void reinitializeClient(Client *client, int fd) {
		client->setConnState(Client::ACTIVE);
		SKC_TRACE(client, 2, "Client associated with file descriptor: " << fd);
		client->input.reinitialize(fd);
		client->output.reinitialize(fd);
	}

	virtual void deinitializeClient(Client *client) {
		client->input.deinitialize();
		client->output.deinitialize();
	}

	virtual void onShutdown(bool forceDisconnect) {
		// Do nothing.
	}

public:
	/***** Public methods *****/

	BaseServer(Context *context, const BaseServerSchema &schema,
		const Json::Value &initialConfig = Json::Value(),
		const ConfigKit::Translator &translator = ConfigKit::DummyTranslator())
		: config(schema, initialConfig, translator),
		  configRlz(config),
		  shutdownFinishCallback(NULL),
		  serverState(ACTIVE),
		  freeClientCount(0),
		  activeClientCount(0),
		  disconnectedClientCount(0),
		  peakActiveClientCount(0),
		  totalClientsAccepted(0),
		  lastTotalClientsAccepted(0),
		  totalBytesConsumed(0),
		  lastStatisticsUpdateTime(ev_time()),
		  clientAcceptSpeed1m(-1),
		  clientAcceptSpeed1h(-1),
		  ctx(context),
		  nextClientNumber(1),
		  nEndpoints(0),
		  accept4Available(true)
	{
		preinitialize(context);
	}

	virtual ~BaseServer() {
		P_ASSERT_EQ(serverState, FINISHED_SHUTDOWN);
	}


	/***** Initialization, listening and shutdown *****/

	virtual void initialize() {
		statisticsUpdateWatcher.set(5, 5);
		statisticsUpdateWatcher.start();
	}

	// Pre-create multiple client objects so that they get allocated
	// near each other in memory. Hopefully increases CPU cache locality.
	void createSpareClients() {
		for (unsigned int i = 0; i < configRlz.minSpareClients; i++) {
			Client *client = createNewClientObject();
			client->setConnState(Client::IN_FREELIST);
			STAILQ_INSERT_HEAD(&freeClients, client, nextClient.freeClient);
			freeClientCount++;
		}
	}

	void listen(int fd) {
		#ifdef EOPNOTSUPP
			#define EXTENSION_EOPNOTSUPP EOPNOTSUPP
		#else
			#define EXTENSION_EOPNOTSUPP ENOTSUP
		#endif

		TRACE_POINT();
		assert(nEndpoints < SERVER_KIT_MAX_SERVER_ENDPOINTS);
		int flag = 1;
		setNonBlocking(fd);
		if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) == -1
		 && errno != ENOPROTOOPT
		 && errno != ENOTSUP
		 && errno != EXTENSION_EOPNOTSUPP)
		{
			int e = errno;
			SKS_WARN("Cannot disable Nagle's algorithm on a TCP socket: " <<
				strerror(e) << " (errno=" << e << ")");
		}
		ev_io_init(&endpoints[nEndpoints], _onAcceptable, fd, EV_READ);
		endpoints[nEndpoints].data = this;
		ev_io_start(ctx->libev->getLoop(), &endpoints[nEndpoints]);
		nEndpoints++;

		#undef EXTENSION_EOPNOTSUPP
	}

	void shutdown(bool forceDisconnect = false) {
		if (serverState != ACTIVE) {
			return;
		}

		vector<Client *> clients;
		Client *client;
		typename vector<Client *>::iterator v_it, v_end;

		SKS_DEBUG("Shutting down");
		serverState = SHUTTING_DOWN;
		onShutdown(forceDisconnect);

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
			P_ASSERT_EQ(client->getConnState(), Client::ACTIVE);
			refClient(client, __FILE__, __LINE__);
			clients.push_back(client);
		}

		// Disconnect each active client.
		v_end = clients.end();
		for (v_it = clients.begin(); v_it != v_end; v_it++) {
			client = (Client *) *v_it;
			Client *c = client;
			if (forceDisconnect || shouldDisconnectClientOnShutdown(client)) {
				disconnectWithError(&client, "server is shutting down");
			}
			unrefClient(c, __FILE__, __LINE__);
		}

		// When all active and disconnected clients are gone,
		// finishShutdown() will be called to set state to FINISHED_SHUTDOWN.
	}


	void feedNewClients(const int *fds, unsigned int size) {
		Client *client;
		Client *acceptedClients[MAX_ACCEPT_BURST_COUNT];

		assert(size > 0);
		assert(size <= MAX_ACCEPT_BURST_COUNT);
		P_ASSERT_EQ(serverState, ACTIVE);

		activeClientCount += size;
		totalClientsAccepted += size;

		for (unsigned int i = 0; i < size; i++) {
			client = checkoutClientObject();
			TAILQ_INSERT_HEAD(&activeClients, client, nextClient.activeOrDisconnectedClient);
			acceptedClients[i] = client;
			client->number = getNextClientNumber();
			reinitializeClient(client, fds[i]);
			P_LOG_FILE_DESCRIPTOR_PURPOSE(fds[i], "Server " << getServerName()
				<< ", client " << getClientName(client));
		}

		SKS_DEBUG(size << " new client(s) accepted; there are now " <<
			activeClientCount << " active client(s)");

		onClientsAccepted(acceptedClients, size);
	}


	/***** Server management *****/

	virtual void compact(LoggingKit::Level logLevel = LoggingKit::NOTICE) {
		unsigned int count = freeClientCount;

		while (!STAILQ_EMPTY(&freeClients)) {
			Client *client = STAILQ_FIRST(&freeClients);
			P_ASSERT_EQ(client->getConnState(), Client::IN_FREELIST);
			client->refcount.store(2, boost::memory_order_relaxed);
			freeClientCount--;
			STAILQ_REMOVE_HEAD(&freeClients, nextClient.freeClient);
			delete client;
		}
		assert(freeClientCount == 0);

		SKS_LOG(logLevel, __FILE__, __LINE__,
			"Freed " << count << " spare client objects");
	}


	/***** Client management *****/

	// Ensures that the buffer is NULL-terminated.
	virtual unsigned int getClientName(const Client *client, char *buf, size_t size) const {
		unsigned int ret = uintToString(client->number, buf, size - 1);
		buf[ret] = '\0';
		return ret;
	}

	string getClientName(const Client *client) {
		char buf[128];
		unsigned int size = getClientName(client, buf, sizeof(buf));
		return string(buf, size);
	}

	vector<ClientRefType> getActiveClients() {
		vector<ClientRefType> result;
		Client *client;

		TAILQ_FOREACH (client, &activeClients, nextClient.activeOrDisconnectedClient) {
			P_ASSERT_EQ(client->getConnState(), Client::ACTIVE);
			result.push_back(ClientRefType(client, __FILE__, __LINE__));
		}
		return result;
	}

	Client *lookupClient(int fd) {
		Client *client;

		TAILQ_FOREACH (client, &activeClients, nextClient.activeOrDisconnectedClient) {
			P_ASSERT_EQ(client->getConnState(), Client::ACTIVE);
			if (client->fd == fd) {
				return client;
			}
		}
		return NULL;
	}

	Client *lookupClient(const StaticString &clientName) {
		Client *client;

		TAILQ_FOREACH (client, &activeClients, nextClient.activeOrDisconnectedClient) {
			P_ASSERT_EQ(client->getConnState(), Client::ACTIVE);
			char buf[512];
			unsigned int size;

			size = getClientName(client, buf, sizeof(buf));
			if (StaticString(buf, size) == clientName) {
				return client;
			}
		}
		return NULL;
	}

	bool clientOnUnixDomainSocket(Client *client) {
		union {
			struct sockaddr genericAddress;
			struct sockaddr_un unixAddress;
			struct sockaddr_in inetAddress;
		} addr;
		socklen_t len = sizeof(addr);
		int ret;

		do {
			ret = getsockname(client->getFd(), &addr.genericAddress, &len);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			int e = errno;
			throw SystemException("Unable to autodetect socket type (getsockname() failed)", e);
		} else {
			#ifdef AF_UNIX
				return addr.genericAddress.sa_family == AF_UNIX;
			#else
				return addr.genericAddress.sa_family == AF_LOCAL;
			#endif
		}
	}

	/** Increase client reference count. */
	void refClient(Client *client, const char *file, unsigned int line) {
		int oldRefcount = client->refcount.fetch_add(1, boost::memory_order_relaxed);
		SKC_TRACE_WITH_POS(client, 3, file, line,
			"Refcount increased; it is now " << (oldRefcount + 1));
	}

	/** Decrease client reference count. Adds client to the
	 * freelist if reference count drops to 0.
	 */
	void unrefClient(Client *client, const char *file, unsigned int line) {
		int oldRefcount = client->refcount.fetch_sub(1, boost::memory_order_release);
		assert(oldRefcount >= 1);

		SKC_TRACE_WITH_POS(client, 3, file, line,
			"Refcount decreased; it is now " << (oldRefcount - 1));
		if (oldRefcount == 1) {
			boost::atomic_thread_fence(boost::memory_order_acquire);

			if (ctx->libev->onEventLoopThread()) {
				assert(client->getConnState() != Client::IN_FREELIST);
				/* As long as the client is still in the ACTIVE state, it has at least
				 * one reference, namely from the Server itself. Therefore it's impossible
				 * to get to a zero reference count without having disconnected a client. */
				P_ASSERT_EQ(client->getConnState(), Client::DISCONNECTED);
				clientReachedZeroRefcount(client);
			} else {
				// Let the event loop handle the client reaching the 0 refcount.
				SKC_TRACE(client, 3, "Passing client object to event loop thread");
				passClientToEventLoopThread(client);
			}
		}
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

	bool disconnect(const StaticString &clientName) {
		assert(serverState != FINISHED_SHUTDOWN);
		Client *client = lookupClient(clientName);
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

		deinitializeClient(c);
		SKC_TRACE(c, 2, "Closing client file descriptor: " << fdnum);
		try {
			safelyClose(fdnum);
			P_LOG_FILE_DESCRIPTOR_CLOSE(fdnum);
		} catch (const SystemException &e) {
			SKC_WARN(c, "An error occurred while closing the client file descriptor: " <<
				e.what() << " (errno=" << e.code() << ")");
		}

		*client = NULL;
		onClientDisconnected(c);
		unrefClient(c, __FILE__, __LINE__);
		return true;
	}

	void disconnectWithWarning(Client **client, const StaticString &message) {
		SKC_WARN(*client, "Disconnecting client with warning: " << message);
		disconnect(client);
	}

	void disconnectWithError(Client **client, const StaticString &message,
		LoggingKit::Level logLevel = LoggingKit::WARN)
	{
		SKC_LOG(*client, logLevel, "Disconnecting client with error: " << message);
		disconnect(client);
	}


	/***** Introspection *****/

	OXT_FORCE_INLINE
	Context *getContext() {
		return ctx;
	}

	OXT_FORCE_INLINE
	const Context *getContext() const {
		return ctx;
	}

	OXT_FORCE_INLINE
	struct ev_loop *getLoop() const {
		return ctx->libev->getLoop();
	}

	virtual StaticString getServerName() const {
		return P_STATIC_STRING("Server");
	}

	bool prepareConfigChange(const Json::Value &updates,
		vector<ConfigKit::Error> &errors, BaseServerConfigChangeRequest &req)
	{
		req.config.reset(new ConfigKit::Store(config, updates, errors));
		if (errors.empty()) {
			req.configRlz.reset(new BaseServerConfigRealization(*req.config));
		}
		return errors.empty();
	}

	void commitConfigChange(BaseServerConfigChangeRequest &req)
		BOOST_NOEXCEPT_OR_NOTHROW
	{
		config.swap(*req.config);
		configRlz.swap(*req.configRlz);
	}

	virtual Json::Value inspectConfig() const {
		return config.inspect();
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc = ctx->inspectStateAsJson();
		const Client *client;

		doc["pid"] = (unsigned int) getpid();
		doc["server_state"] = getServerStateString();
		doc["free_client_count"] = freeClientCount;
		Json::Value &activeClientsDoc = doc["active_clients"] = Json::Value(Json::objectValue);
		doc["active_client_count"] = activeClientCount;
		Json::Value &disconnectedClientsDoc = doc["disconnected_clients"] = Json::Value(Json::objectValue);
		doc["disconnected_client_count"] = disconnectedClientCount;
		doc["peak_active_client_count"] = peakActiveClientCount;
		doc["client_accept_speed"]["1m"] = averageSpeedToJson(
			capFloatPrecision(clientAcceptSpeed1m * 60),
			"minute", "1 minute", -1);
		doc["client_accept_speed"]["1h"] = averageSpeedToJson(
			capFloatPrecision(clientAcceptSpeed1h * 60),
			"minute", "1 hour", -1);
		doc["total_clients_accepted"] = (Json::UInt64) totalClientsAccepted;
		doc["total_bytes_consumed"] = (Json::UInt64) totalBytesConsumed;

		TAILQ_FOREACH (client, &activeClients, nextClient.activeOrDisconnectedClient) {
			Json::Value subdoc;
			char clientName[16];

			getClientName(client, clientName, sizeof(clientName));
			activeClientsDoc[clientName] = inspectClientStateAsJson(client);
		}

		TAILQ_FOREACH (client, &disconnectedClients, nextClient.activeOrDisconnectedClient) {
			Json::Value subdoc;
			char clientName[16];

			getClientName(client, clientName, sizeof(clientName));
			disconnectedClientsDoc[clientName] = inspectClientStateAsJson(client);
		}

		return doc;
	}

	virtual Json::Value inspectClientStateAsJson(const Client *client) const {
		Json::Value doc;
		char clientName[16];

		assert(client->getConnState() != Client::IN_FREELIST);
		getClientName(client, clientName, sizeof(clientName));
		doc["connection_state"] = client->getConnStateString();
		doc["name"] = clientName;
		doc["number"] = client->number;
		doc["refcount"] = client->refcount.load(boost::memory_order_relaxed);
		doc["output_channel_state"] = client->output.inspectAsJson();

		return doc;
	}


	/***** Miscellaneous *****/

	/** Get a thread-safe reference to the client. As long as the client
	 * has a reference, it will never be added to the freelist.
	 */
	ClientRefType getClientRef(Client *client, const char *file, unsigned int line) {
		return ClientRefType(client, file, line);
	}

	/**
	 * Returns a pointer to the BaseServer that created the given Client object.
	 * Unlike the void pointer that Client::getServerBaseClassPointer() returns,
	 * this method's typed return value allows safe recasting of the result pointer.
	 */
	static const BaseServer *getConstServerFromClient(const Client *client) {
		return static_cast<BaseServer *>(client->getServerBaseClassPointer());
	}

	static BaseServer *getServerFromClient(Client *client) {
		return static_cast<BaseServer *>(client->getServerBaseClassPointer());
	}


	/***** Friend-public methods and hook implementations *****/

	void _refClient(Client *client, const char *file, unsigned int line) {
		refClient(client, file, line);
	}

	void _unrefClient(Client *client, const char *file, unsigned int line) {
		unrefClient(client, __FILE__, __LINE__);
	}

	static bool _getClientNameFromTracePoint(char *output, unsigned int size, void *userData) {
		Client *client = static_cast<Client *>(userData);
		BaseServer *server = getServerFromClient(client);
		char *pos = output;
		const char *end = output + size;

		pos = appendData(pos, end, "Client ");
		server->getClientName(client, pos, end - pos);
		return true;
	}

	virtual bool hook_isConnected(Hooks *hooks, void *source) {
		Client *client = static_cast<Client *>(static_cast<BaseClient *>(hooks->userData));
		return client->connected();
	}

	virtual void hook_ref(Hooks *hooks, void *source, const char *file, unsigned int line) {
		Client *client = static_cast<Client *>(static_cast<BaseClient *>(hooks->userData));
		refClient(client, file, line);
	}

	virtual void hook_unref(Hooks *hooks, void *source, const char *file, unsigned int line) {
		Client *client = static_cast<Client *>(static_cast<BaseClient *>(hooks->userData));
		unrefClient(client, file, line);
	}
};


template <typename Client = Client>
class Server: public BaseServer<Server<Client>, Client> {
public:
	Server(Context *context, const BaseServerSchema &schema,
		const Json::Value &initialConfig = Json::Value())
		: BaseServer<Server, Client>(context, schema, initialConfig)
		{ }
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_SERVER_H_ */
