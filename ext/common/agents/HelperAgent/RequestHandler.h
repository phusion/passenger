/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2014 Phusion
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

/*
   STAGES

     Accept connect password
              |
             \|/
          Read header
              |
             \|/
       +------+------+
       |             |
       |             |
      \|/            |
     Buffer          |
     request         |
     body            |
       |             |
       |             |
      \|/            |
    Checkout <-------+
    session
       |
       |
      \|/
  Send header
    to app
       |
       |
      \|/
  Send request
   body to app



     OVERVIEW OF I/O CHANNELS, PIPES AND WATCHERS


                             OPTIONAL:                                       appOutputWatcher
                          clientBodyBuffer                                         (o)
                                 |                                                  |
    +----------+                 |             +-----------+                        |   +---------------+
    |          |     ------ clientInput -----> |  Request  | ---------------->          |               |
    |  Client  | fd                            |  Handler  |                    session |  Application  |
    |          |     <--- clientOutputPipe --- |           | <--- appInput ---          |               |
    +----------+ |                             +-----------+                            +---------------+
                 |
                (o)
        clientOutputWatcher



   REQUEST BODY HANDLING STRATEGIES

   This table describes how we should handle the request body (the part in the request
   that comes after the request header, and may include WebSocket data), given various
   factors. Strategies that are listed first have precedence.

    Method     'Upgrade'  'Content-Length' or   Application    Action
               header     'Transfer-Encoding'   socket
               present?   header present?       protocol
    ---------------------------------------------------------------------------------------------

    GET/HEAD   Y          Y                     -              Reject request[1]
    Other      Y          -                     -              Reject request[2]

    GET/HEAD   Y          N                     http_session   Set requestBodyLength=-1, keep socket open when done forwarding.
    -          N          N                     http_session   Set requestBodyLength=0, keep socket open when done forwarding.
    -          N          Y                     http_session   Keep socket open when done forwarding. If Transfer-Encoding is
                                                               chunked, rechunck the body during forwarding.

    GET/HEAD   Y          N                     session        Set requestBodyLength=-1, half-close app socket when done forwarding.
    -          N          N                     session        Set requestBodyLength=0, half-close app socket when done forwarding.
    -          N          Y                     session        Half-close app socket when done forwarding.
    ---------------------------------------------------------------------------------------------

    [1] Supporting situations in which there is both an HTTP request body and WebSocket data
        is way too complicated. The RequestHandler code is complicated enough as it is,
        so we choose not to support requests like these.
    [2] RFC 6455 states that WebSocket upgrades may only happen over GET requests.
        We don't bother supporting non-WebSocket upgrades.

 */

#ifndef _PASSENGER_REQUEST_HANDLER_H_
#define _PASSENGER_REQUEST_HANDLER_H_

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/regex.hpp>
#include <boost/cstdint.hpp>
#include <ev++.h>

#if defined(__GLIBCXX__) || defined(__APPLE__)
	#include <cxxabi.h>
	#define CXX_ABI_API_AVAILABLE
#endif

#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <utility>
#include <typeinfo>
#include <cassert>
#include <cctype>

#include <Logging.h>
#include <EventedBufferedInput.h>
#include <MessageReadersWriters.h>
#include <Constants.h>
#include <UnionStation/Core.h>
#include <UnionStation/Transaction.h>
#include <UnionStation/ScopeLog.h>
#include <ApplicationPool2/Pool.h>
#include <ApplicationPool2/ErrorRenderer.h>
#include <Utils/StrIntUtils.h>
#include <Utils/IOUtils.h>
#include <Utils/HttpHeaderBufferer.h>
#include <Utils/HttpConstants.h>
#include <Utils/Template.h>
#include <Utils/Timer.h>
#include <Utils/Dechunker.h>
#include <agents/HelperAgent/AgentOptions.h>
#include <agents/HelperAgent/FileBackedPipe.h>
#include <agents/HelperAgent/ScgiRequestParser.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;
using namespace ApplicationPool2;

class RequestHandler;

#define MAX_STATUS_HEADER_SIZE 64

#define RH_ERROR(client, x) P_ERROR("[Client " << client->name() << "] " << x)
#define RH_WARN(client, x) P_WARN("[Client " << client->name() << "] " << x)
#define RH_DEBUG(client, x) P_DEBUG("[Client " << client->name() << "] " << x)
#define RH_TRACE(client, level, x) P_TRACE(level, "[Client " << client->name() << "] " << x)

#define RH_LOG_EVENT(client, eventName) \
	char _clientName[7 + 8]; \
	snprintf(_clientName, sizeof(_clientName), "Client %d", client->fdnum); \
	TRACE_POINT_WITH_DATA(_clientName); \
	RH_TRACE(client, 3, "Event: " eventName)


class Client: public boost::enable_shared_from_this<Client> {
private:
	struct ev_loop *getLoop() const;
	const SafeLibevPtr &getSafeLibev() const;
	unsigned int getConnectPasswordTimeout(const RequestHandler *handler) const;

	static size_t onClientInputData(const EventedBufferedInputPtr &source, const StaticString &data);
	static void onClientInputError(const EventedBufferedInputPtr &source, const char *message, int errnoCode);

	static void onClientBodyBufferData(const FileBackedPipePtr &source,
		const char *data, size_t size,
		const FileBackedPipe::ConsumeCallback &callback);
	static void onClientBodyBufferEnd(const FileBackedPipePtr &source);
	static void onClientBodyBufferError(const FileBackedPipePtr &source, int errorCode);
	static void onClientBodyBufferCommit(const FileBackedPipePtr &source);

	static void onClientOutputPipeData(const FileBackedPipePtr &source,
		const char *data, size_t size,
		const FileBackedPipe::ConsumeCallback &callback);
	static void onClientOutputPipeEnd(const FileBackedPipePtr &source);
	static void onClientOutputPipeError(const FileBackedPipePtr &source, int errorCode);
	static void onClientOutputPipeCommit(const FileBackedPipePtr &source);

	void onClientOutputWritable(ev::io &io, int revents);

	static size_t onAppInputData(const EventedBufferedInputPtr &source, const StaticString &data);
	static void onAppInputChunk(const char *data, size_t size, void *userData);
	static void onAppInputChunkEnd(void *userData);
	static void onAppInputError(const EventedBufferedInputPtr &source, const char *message, int errnoCode);

	void onAppOutputWritable(ev::io &io, int revents);

	void onTimeout(ev::timer &timer, int revents);


	static const char *boolStr(bool val) {
		static const char *strs[] = { "false", "true" };
		return strs[val];
	}

	void resetPrimitiveFields() {
		requestHandler = NULL;
		state = DISCONNECTED;
		backgroundOperations = 0;
		requestBodyIsBuffered = false;
		requestIsChunked = false;
		freeBufferedConnectPassword();
		connectedAt = 0;
		requestBodyLength = 0;
		requestBodyAlreadyRead = 0;
		checkoutSessionAfterCommit = false;
		stickySession = false;
		sessionCheckedOut = false;
		sessionCheckoutTry = 0;
		responseHeaderSeen = false;
		chunkedResponse = false;
		responseContentLength = -1;
		responseBodyAlreadyRead = 0;
		appRoot.clear();
	}

	void freeScopeLogs() {
		endScopeLog(&scopeLogs.requestProxying, false);
		endScopeLog(&scopeLogs.getFromPool, false);
		endScopeLog(&scopeLogs.bufferingRequestBody, false);
		endScopeLog(&scopeLogs.requestProcessing, false);
	}

public:
	/** Back reference to the RequestHandler that this Client is associated with.
	 * NULL when this Client is not in the pool or is disconnected. */
	RequestHandler *requestHandler;
	/** File descriptor of the client socket. Is empty when this Client is not
	 * in the pool or is disconnected. */
	FileDescriptor fd;
	/** The last associated file descriptor number is stored here. It is not
	 * cleared after disassociating. Its only purpose is to make logging calls
	 * like RH_DEBUG() print the correct client name after disconnect() is called.
	 * Do not use this value for anything else as it may not refer to a valid
	 * file descriptor. */
	int fdnum;


	/***** Client <-> RequestHandler I/O channels, pipes and watchers *****/

	/** Client input channel. */
	EventedBufferedInputPtr clientInput;
	/** If request body buffering is turned on, it will be buffered into this FileBackedPipe. */
	FileBackedPipePtr clientBodyBuffer;
	/** Client output pipe. */
	FileBackedPipePtr clientOutputPipe;
	/** Client output channel watcher. */
	ev::io clientOutputWatcher;


	/***** RequestHandler <-> Application I/O channels, pipes and watchers *****/

	/** Application input channel. */
	EventedBufferedInputPtr appInput;
	string appOutputBuffer;
	/** Application output channel watcher. */
	ev::io appOutputWatcher;


	/***** State variables *****/

	enum {
		BEGIN_READING_CONNECT_PASSWORD,
		STILL_READING_CONNECT_PASSWORD,
		READING_HEADER,
		BUFFERING_REQUEST_BODY,
		CHECKING_OUT_SESSION,
		SENDING_HEADER_TO_APP,
		FORWARDING_BODY_TO_APP,

		// Special states
		WRITING_SIMPLE_RESPONSE,
		DISCONNECTED
	} state;

	/* How many background operations are currently in progress, e.g.
	 * an asyncGet() or bodyBuffer.add(). If the client is disconnected
	 * while this flag is true, then the Client object is not reassociateable
	 * in order to give the completion callbacks a chance to cancel properly.
	 */
	unsigned int backgroundOperations;

	struct {
		char *data;
		unsigned int alreadyRead;
	} bufferedConnectPassword;

	// Used for enforcing the connection timeout.
	ev::timer timeoutTimer;

	ev_tstamp connectedAt;
	/** The size of the request body. The request body is the part that comes
	 * after the request headers, which may be the HTTP request message body,
	 * but may also be any other arbitrary data that is sent over the request
	 * socket (e.g. WebSocket data).
	 *
	 * Possible values:
	 *
	 * -1: infinite. Should keep forwarding client body until end of stream.
	 *  0: no client body. Should stop after sending headers to application.
	 * >0: Should forward exactly this many bytes of the client body.
	 */
	long long requestBodyLength;
	unsigned long long requestBodyAlreadyRead;
	Options options;
	ScgiRequestParser scgiParser;
	SessionPtr session;
	string appRoot;
	struct {
		UnionStation::ScopeLog
			*requestProcessing,
			*bufferingRequestBody,
			*getFromPool,
			*requestProxying;
	} scopeLogs;
	unsigned int sessionCheckoutTry;
	bool requestBodyIsBuffered;
	bool requestIsChunked;
	bool sessionCheckedOut;
	bool checkoutSessionAfterCommit;
	bool stickySession;

	bool responseHeaderSeen;
	bool chunkedResponse;
	/** The size of the response body, set based on the values of
	 * the Content-Length and Transfer-Encoding response headers.
	 * Possible values:
	 *
	 * -1: infinite. Should keep forwarding response body until end of stream.
	 *     This is the case for WebSockets or for responses without Content-Length.
	 *     Responses with "Transfer-Encoding: chunked" also fall under this
	 *     category, though in this case encountering the zero-length chunk is
	 *     treated the same as end of stream.
	 * 0 : no client body. Should immediately close connection after forwarding
	 *     headers.
	 * >0: Should forward exactly this many bytes of the response body.
	 */
	long long responseContentLength;
	unsigned long long responseBodyAlreadyRead;
	HttpHeaderBufferer responseHeaderBufferer;
	Dechunker responseDechunker;


	Client() {
		fdnum = -1;

		clientInput = boost::make_shared< EventedBufferedInput<> >();
		clientInput->onData   = onClientInputData;
		clientInput->onError  = onClientInputError;
		clientInput->userData = this;

		clientBodyBuffer = boost::make_shared<FileBackedPipe>("/tmp");
		clientBodyBuffer->userData  = this;
		clientBodyBuffer->onData    = onClientBodyBufferData;
		clientBodyBuffer->onEnd     = onClientBodyBufferEnd;
		clientBodyBuffer->onError   = onClientBodyBufferError;
		clientBodyBuffer->onCommit  = onClientBodyBufferCommit;

		clientOutputPipe = boost::make_shared<FileBackedPipe>("/tmp");
		clientOutputPipe->userData  = this;
		clientOutputPipe->onData    = onClientOutputPipeData;
		clientOutputPipe->onEnd     = onClientOutputPipeEnd;
		clientOutputPipe->onError   = onClientOutputPipeError;
		clientOutputPipe->onCommit  = onClientOutputPipeCommit;

		clientOutputWatcher.set<Client, &Client::onClientOutputWritable>(this);


		appInput = boost::make_shared< EventedBufferedInput<> >();
		appInput->onData   = onAppInputData;
		appInput->onError  = onAppInputError;
		appInput->userData = this;

		appOutputWatcher.set<Client, &Client::onAppOutputWritable>(this);


		timeoutTimer.set<Client, &Client::onTimeout>(this);


		responseDechunker.onData = onAppInputChunk;
		responseDechunker.onEnd = onAppInputChunkEnd;
		responseDechunker.userData = this;


		bufferedConnectPassword.data = NULL;
		bufferedConnectPassword.alreadyRead = 0;
		memset(&scopeLogs, 0, sizeof(scopeLogs));
		resetPrimitiveFields();
	}

	~Client() {
		if (requestHandler != NULL) {
			discard();
		}
		clientInput->userData      = NULL;
		clientBodyBuffer->userData = NULL;
		clientOutputPipe->userData = NULL;
		appInput->userData         = NULL;
		freeBufferedConnectPassword();
		freeScopeLogs();
	}

	void associate(RequestHandler *handler, const FileDescriptor &_fd) {
		assert(requestHandler == NULL);
		requestHandler = handler;
		fd = _fd;
		fdnum = _fd;
		state = BEGIN_READING_CONNECT_PASSWORD;
		connectedAt = ev_time();

		clientInput->reset(getSafeLibev().get(), _fd);
		clientInput->start();
		clientBodyBuffer->reset(getSafeLibev());
		clientOutputPipe->reset(getSafeLibev());
		clientOutputPipe->start();
		clientOutputWatcher.set(getLoop());
		clientOutputWatcher.set(_fd, ev::WRITE);

		// appOutputWatcher is initialized in initiateSession.

		timeoutTimer.set(getLoop());
		timeoutTimer.start(getConnectPasswordTimeout(handler) / 1000.0, 0.0);
	}

	void disassociate() {
		assert(requestHandler != NULL);
		resetPrimitiveFields();
		fd = FileDescriptor();

		clientInput->reset(NULL, FileDescriptor());
		clientBodyBuffer->reset();
		clientOutputPipe->reset();
		clientOutputWatcher.stop();

		appInput->reset(NULL, FileDescriptor());
		appOutputBuffer.resize(0);
		appOutputWatcher.stop();

		timeoutTimer.stop();
		scgiParser.reset();
		session.reset();
		responseHeaderBufferer.reset();
		responseDechunker.reset();
		freeScopeLogs();
	}

	void discard() {
		assert(requestHandler != NULL);
		resetPrimitiveFields();
		fd = FileDescriptor();

		clientInput->stop();
		clientBodyBuffer->reset();
		clientOutputPipe->reset();
		clientOutputWatcher.stop();

		appInput->stop();
		appOutputWatcher.stop();

		timeoutTimer.stop();

		freeScopeLogs();

		requestHandler = NULL;
	}

	bool reassociateable() const {
		return requestHandler == NULL
			&& backgroundOperations == 0
			&& clientInput->resetable()
			&& clientBodyBuffer->resetable()
			&& clientOutputPipe->resetable()
			&& appInput->resetable();
	}

	string name() const {
		if (fdnum == -1) {
			return "(null)";
		} else {
			return toString(fdnum);
		}
	}

	bool connected() const {
		return requestHandler != NULL;
	}

	const char *getStateName() const {
		switch (state) {
		case BEGIN_READING_CONNECT_PASSWORD:
			return "BEGIN_READING_CONNECT_PASSWORD";
		case STILL_READING_CONNECT_PASSWORD:
			return "STILL_READING_CONNECT_PASSWORD";
		case READING_HEADER:
			return "READING_HEADER";
		case BUFFERING_REQUEST_BODY:
			return "BUFFERING_REQUEST_BODY";
		case CHECKING_OUT_SESSION:
			return "CHECKING_OUT_SESSION";
		case SENDING_HEADER_TO_APP:
			return "SENDING_HEADER_TO_APP";
		case FORWARDING_BODY_TO_APP:
			return "FORWARDING_BODY_TO_APP";
		case WRITING_SIMPLE_RESPONSE:
			return "WRITING_SIMPLE_RESPONSE";
		case DISCONNECTED:
			return "DISCONNECTED";
		default:
			return "UNKNOWN";
		}
	}

	void freeBufferedConnectPassword() {
		if (bufferedConnectPassword.data != NULL) {
			free(bufferedConnectPassword.data);
			bufferedConnectPassword.data = NULL;
			bufferedConnectPassword.alreadyRead = 0;
		}
	}

	/**
	 * Checks whether we should half-close the application socket after forwarding
	 * the request. HTTP does not formally support half-closing, and Node.js treats a
	 * half-close as a full close, so we only half-close session sockets, not
	 * HTTP sockets.
	 */
	bool shouldHalfCloseWrite() const {
		return session->getProtocol() == "session";
	}

	bool useUnionStation() const {
		return options.transaction != NULL;
	}

	UnionStation::TransactionPtr getUnionStationTransaction() const {
		return options.transaction;
	}

	void beginScopeLog(UnionStation::ScopeLog **scopeLog, const char *name) {
		if (options.transaction != NULL) {
			*scopeLog = new UnionStation::ScopeLog(options.transaction, name);
		}
	}

	void endScopeLog(UnionStation::ScopeLog **scopeLog, bool success = true) {
		if (success && *scopeLog != NULL) {
			(*scopeLog)->success();
		}
		delete *scopeLog;
		*scopeLog = NULL;
	}

	void logMessage(const StaticString &message) {
		options.transaction->message(message);
	}

	void verifyInvariants() const {
		assert((requestHandler == NULL) == (fd == -1));
		assert((requestHandler == NULL) == (state == DISCONNECTED));
	}

	template<typename Stream>
	void inspect(Stream &stream) const {
		const char *indent = "    ";
		time_t the_time;
		struct tm the_tm;
		char timestr[60];

		the_time = (time_t) connectedAt;
		localtime_r(&the_time, &the_tm);
		strftime(timestr, sizeof(timestr) - 1, "%F %H:%M:%S", &the_tm);

		stream << indent << "host                        = " << (scgiParser.getHeader("HTTP_HOST").empty() ? "(empty)" : scgiParser.getHeader("HTTP_HOST")) << "\n";
		stream << indent << "uri                         = " << (scgiParser.getHeader("REQUEST_URI").empty() ? "(empty)" : scgiParser.getHeader("REQUEST_URI")) << "\n";
		stream << indent << "connected at                = " << timestr << " (" << (unsigned long long) (ev_time() - connectedAt) << " sec ago)\n";
		stream << indent << "state                       = " << getStateName() << "\n";
		if (session == NULL) {
			stream << indent << "session                     = NULL\n";
		} else {
			stream << indent << "session pid                 = " << session->getPid() << " (" <<
				session->getGroup()->name << ")\n";
			stream << indent << "session gupid               = " << session->getGupid() << "\n";
			stream << indent << "session initiated           = " << boolStr(session->initiated()) << "\n";
		}
		stream
			<< indent << "requestBodyIsBuffered       = " << boolStr(requestBodyIsBuffered) << "\n"
			<< indent << "requestIsChunked            = " << boolStr(requestIsChunked) << "\n"
			<< indent << "requestBodyLength           = " << requestBodyLength << "\n"
			<< indent << "requestBodyAlreadyRead      = " << requestBodyAlreadyRead << "\n"
			<< indent << "responseContentLength       = " << responseContentLength << "\n"
			<< indent << "responseBodyAlreadyRead     = " << responseBodyAlreadyRead << "\n"
			<< indent << "clientInput                 = " << clientInput.get() <<  " " << clientInput->inspect() << "\n"
			<< indent << "clientInput started         = " << boolStr(clientInput->isStarted()) << "\n"
			<< indent << "clientBodyBuffer started    = " << boolStr(clientBodyBuffer->isStarted()) << "\n"
			<< indent << "clientBodyBuffer reachedEnd = " << boolStr(clientBodyBuffer->reachedEnd()) << "\n"
			<< indent << "clientOutputPipe started    = " << boolStr(clientOutputPipe->isStarted()) << "\n"
			<< indent << "clientOutputPipe reachedEnd = " << boolStr(clientOutputPipe->reachedEnd()) << "\n"
			<< indent << "clientOutputWatcher active  = " << boolStr(clientOutputWatcher.is_active()) << "\n"
			<< indent << "appInput                    = " << appInput.get() << " " << appInput->inspect() << "\n"
			<< indent << "appInput started            = " << boolStr(appInput->isStarted()) << "\n"
			<< indent << "appInput reachedEnd         = " << boolStr(appInput->endReached()) << "\n"
			<< indent << "responseHeaderSeen          = " << boolStr(responseHeaderSeen) << "\n"
			<< indent << "useUnionStation             = " << boolStr(useUnionStation()) << "\n"
			;
	}
};

typedef boost::shared_ptr<Client> ClientPtr;


class RequestHandler {
public:
	enum BenchmarkPoint {
		BP_NONE,
		BP_AFTER_ACCEPT,
		BP_AFTER_CHECK_CONNECT_PASSWORD,
		BP_AFTER_PARSING_HEADER,
		BP_BEFORE_CHECKOUT_SESSION
	};

private:
	friend class Client;

	const SafeLibevPtr libev;
	FileDescriptor requestSocket;
	PoolPtr pool;
	const AgentOptions &options;
	const ResourceLocator resourceLocator;
	UnionStation::CorePtr unionStationCore;
	ev::io requestSocketWatcher;
	ev::timer resumeSocketWatcherTimer;
	HashMap<int, ClientPtr> clients;
	Timer inactivityTimer;
	bool accept4Available;
	boost::regex upgradeHeaderRegex;


	void disconnect(const ClientPtr &client) {
		// Prevent Client object from being destroyed until we're done.
		ClientPtr reference = client;

		clients.erase(client->fd);
		client->discard();
		client->verifyInvariants();
		RH_DEBUG(client, "Disconnected; new client count = " << clients.size());

		if (clients.empty()) {
			inactivityTimer.start();
		}
	}

	void disconnectWithError(const ClientPtr &client, const StaticString &message) {
		RH_WARN(client, "Disconnecting with error: " << message);
		if (client->useUnionStation()) {
			client->logMessage("Disconnecting with error: " + message);
		}
		disconnect(client);
	}

	void disconnectWithClientSocketWriteError(const ClientPtr &client, int e) {
		stringstream message;
		message << "client socket write error: ";
		message << strerror(e);
		message << " (errno=" << e << ")";
		disconnectWithError(client, message.str());
	}

	void disconnectWithAppSocketWriteError(const ClientPtr &client, int e) {
		stringstream message;
		message << "app socket write error: ";
		message << strerror(e);
		message << " (errno=" << e << ")";
		disconnectWithError(client, message.str());
	}

	void disconnectWithWarning(const ClientPtr &client, const StaticString &message) {
		P_DEBUG("Disconnected client " << client->name() << " with warning: " << message);
		disconnect(client);
	}

	template<typename Number>
	static Number clamp(Number n, Number min, Number max) {
		if (n < min) {
			return min;
		} else if (n > max) {
			return max;
		} else {
			return n;
		}
	}

	// GDB helper function, implemented in .cpp file to prevent inlining.
	Client *getClientPointer(const ClientPtr &client);

	void doResetInactivityTime() {
		inactivityTimer.reset();
	}

	void getInactivityTime(unsigned long long *result) const {
		*result = inactivityTimer.elapsed();
	}

	static bool getBoolOption(const ClientPtr &client, const StaticString &name, bool defaultValue = false) {
		ScgiRequestParser::const_iterator it = client->scgiParser.getHeaderIterator(name);
		if (it != client->scgiParser.end()) {
			return it->second == "true";
		} else {
			return defaultValue;
		}
	}

	static long long getULongLongOption(const ClientPtr &client, const StaticString &name, long long defaultValue = -1) {
		ScgiRequestParser::const_iterator it = client->scgiParser.getHeaderIterator(name);
		if (it != client->scgiParser.end()) {
			long long result = stringToULL(it->second);
			// The client may send a malicious integer, so check for this.
			if (result < 0) {
				return defaultValue;
			} else {
				return result;
			}
		} else {
			return defaultValue;
		}
	}

	bool friendlyErrorPagesEnabled(const ClientPtr &client) const {
		bool defaultValue = client->options.environment != "staging"
			&& client->options.environment != "production";
		return getBoolOption(client, "PASSENGER_FRIENDLY_ERROR_PAGES", defaultValue);
	}

	void writeSimpleResponse(const ClientPtr &client, const StaticString &data, int code = 200) {
		char header[256], statusBuffer[50];
		char *pos = header;
		const char *end = header + sizeof(header) - 1;
		const char *status;

		status = getStatusCodeAndReasonPhrase(code);
		if (status == NULL) {
			snprintf(statusBuffer, sizeof(statusBuffer), "%d Unknown Reason-Phrase", code);
			status = statusBuffer;
		}

		if (getBoolOption(client, "PASSENGER_STATUS_LINE", true)) {
			pos += snprintf(pos, end - pos, "HTTP/1.1 %s\r\n",
				status);
		}
		pos += snprintf(pos, end - pos,
			"Status: %s\r\n"
			"Content-Length: %lu\r\n"
			"Content-Type: text/html; charset=UTF-8\r\n"
			"Cache-Control: no-cache, no-store, must-revalidate\r\n"
			"\r\n",
			status, (unsigned long) data.size());

		client->clientOutputPipe->write(header, pos - header);
		if (!client->connected()) {
			return;
		}
		client->clientOutputPipe->write(data.data(), data.size());
		if (!client->connected()) {
			return;
		}
		client->clientOutputPipe->end();
		if (!client->connected()) {
			return;
		}

		if (client->useUnionStation()) {
			snprintf(header, end - header, "Status: %d %s",
				code, status);
			client->logMessage(header);
		}
	}

	void writeErrorResponse(const ClientPtr &client, const StaticString &message, const SpawnException *e = NULL) {
		assert(client->state < Client::FORWARDING_BODY_TO_APP);
		client->state = Client::WRITING_SIMPLE_RESPONSE;

		ErrorRenderer renderer(resourceLocator);
		string data;

		if (friendlyErrorPagesEnabled(client)) {
			try {
				data = renderer.renderWithDetails(message, client->options, e);
			} catch (const SystemException &e2) {
				P_ERROR("Cannot render an error page: " << e2.what() << "\n" <<
					e2.backtrace());
				data = message;
			}
		} else {
			try {
				data = renderer.renderWithoutDetails();
			} catch (const SystemException &e2) {
				P_ERROR("Cannot render an error page: " << e2.what() << "\n" <<
					e2.backtrace());
				data = "Internal Server Error";
			}
		}

		stringstream str;
		if (getBoolOption(client, "PASSENGER_STATUS_LINE", true)) {
			str << "HTTP/1.1 500 Internal Server Error\r\n";
		}
		str << "Status: 500 Internal Server Error\r\n";
		str << "Content-Length: " << data.size() << "\r\n";
		str << "Content-Type: text/html; charset=UTF-8\r\n";
		str << "Cache-Control: no-cache, no-store, must-revalidate\r\n";
		str << "\r\n";

		const string header = str.str();
		client->clientOutputPipe->write(header.data(), header.size());
		if (!client->connected()) {
			return;
		}
		client->clientOutputPipe->write(data.data(), data.size());
		if (!client->connected()) {
			return;
		}
		client->clientOutputPipe->end();
		if (!client->connected()) {
			return;
		}

		if (client->useUnionStation()) {
			client->logMessage("Status: 500 Internal Server Error");
			// TODO: record error message
		}
	}

	static BenchmarkPoint getDefaultBenchmarkPoint() {
		const char *val = getenv("PASSENGER_REQUEST_HANDLER_BENCHMARK_POINT");
		if (val == NULL || *val == '\0') {
			return BP_NONE;
		} else if (strcmp(val, "after_accept") == 0) {
			return BP_AFTER_ACCEPT;
		} else if (strcmp(val, "after_check_connect_password") == 0) {
			return BP_AFTER_CHECK_CONNECT_PASSWORD;
		} else if (strcmp(val, "after_parsing_header") == 0) {
			return BP_AFTER_PARSING_HEADER;
		} else if (strcmp(val, "before_checkout_session") == 0) {
			return BP_BEFORE_CHECKOUT_SESSION;
		} else {
			P_WARN("Invalid RequestHandler benchmark point requested: " << val);
			return BP_NONE;
		}
	}


	/*****************************************************
	 * COMPONENT: appInput -> clientOutputPipe plumbing
	 *
	 * The following code receives data from appInput,
	 * possibly modifies it, and forwards it to
	 * clientOutputPipe.
	 *****************************************************/

	struct Header {
		StaticString key;
		StaticString value;

		Header() { }

		Header(const StaticString &_key, const StaticString &_value)
			: key(_key),
			  value(_value)
			{ }

		bool empty() const {
			return key.empty();
		}

		const char *begin() const {
			return key.data();
		}

		const char *end() const {
			return value.data() + value.size() + sizeof("\r\n") - 1;
		}

		size_t size() const {
			return end() - begin();
		}
	};

	/** Given a substring containing the start of the header value,
	 * extracts the substring that contains a single header value.
	 *
	 *   const char *data =
	 *      "Status: 200 OK\r\n"
	 *      "Foo: bar\r\n";
	 *   extractHeaderValue(data + strlen("Status:"), strlen(data) - strlen("Status:"));
	 *      // "200 OK"
	 */
	static StaticString extractHeaderValue(const char *data, size_t size) {
		const char *start = data;
		const char *end   = data + size;
		const char *terminator;

		while (start < end && *start == ' ') {
			start++;
		}

		terminator = (const char *) memchr(start, '\r', end - start);
		if (terminator == NULL) {
			return StaticString();
		} else {
			return StaticString(start, terminator - start);
		}
	}

	static Header lookupHeader(const StaticString &headerData, const StaticString &name) {
		string::size_type searchStart = 0;
		while (searchStart < headerData.size()) {
			string::size_type pos = headerData.find(name, searchStart);
			if (OXT_UNLIKELY(pos == string::npos)) {
				return Header();
			} else if ((pos == 0 || headerData[pos - 1] == '\n')
				&& headerData.size() > pos + name.size()
				&& headerData[pos + name.size()] == ':')
			{
				StaticString value = extractHeaderValue(
					headerData.data() + pos + name.size() + 1,
					headerData.size() - pos - name.size() - 1);
				return Header(headerData.substr(pos, name.size()), value);
			} else {
				searchStart = pos + name.size() + 1;
			}
		}
		return Header();
	}

	static Header lookupHeader(const StaticString &headerData, const StaticString &name,
		const StaticString &name2)
	{
		Header header = lookupHeader(headerData, name);
		if (header.empty()) {
			header = lookupHeader(headerData, name2);
		}
		return header;
	}

	bool addStatusHeaderFromStatusLine(const ClientPtr &client, string &headerData) {
		string::size_type begin, end;

		begin = headerData.find(' ');
		if (begin != string::npos) {
			end = headerData.find("\r\n", begin + 1);
		} else {
			end = string::npos;
		}
		if (begin != string::npos && end != string::npos) {
			StaticString statusValue(headerData.data() + begin + 1, end - begin);
			if (statusValue.size() <= MAX_STATUS_HEADER_SIZE) {
				char header[MAX_STATUS_HEADER_SIZE + sizeof("Status: \r\n")];
				char *pos = header;
				const char *end = header + sizeof(header);

				pos = appendData(pos, end, "Status: ");
				pos = appendData(pos, end, statusValue);
				pos = appendData(pos, end, "\r\n");
				headerData.append(StaticString(header, pos - header));
				return true;
			} else {
				disconnectWithError(client, "application sent malformed response: the Status header's (" +
					statusValue + ") exceeds the allowed limit of " +
					toString(MAX_STATUS_HEADER_SIZE) + " bytes.");
				return false;
			}
		} else {
			disconnectWithError(client, "application sent malformed response: the HTTP status line is invalid.");
			return false;
		}
	}

	static bool addReasonPhrase(string &headerData, const Header &status) {
		if (status.value.find(' ') == string::npos) {
			int statusCode = stringToInt(status.value);
			const char *statusCodeAndReasonPhrase = getStatusCodeAndReasonPhrase(statusCode);
			char newStatus[100];
			char *pos = newStatus;
			const char *end = newStatus + sizeof(newStatus);

			pos = appendData(pos, end, "Status: ");
			if (statusCodeAndReasonPhrase == NULL) {
				pos = appendData(pos, end, toString(statusCode));
				pos = appendData(pos, end, " Unknown Reason-Phrase\r\n");
			} else {
				pos = appendData(pos, end, statusCodeAndReasonPhrase);
				pos = appendData(pos, end, "\r\n");
			}

			headerData.replace(status.begin() - headerData.data(), status.size(),
				newStatus, pos - newStatus);
			return true;
		} else {
			return false;
		}
	}

	bool removeStatusLine(const ClientPtr &client, string &headerData) {
		string::size_type end = headerData.find("\r\n");
		if (end != string::npos) {
			headerData.erase(0, end + 2);
			return true;
		} else {
			disconnectWithError(client, "application sent malformed response: the HTTP status line is invalid.");
			return false;
		}
	}

	static void addStatusLineFromStatusHeader(string &headerData, const Header &status) {
		char statusLine[100];
		char *pos = statusLine;
		const char *end = statusLine + sizeof(statusLine);

		pos = appendData(pos, end, "HTTP/1.1 ");
		pos = appendData(pos, end, status.value);
		pos = appendData(pos, end, "\r\n");

		headerData.insert(0, statusLine, pos - statusLine);
	}

	static void removeHeader(string &headerData, const Header &header) {
		headerData.erase(header.begin() - headerData.data(), header.size());
	}

	/*
	 * Given a full header, possibly modify the header and send it to the clientOutputPipe.
	 */
	bool processResponseHeader(const ClientPtr &client,
		const StaticString &origHeaderData)
	{
		string headerData;
		headerData.reserve(origHeaderData.size() + 150);
		// Strip trailing CRLF.
		headerData.append(origHeaderData.data(), origHeaderData.size() - 2);

		if (startsWith(headerData, "HTTP/1.")) {
			Header status = lookupHeader(headerData, "Status", "status");
			if (status.empty()) {
				// Add status header if necessary.
				if (!addStatusHeaderFromStatusLine(client, headerData)) {
					return false;
				}
			} else {
				// Add reason phrase to existing status header if necessary.
				addReasonPhrase(headerData, status);
			}
			// Remove status line if necesary.
			if (!getBoolOption(client, "PASSENGER_STATUS_LINE", true)) {
				if (!removeStatusLine(client, headerData)) {
					return false;
				}
			}
		} else {
			Header status = lookupHeader(headerData, "Status", "status");
			if (!status.empty()) {
				// Add reason phrase to status header if necessary.
				if (addReasonPhrase(headerData, status)) {
					status = lookupHeader(headerData, "Status", "status");
				}
				// Add status line if necessary.
				if (getBoolOption(client, "PASSENGER_STATUS_LINE", true)) {
					addStatusLineFromStatusHeader(headerData, status);
				}
			} else {
				disconnectWithError(client, "application sent malformed response: it didn't send an HTTP status line or a Status header.");
				return false;
			}
		}

		if (client->useUnionStation()) {
			Header status = lookupHeader(headerData, "Status", "status");
			string message = "Status: ";
			message.append(status.value);
			client->logMessage(message);
		}

		// Process chunked transfer encoding.
		Header transferEncoding = lookupHeader(headerData, "Transfer-Encoding", "transfer-encoding");
		if (!transferEncoding.empty() && transferEncoding.value == "chunked") {
			RH_TRACE(client, 3, "Response with chunked transfer encoding detected.");
			client->chunkedResponse = true;
			removeHeader(headerData, transferEncoding);
		} else {
			Header contentLength = lookupHeader(headerData, "Content-Length", "content-length");
			if (!contentLength.empty()) {
				client->responseContentLength = stringToLL(contentLength.value);
			}
		}

		Header connection = lookupHeader(headerData, "Connection", "connection");
		if (!connection.empty() && (connection.value == "keep-alive"
			|| connection.value == "Keep-Alive"))
		{
			RH_TRACE(client, 3, "Keep-alive response detected. Changing to non-keep alive.");
			removeHeader(headerData, connection);
			headerData.append("Connection: close\r\n");
		}

		// Add X-Powered-By.
		if (getBoolOption(client, "PASSENGER_SHOW_VERSION_IN_HEADER", true)) {
			headerData.append("X-Powered-By: Phusion Passenger " PASSENGER_VERSION "\r\n");
		} else {
			headerData.append("X-Powered-By: Phusion Passenger\r\n");
		}

		// Add sticky session ID.
		if (client->stickySession && client->session != NULL) {
			StaticString baseURI = client->scgiParser.getHeader("SCRIPT_NAME");
			if (baseURI.empty()) {
				baseURI = StaticString("/", 1);
			}

			StaticString cookieName = getStickySessionCookieName(client);
			// Note that we do NOT set HttpOnly. If we set that flag then Chrome
			// doesn't send cookies over WebSocket handshakes. Confirmed on Chrome 25.
			headerData.append("Set-Cookie: ");
			headerData.append(cookieName.data(), cookieName.size());
			headerData.append("=");
			headerData.append(toString(client->session->getStickySessionId()));
			headerData.append("; Path=");
			headerData.append(baseURI.data(), baseURI.size());
			headerData.append("\r\n");

			// Invalidate all cookies with a different route.
			//
			// TODO: This is not entirely correct. Clients MAY send multiple Cookie
			// headers, although this is in practice extremely rare.
			// http://stackoverflow.com/questions/16305814/are-multiple-cookie-headers-allowed-in-an-http-request
			StaticString cookieHeader = client->scgiParser.getHeader("HTTP_COOKIE");
			vector< pair<StaticString, StaticString> > cookies;
			pair<StaticString, StaticString> cookie;

			parseCookieHeader(cookieHeader, cookies);

			foreach (cookie, cookies) {
				if (cookie.first == cookieName) {
					unsigned int stickySessionId = stringToUint(cookie.second);
					if (stickySessionId != client->session->getStickySessionId()) {
						headerData.append("Set-Cookie: ");
						headerData.append(cookie.first.data(), cookie.first.size());
						headerData.append("=");
						headerData.append(cookie.second.data(), cookie.second.size());
						headerData.append("; Path=");
						headerData.append(baseURI.data(), baseURI.size());
						headerData.append("; Expires=Thu, 01 Jan 1970 00:00:00 GMT\r\n");
					}
				}
			}
		}

		// Add Date header. https://code.google.com/p/phusion-passenger/issues/detail?id=485
		if (lookupHeader(headerData, "Date", "date").empty()) {
			char dateStr[60];
			char *pos = dateStr;
			const char *end = dateStr + sizeof(dateStr) - 1;
			time_t the_time = time(NULL);
			struct tm the_tm;

			pos = appendData(pos, end, "Date: ");
			gmtime_r(&the_time, &the_tm);
			pos += strftime(pos, end - pos, "%a, %d %b %Y %H:%M:%S %Z", &the_tm);
			pos = appendData(pos, end, "\r\n");
			headerData.append(dateStr, pos - dateStr);
		}

		// Detect out of band work request
		Header oobw = lookupHeader(headerData, "X-Passenger-Request-OOB-Work", "x-passenger-request-oob-work");
		if (!oobw.empty()) {
			P_TRACE(3, "Response with oobw detected.");
			if (client->session != NULL) {
				client->session->requestOOBW();
			}
			removeHeader(headerData, oobw);
		}

		P_TRACE(2, "Fowarding response header from app client: " << headerData);
		headerData.append("\r\n");
		writeToClientOutputPipe(client, headerData);
		return true;
	}

	void writeToClientOutputPipe(const ClientPtr &client, const StaticString &data) {
		bool wasCommittingToDisk = client->clientOutputPipe->isCommittingToDisk();
		bool nowCommittingToDisk = !client->clientOutputPipe->write(data.data(), data.size());
		if (!client->connected()) {
			// EPIPE/ECONNRESET detected.
			return;
		}
		if (!wasCommittingToDisk && nowCommittingToDisk) {
			RH_TRACE(client, 3, "Buffering response data to disk; temporarily stopping application socket.");
			client->backgroundOperations++;
			// If the data comes from writeErrorResponse(), then appInput is not available.
			if (client->session != NULL && client->session->initiated()) {
				client->appInput->stop();
			}
		}
	}

	size_t onAppInputData(const ClientPtr &client, const StaticString &data) {
		RH_LOG_EVENT(client, "onAppInputData");
		if (!client->connected()) {
			return 0;
		}

		if (!data.empty()) {
			RH_TRACE(client, 3, "Application sent data: \"" << cEscapeString(data) << "\"");

			// Buffer the application response until we've encountered the end of the header.
			if (!client->responseHeaderSeen) {
				size_t consumed = client->responseHeaderBufferer.feed(data.data(), data.size());
				if (!client->responseHeaderBufferer.acceptingInput()) {
					if (client->responseHeaderBufferer.hasError()) {
						disconnectWithError(client, "application response format error (invalid header)");
					} else {
						// Now that we have a full header, do something with it.
						RH_TRACE(client, 3, "Response header fully buffered");
						client->responseHeaderSeen = true;
						StaticString header = client->responseHeaderBufferer.getData();
						if (processResponseHeader(client, header)) {
							if (client->responseContentLength == 0) {
								RH_TRACE(client, 3, "Disconnecting client because response Content-Length = 0");
								onAppInputEof(client);
							}
							return consumed;
						} else {
							assert(!client->connected());
						}
					}
				}
			// The header has already been processed so forward it
			// directly to clientOutputPipe, possibly through a
			// dechunker first.
			} else if (client->chunkedResponse) {
				client->responseDechunker.feed(data.data(), data.size());
			} else {
				onAppInputChunk(client, data);
			}
			return data.size();

		} else {
			onAppInputEof(client);
			return 0;
		}
	}

	void onAppInputChunk(const ClientPtr &client, const StaticString &data) {
		RH_LOG_EVENT(client, "onAppInputChunk");
		StaticString data2;

		if (client->responseContentLength == -1) {
			data2 = data;
		} else {
			size_t rest = client->responseContentLength -
				client->responseBodyAlreadyRead;
			data2 = StaticString(data.data(), std::min<size_t>(
				rest, data.size()));
		}

		client->responseBodyAlreadyRead += data2.size();
		assert(client->responseContentLength == -1 || client->responseBodyAlreadyRead <=
			(unsigned long long) client->responseContentLength);
		if (data2.empty()) {
			// Client sent more data than was advertised through
			// Content-Length. Ignore them.
			return;
		}

		writeToClientOutputPipe(client, data2);

		if (client->responseContentLength > 0) {
			RH_TRACE(client, 3, client->responseBodyAlreadyRead << "/" <<
				client->responseContentLength <<
				" bytes of application data forwarded so far.");

			if (client->connected() && (unsigned long long) client->responseContentLength
				== client->responseBodyAlreadyRead)
			{
				RH_TRACE(client, 3, "Disconnecting client because application data has been fully forwarded.");
				onAppInputEof(client);
			}
		}
	}

	void onAppInputChunkEnd(const ClientPtr &client) {
		RH_LOG_EVENT(client, "onAppInputChunkEnd");
		onAppInputEof(client);
	}

	void onAppInputEof(const ClientPtr &client) {
		RH_LOG_EVENT(client, "onAppInputEof");
		// Check for session == NULL in order to avoid executing the code twice on
		// responses with chunked encoding.
		// This also ensures that when onAppInputEof() is called twice (e.g. because
		// additional data was received after onAppInputChunk has already called onAppInputEof()),
		// we don't do things twice.
		if (!client->connected() || client->session == NULL) {
			return;
		}

		RH_DEBUG(client, "Application sent EOF");
		client->appInput->stop();
		client->session.reset();
		client->endScopeLog(&client->scopeLogs.requestProxying);
		client->clientOutputPipe->end();
	}

	void onAppInputError(const ClientPtr &client, const char *message, int errorCode) {
		RH_LOG_EVENT(client, "onAppInputError");
		if (!client->connected()) {
			return;
		}

		if (errorCode == ECONNRESET) {
			// We might as well treat ECONNRESET like an EOF.
			// http://stackoverflow.com/questions/2974021/what-does-econnreset-mean-in-the-context-of-an-af-local-socket
			onAppInputEof(client);
		} else {
			stringstream message;
			message << "application socket read error: ";
			message << strerror(errorCode);
			message << " (fd=" << client->appInput->getFd();
			message << ", errno=" << errorCode << ")";
			disconnectWithError(client, message.str());
		}
	}

	void onClientOutputPipeCommit(const ClientPtr &client) {
		RH_LOG_EVENT(client, "onClientOutputPipeCommit");
		if (!client->connected()) {
			return;
		}

		RH_TRACE(client, 3, "Done buffering response data to disk; resuming application socket.");
		client->backgroundOperations--;
		// If the data comes from writeErrorResponse(), then appInput is not available.
		if (client->session != NULL && client->session->initiated()) {
			client->appInput->start();
		}
	}


	/*****************************************************
	 * COMPONENT: clientOutputPipe -> client fd plumbing
	 *
	 * The following code handles forwarding data from
	 * clientOutputPipe to the client socket.
	 *****************************************************/

	void onClientOutputPipeData(const ClientPtr &client, const char *data,
		size_t size, const FileBackedPipe::ConsumeCallback &consumed)
	{
		RH_LOG_EVENT(client, "onClientOutputPipeData");
		if (!client->connected()) {
			return;
		}

		RH_TRACE(client, 3, "Forwarding " << size << " bytes of application data to client.");
		ssize_t ret = syscalls::write(client->fd, data, size);
		if (ret == -1) {
			int e = errno;
			RH_TRACE(client, 3, "Could not write to client socket: " << strerror(e) << " (errno=" << e << ")");
			if (e == EAGAIN) {
				RH_TRACE(client, 3, "Waiting until the client socket is writable again.");
				client->clientOutputWatcher.start();
				consumed(0, true);
			} else if (e == EPIPE || e == ECONNRESET) {
				// If the client closed the connection then disconnect quietly.
				RH_TRACE(client, 3, "Client stopped reading prematurely");
				if (client->useUnionStation()) {
					client->logMessage("Disconnecting: client stopped reading prematurely");
				}
				disconnect(client);
			} else {
				disconnectWithClientSocketWriteError(client, e);
			}
		} else {
			RH_TRACE(client, 3, "Managed to forward " << ret << " bytes.");
			consumed(ret, false);
		}
	}

	void onClientOutputPipeEnd(const ClientPtr &client) {
		RH_LOG_EVENT(client, "onClientOutputPipeEnd");
		if (!client->connected()) {
			return;
		}

		RH_TRACE(client, 2, "Client output pipe ended; disconnecting client");
		client->endScopeLog(&client->scopeLogs.requestProcessing);
		disconnect(client);
	}

	void onClientOutputPipeError(const ClientPtr &client, int errorCode) {
		RH_LOG_EVENT(client, "onClientOutputPipeError");
		if (!client->connected()) {
			return;
		}

		stringstream message;
		message << "client output pipe error: ";
		message << strerror(errorCode);
		message << " (errno=" << errorCode << ")";
		disconnectWithError(client, message.str());
	}

	void onClientOutputWritable(const ClientPtr &client) {
		RH_LOG_EVENT(client, "onClientOutputWritable");
		if (!client->connected()) {
			return;
		}

		// Continue forwarding output data to the client.
		RH_TRACE(client, 3, "Client socket became writable again.");
		client->clientOutputWatcher.stop();
		assert(!client->clientOutputPipe->isStarted());
		client->clientOutputPipe->start();
	}


	/*****************************************************
	 * COMPONENT: client acceptor
	 *
	 * The following code accepts new client connections
	 * and forwards events to the appropriate functions
	 * depending on the client state.
	 *****************************************************/

	FileDescriptor acceptNonBlockingSocket(int sock) {
		union {
			struct sockaddr_in inaddr;
			struct sockaddr_un unaddr;
		} u;
		socklen_t addrlen = sizeof(u);

		if (accept4Available) {
			FileDescriptor fd(callAccept4(requestSocket,
				(struct sockaddr *) &u, &addrlen, O_NONBLOCK));
			// FreeBSD returns EINVAL if accept4() is called with invalid flags.
			if (fd == -1 && (errno == ENOSYS || errno == EINVAL)) {
				accept4Available = false;
				return acceptNonBlockingSocket(sock);
			} else {
				return fd;
			}
		} else {
			FileDescriptor fd(syscalls::accept(requestSocket,
				(struct sockaddr *) &u, &addrlen));
			if (fd != -1) {
				int e = errno;
				setNonBlocking(fd);
				errno = e;
			}
			return fd;
		}
	}


	void onResumeSocketWatcher(ev::timer &timer, int revents) {
		P_INFO("Resuming listening on server socket.");
		resumeSocketWatcherTimer.stop();
		requestSocketWatcher.start();
	}

	void onAcceptable(ev::io &io, int revents) {
		bool endReached = false;
		unsigned int count = 0;
		unsigned int maxAcceptTries = clamp<unsigned int>(clients.size(), 1, 10);
		ClientPtr acceptedClients[10];

		while (!endReached && count < maxAcceptTries) {
			FileDescriptor fd = acceptNonBlockingSocket(requestSocket);
			if (fd == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					endReached = true;
				} else {
					int e = errno;
					P_ERROR("Cannot accept client: " << strerror(e) <<
						" (errno=" << e << "). " <<
						"Pausing listening on server socket for 3 seconds. " <<
						"Current client count: " << clients.size());
					requestSocketWatcher.stop();
					resumeSocketWatcherTimer.start();
					endReached = true;
				}
			} else if (benchmarkPoint == BP_AFTER_ACCEPT) {
				writeExact(fd,
					"HTTP/1.1 200 OK\r\n"
					"Status: 200 OK\r\n"
					"Content-Type: text/html\r\n"
					"Connection: close\r\n"
					"\r\n"
					"Benchmark point: after_accept\n");
			} else {
				ClientPtr client = boost::make_shared<Client>();
				client->associate(this, fd);
				clients.insert(make_pair((int) fd, client));
				acceptedClients[count] = client;
				count++;
				RH_DEBUG(client, "New client accepted; new client count = " << clients.size());
			}
		}

		for (unsigned int i = 0; i < count; i++) {
			acceptedClients[i]->clientInput->readNow();
		}

		if (OXT_LIKELY(!clients.empty())) {
			inactivityTimer.stop();
		}
	}


	size_t onClientInputData(const ClientPtr &client, const StaticString &data) {
		RH_LOG_EVENT(client, "onClientInputData");
		if (!client->connected()) {
			return 0;
		}

		if (data.empty()) {
			onClientEof(client);
			return 0;
		} else {
			return onClientRealData(client, data.data(), data.size());
		}
	}

	size_t onClientRealData(const ClientPtr &client, const char *buf, size_t size) {
		size_t consumed = 0;

		while (consumed < size && client->connected() && client->clientInput->isStarted()) {
			const char *data = buf + consumed;
			size_t len       = size - consumed;
			size_t locallyConsumed;

			RH_TRACE(client, 3, "Processing client data: \"" << cEscapeString(StaticString(data, len)) << "\"");
			switch (client->state) {
			case Client::BEGIN_READING_CONNECT_PASSWORD:
				locallyConsumed = state_beginReadingConnectPassword_onClientData(client, data, len);
				break;
			case Client::STILL_READING_CONNECT_PASSWORD:
				locallyConsumed = state_stillReadingConnectPassword_onClientData(client, data, len);
				break;
			case Client::READING_HEADER:
				locallyConsumed = state_readingHeader_onClientData(client, data, len);
				break;
			case Client::BUFFERING_REQUEST_BODY:
				locallyConsumed = state_bufferingRequestBody_onClientData(client, data, len);
				break;
			case Client::FORWARDING_BODY_TO_APP:
				locallyConsumed = state_forwardingBodyToApp_onClientData(client, data, len);
				break;
			default:
				abort();
			}

			consumed += locallyConsumed;
			RH_TRACE(client, 3, "Processed client data: consumed " << locallyConsumed << " bytes");
			assert(consumed <= size);
		}

		return consumed;
	}

	void onClientEof(const ClientPtr &client) {
		RH_LOG_EVENT(client, "onClientEof; client sent EOF");
		switch (client->state) {
		case Client::BUFFERING_REQUEST_BODY:
			state_bufferingRequestBody_onClientEof(client);
			break;
		case Client::FORWARDING_BODY_TO_APP:
			state_forwardingBodyToApp_onClientEof(client);
			break;
		default:
			disconnect(client);
			break;
		}
	}

	void onClientInputError(const ClientPtr &client, const char *message, int errnoCode) {
		RH_LOG_EVENT(client, "onClientInputError");
		if (!client->connected()) {
			return;
		}

		if (errnoCode == ECONNRESET) {
			// We might as well treat ECONNRESET like an EOF.
			// http://stackoverflow.com/questions/2974021/what-does-econnreset-mean-in-the-context-of-an-af-local-socket
			RH_TRACE(client, 3, "Client socket ECONNRESET error; treating it as EOF");
			onClientEof(client);
		} else {
			stringstream message;
			message << "client socket read error: ";
			message << strerror(errnoCode);
			message << " (errno=" << errnoCode << ")";
			disconnectWithError(client, message.str());
		}
	}


	void onClientBodyBufferData(const ClientPtr &client, const char *data, size_t size, const FileBackedPipe::ConsumeCallback &consumed) {
		RH_LOG_EVENT(client, "onClientBodyBufferData");
		if (!client->connected()) {
			return;
		}

		switch (client->state) {
		case Client::FORWARDING_BODY_TO_APP:
			state_forwardingBodyToApp_onClientBodyBufferData(client, data, size, consumed);
			break;
		default:
			abort();
		}
	}

	void onClientBodyBufferError(const ClientPtr &client, int errorCode) {
		RH_LOG_EVENT(client, "onClientBodyBufferError");
		if (!client->connected()) {
			return;
		}

		stringstream message;
		message << "client body buffer error: ";
		message << strerror(errorCode);
		message << " (errno=" << errorCode << ")";
		disconnectWithError(client, message.str());
	}

	void onClientBodyBufferEnd(const ClientPtr &client) {
		RH_LOG_EVENT(client, "onClientBodyBufferEnd");
		if (!client->connected()) {
			return;
		}

		switch (client->state) {
		case Client::FORWARDING_BODY_TO_APP:
			state_forwardingBodyToApp_onClientBodyBufferEnd(client);
			break;
		default:
			abort();
		}
	}

	void onClientBodyBufferCommit(const ClientPtr &client) {
		RH_LOG_EVENT(client, "onClientBodyBufferCommit");
		if (!client->connected()) {
			return;
		}

		switch (client->state) {
		case Client::BUFFERING_REQUEST_BODY:
			state_bufferingRequestBody_onClientBodyBufferCommit(client);
			break;
		default:
			abort();
		}
	}

	void onAppOutputWritable(const ClientPtr &client) {
		RH_LOG_EVENT(client, "onAppOutputWritable");
		if (!client->connected()) {
			return;
		}

		switch (client->state) {
		case Client::SENDING_HEADER_TO_APP:
			state_sendingHeaderToApp_onAppOutputWritable(client);
			break;
		case Client::FORWARDING_BODY_TO_APP:
			state_forwardingBodyToApp_onAppOutputWritable(client);
			break;
		default:
			abort();
		}
	}


	void onTimeout(const ClientPtr &client) {
		RH_LOG_EVENT(client, "onTimeout");
		if (!client->connected()) {
			return;
		}

		switch (client->state) {
		case Client::BEGIN_READING_CONNECT_PASSWORD:
		case Client::STILL_READING_CONNECT_PASSWORD:
			disconnectWithError(client, "no connect password received within timeout");
			break;
		default:
			disconnectWithError(client, "timeout");
			break;
		}
	}


	/*****************************************************
	 * COMPONENT: client -> application plumbing
	 *
	 * The following code implements forwarding data from
	 * the client to the application. Code is seperated
	 * by client state.
	 *****************************************************/


	/******* State: BEGIN_READING_CONNECT_PASSWORD *******/

	void checkConnectPassword(const ClientPtr &client, const char *data, unsigned int len) {
		RH_TRACE(client, 3, "Given connect password: \"" << cEscapeString(StaticString(data, len)) << "\"");
		if (constantTimeCompare(StaticString(data, len), options.requestSocketPassword)) {
			RH_TRACE(client, 3, "Connect password is correct; reading header");
			client->state = Client::READING_HEADER;
			client->freeBufferedConnectPassword();
			client->timeoutTimer.stop();

			if (benchmarkPoint == BP_AFTER_CHECK_CONNECT_PASSWORD) {
				writeSimpleResponse(client, "Benchmark point: after_check_connect_password\n");
			}
		} else {
			disconnectWithError(client, "wrong connect password");
		}
	}

	size_t state_beginReadingConnectPassword_onClientData(const ClientPtr &client, const char *data, size_t size) {
		if (size >= options.requestSocketPassword.size()) {
			checkConnectPassword(client, data, options.requestSocketPassword.size());
			return options.requestSocketPassword.size();
		} else {
			client->bufferedConnectPassword.data = (char *) malloc(options.requestSocketPassword.size());
			client->bufferedConnectPassword.alreadyRead = size;
			memcpy(client->bufferedConnectPassword.data, data, size);
			client->state = Client::STILL_READING_CONNECT_PASSWORD;
			return size;
		}
	}


	/******* State: STILL_READING_CONNECT_PASSWORD *******/

	size_t state_stillReadingConnectPassword_onClientData(const ClientPtr &client, const char *data, size_t size) {
		size_t consumed = std::min<size_t>(size,
			options.requestSocketPassword.size() -
				client->bufferedConnectPassword.alreadyRead);
		memcpy(client->bufferedConnectPassword.data + client->bufferedConnectPassword.alreadyRead,
			data, consumed);
		client->bufferedConnectPassword.alreadyRead += consumed;
		if (client->bufferedConnectPassword.alreadyRead == options.requestSocketPassword.size()) {
			checkConnectPassword(client, client->bufferedConnectPassword.data,
				options.requestSocketPassword.size());
		}
		return consumed;
	}


	/******* State: READING_HEADER *******/

	bool modifyClientHeaders(const ClientPtr &client) {
		ScgiRequestParser &parser = client->scgiParser;
		ScgiRequestParser::HeaderMap &map = parser.getMap();
		ScgiRequestParser::iterator it, end = map.end();
		bool modified = false;

		/* The Rack spec specifies that HTTP_CONTENT_LENGTH and HTTP_CONTENT_TYPE must
		 * not exist and that their respective non-HTTP_ versions should exist instead.
		 */

		if ((it = map.find("HTTP_CONTENT_LENGTH")) != end) {
			if (map.find("CONTENT_LENGTH") == end) {
				map["CONTENT_LENGTH"] = it->second;
				map.erase("HTTP_CONTENT_LENGTH");
			} else {
				map.erase(it);
			}
			modified = true;
		}

		if ((it = map.find("HTTP_CONTENT_TYPE")) != end) {
			if (map.find("CONTENT_TYPE") == end) {
				map["CONTENT_TYPE"] = it->second;
				map.erase("HTTP_CONTENT_TYPE");
			} else {
				map.erase(it);
			}
			modified = true;
		}

		return modified;
	}

	void reportBadRequestAndDisconnect(const ClientPtr &client, const char *message) {
		writeSimpleResponse(client, message, 400);
		if (client->connected()) {
			disconnectWithError(client, message);
		}
	}

	void checkAndInternalizeRequestHeaders(const ClientPtr &client) {
		ScgiRequestParser &parser = client->scgiParser;
		StaticString requestMethod = parser.getHeader("REQUEST_METHOD");

		if (requestMethod.empty()) {
			reportBadRequestAndDisconnect(client, "Bad request (no request method given)");
			return;
		}

		// Check Content-Length and Transfer-Encoding.
		long long contentLength = getULongLongOption(client, "CONTENT_LENGTH");
		StaticString transferEncoding = parser.getHeader("HTTP_TRANSFER_ENCODING");
		if (contentLength != -1 && !transferEncoding.empty()) {
			reportBadRequestAndDisconnect(client, "Bad request (request may not contain both Content-Length and Transfer-Encoding)");
			return;
		}
		if (!transferEncoding.empty() && transferEncoding != "chunked") {
			reportBadRequestAndDisconnect(client, "Bad request (only Transfer-Encoding chunked is supported)");
			return;
		}
		// According to the HTTP/1.1 spec, Content-Length may not be 0.
		// We could reject the request, but some important HTTP clients are broken
		// (*cough* Ruby Net::HTTP *cough*) and fixing them is too much of
		// a pain, so we choose support it.
		if (contentLength == 0) {
			contentLength = -1;
			assert(transferEncoding.empty());
		}

		StaticString upgrade = parser.getHeader("HTTP_UPGRADE");
		const bool requestIsGetOrHead = requestMethod == "GET" || requestMethod == "HEAD";
		const bool requestBodyOffered = contentLength != -1 || !transferEncoding.empty();

		// Reject requests that have a request body and an Upgrade header.
		if (!requestIsGetOrHead && !upgrade.empty()) {
			reportBadRequestAndDisconnect(client, "Bad request (Upgrade header is only allowed for non-GET and non-HEAD requests)");
			return;
		}

		if (!requestBodyOffered) {
			if (upgrade.empty()) {
				client->requestBodyLength = 0;
			} else {
				client->requestBodyLength = -1;
			}
		} else {
			client->requestBodyLength = contentLength;
			client->requestIsChunked = !transferEncoding.empty();
		}
	}

	static void fillPoolOption(const ClientPtr &client, StaticString &field, const StaticString &name) {
		ScgiRequestParser::const_iterator it = client->scgiParser.getHeaderIterator(name);
		if (it != client->scgiParser.end()) {
			field = it->second;
		}
	}

	static void fillPoolOption(const ClientPtr &client, bool &field, const StaticString &name) {
		ScgiRequestParser::const_iterator it = client->scgiParser.getHeaderIterator(name);
		if (it != client->scgiParser.end()) {
			field = it->second == "true";
		}
	}

	static void fillPoolOption(const ClientPtr &client, unsigned int &field, const StaticString &name) {
		ScgiRequestParser::const_iterator it = client->scgiParser.getHeaderIterator(name);
		if (it != client->scgiParser.end()) {
			field = stringToUint(it->second);
		}
	}

	static void fillPoolOption(const ClientPtr &client, unsigned long &field, const StaticString &name) {
		ScgiRequestParser::const_iterator it = client->scgiParser.getHeaderIterator(name);
		if (it != client->scgiParser.end()) {
			field = stringToUint(it->second);
		}
	}

	static void fillPoolOption(const ClientPtr &client, long &field, const StaticString &name) {
		ScgiRequestParser::const_iterator it = client->scgiParser.getHeaderIterator(name);
		if (it != client->scgiParser.end()) {
			field = stringToInt(it->second);
		}
	}

	static void fillPoolOptionSecToMsec(const ClientPtr &client, unsigned int &field, const StaticString &name) {
		ScgiRequestParser::const_iterator it = client->scgiParser.getHeaderIterator(name);
		if (it != client->scgiParser.end()) {
			field = stringToUint(it->second) * 1000;
		}
	}

	void fillPoolOptions(const ClientPtr &client) {
		Options &options = client->options;
		ScgiRequestParser &parser = client->scgiParser;
		ScgiRequestParser::const_iterator it, end = client->scgiParser.end();

		options = Options();

		StaticString scriptName = parser.getHeader("SCRIPT_NAME");
		StaticString appRoot = parser.getHeader("PASSENGER_APP_ROOT");
		if (scriptName.empty()) {
			if (appRoot.empty()) {
				StaticString documentRoot = parser.getHeader("DOCUMENT_ROOT");
				if (documentRoot.empty()) {
					disconnectWithError(client, "no PASSENGER_APP_ROOT or DOCUMENT_ROOT headers set.");
					return;
				}
				client->appRoot = extractDirName(documentRoot);
				options.appRoot = client->appRoot;
			} else {
				options.appRoot = appRoot;
			}
		} else {
			if (appRoot.empty()) {
				client->appRoot = extractDirName(resolveSymlink(parser.getHeader("DOCUMENT_ROOT")));
				options.appRoot = client->appRoot;
			} else {
				options.appRoot = appRoot;
			}
			options.baseURI = scriptName;
		}

		options.ruby = this->options.defaultRubyCommand;
		options.logLevel = getLogLevel();
		options.loggingAgentAddress = this->options.loggingAgentAddress;
		options.loggingAgentUsername = "logging";
		options.loggingAgentPassword = this->options.loggingAgentPassword;
		options.defaultUser = this->options.defaultUser;
		options.defaultGroup = this->options.defaultGroup;
		fillPoolOption(client, options.appGroupName, "PASSENGER_APP_GROUP_NAME");
		fillPoolOption(client, options.appType, "PASSENGER_APP_TYPE");
		fillPoolOption(client, options.environment, "PASSENGER_APP_ENV");
		fillPoolOption(client, options.ruby, "PASSENGER_RUBY");
		fillPoolOption(client, options.python, "PASSENGER_PYTHON");
		fillPoolOption(client, options.nodejs, "PASSENGER_NODEJS");
		fillPoolOption(client, options.user, "PASSENGER_USER");
		fillPoolOption(client, options.group, "PASSENGER_GROUP");
		fillPoolOption(client, options.minProcesses, "PASSENGER_MIN_PROCESSES");
		fillPoolOption(client, options.maxProcesses, "PASSENGER_MAX_PROCESSES");
		fillPoolOption(client, options.maxRequests, "PASSENGER_MAX_REQUESTS");
		fillPoolOption(client, options.spawnMethod, "PASSENGER_SPAWN_METHOD");
		fillPoolOption(client, options.startCommand, "PASSENGER_START_COMMAND");
		fillPoolOptionSecToMsec(client, options.startTimeout, "PASSENGER_START_TIMEOUT");
		fillPoolOption(client, options.maxPreloaderIdleTime, "PASSENGER_MAX_PRELOADER_IDLE_TIME");
		fillPoolOption(client, options.maxRequestQueueSize, "PASSENGER_MAX_REQUEST_QUEUE_SIZE");
		fillPoolOption(client, options.statThrottleRate, "PASSENGER_STAT_THROTTLE_RATE");
		fillPoolOption(client, options.restartDir, "PASSENGER_RESTART_DIR");
		fillPoolOption(client, options.startupFile, "PASSENGER_STARTUP_FILE");
		fillPoolOption(client, options.loadShellEnvvars, "PASSENGER_LOAD_SHELL_ENVVARS");
		fillPoolOption(client, options.debugger, "PASSENGER_DEBUGGER");
		fillPoolOption(client, options.raiseInternalError, "PASSENGER_RAISE_INTERNAL_ERROR");
		setStickySessionId(client);
		/******************/

		for (it = client->scgiParser.begin(); it != end; it++) {
			if (!startsWith(it->first, "PASSENGER_")
			 && !startsWith(it->first, "HTTP_")
			 && it->first != "PATH_INFO"
			 && it->first != "SCRIPT_NAME"
			 && it->first != "CONTENT_LENGTH"
			 && it->first != "CONTENT_TYPE")
			{
				options.environmentVariables.push_back(*it);
			}
		}
	}

	void initializeUnionStation(const ClientPtr &client) {
		if (getBoolOption(client, "UNION_STATION_SUPPORT", false)) {
			Options &options = client->options;
			ScgiRequestParser &parser = client->scgiParser;

			StaticString key = parser.getHeader("UNION_STATION_KEY");
			StaticString filters = parser.getHeader("UNION_STATION_FILTERS");
			if (key.empty()) {
				disconnectWithError(client, "header UNION_STATION_KEY must be set.");
				return;
			}

			client->options.transaction = unionStationCore->newTransaction(
				options.getAppGroupName(), "requests", key, filters);
			if (!client->options.transaction->isNull()) {
				client->options.analytics = true;
				client->options.unionStationKey = key;
			}

			client->beginScopeLog(&client->scopeLogs.requestProcessing, "request processing");

			StaticString staticRequestMethod = parser.getHeader("REQUEST_METHOD");
			client->logMessage("Request method: " + staticRequestMethod);

			StaticString staticRequestURI = parser.getHeader("REQUEST_URI");
			if (!staticRequestURI.empty()) {
				client->logMessage("URI: " + staticRequestURI);
			} else {
				string requestURI = parser.getHeader("SCRIPT_NAME");
				requestURI.append(parser.getHeader("PATH_INFO"));
				StaticString queryString = parser.getHeader("QUERY_STRING");
				if (!queryString.empty()) {
					requestURI.append("?");
					requestURI.append(queryString);
				}
				client->logMessage("URI: " + requestURI);
			}
		}
	}

	void parseCookieHeader(const StaticString &header,
		vector< pair<StaticString, StaticString> > &cookies) const
	{
		// See http://stackoverflow.com/questions/6108207/definite-guide-to-valid-cookie-values
		// for syntax grammar.
		vector<StaticString> parts;
		vector<StaticString>::const_iterator it, it_end;

		split(header, ';', parts);
		cookies.reserve(parts.size());
		it_end = parts.end();

		for (it = parts.begin(); it != it_end; it++) {
			const char *begin = it->data();
			const char *end = it->data() + it->size();
			const char *sep;

			skipLeadingWhitespaces(&begin, end);
			skipTrailingWhitespaces(begin, &end);

			// Find the separator ('=').
			sep = (const char *) memchr(begin, '=', end - begin);
			if (sep != NULL) {
				// Valid cookie. Otherwise, ignore it.
				const char *nameEnd = sep;
				const char *valueBegin = sep + 1;

				skipTrailingWhitespaces(begin, &nameEnd);
				skipLeadingWhitespaces(&valueBegin, end);

				cookies.push_back(make_pair(
					StaticString(begin, nameEnd - begin),
					StaticString(valueBegin, end - valueBegin)
				));
			}
		}
	}

	void setStickySessionId(const ClientPtr &client) {
		ScgiRequestParser &parser = client->scgiParser;
		if (parser.getHeader("PASSENGER_STICKY_SESSIONS") == "true") {
			// TODO: This is not entirely correct. Clients MAY send multiple Cookie
			// headers, although this is in practice extremely rare.
			// http://stackoverflow.com/questions/16305814/are-multiple-cookie-headers-allowed-in-an-http-request
			StaticString cookieHeader = parser.getHeader("HTTP_COOKIE");
			StaticString cookieName = getStickySessionCookieName(client);
			vector< pair<StaticString, StaticString> > cookies;
			pair<StaticString, StaticString> cookie;

			client->stickySession = true;
			parseCookieHeader(cookieHeader, cookies);
			foreach (cookie, cookies) {
				if (cookie.first == cookieName) {
					// This cookie matches the one we're looking for.
					client->options.stickySessionId = stringToUint(cookie.second);
					return;
				}
			}
		}
	}

	StaticString getStickySessionCookieName(const ClientPtr &client) const {
		StaticString value = client->scgiParser.getHeader("PASSENGER_STICKY_SESSIONS_COOKIE_NAME");
		if (value.empty()) {
			return StaticString(DEFAULT_STICKY_SESSIONS_COOKIE_NAME,
				sizeof(DEFAULT_STICKY_SESSIONS_COOKIE_NAME) - 1);
		} else {
			return value;
		}
	}

	size_t state_readingHeader_onClientData(const ClientPtr &client, const char *data, size_t size) {
		ScgiRequestParser &parser = client->scgiParser;
		size_t consumed = parser.feed(data, size);
		if (!parser.acceptingInput()) {
			if (parser.getState() == ScgiRequestParser::ERROR) {
				if (parser.getErrorReason() == ScgiRequestParser::LIMIT_REACHED) {
					disconnectWithError(client, "SCGI header too large");
				} else {
					disconnectWithError(client, "invalid SCGI header");
				}
				return consumed;
			}

			if (benchmarkPoint == BP_AFTER_PARSING_HEADER) {
				writeSimpleResponse(client, "Benchmark point: after_parsing_header\n");
				return consumed;
			}

			bool modified = modifyClientHeaders(client);
			/* TODO: in case the headers are not modified, we only need to rebuild the header data
			 * right now because the scgiParser buffer is invalidated as soon as onClientData exits.
			 * We should figure out a way to not copy anything if we can do everything before
			 * onClientData exits.
			 */
			parser.rebuildData(modified);

			checkAndInternalizeRequestHeaders(client);
			if (!client->connected()) {
				return consumed;
			}
			fillPoolOptions(client);
			if (!client->connected()) {
				return consumed;
			}
			initializeUnionStation(client);
			if (!client->connected()) {
				return consumed;
			}

			if (getBoolOption(client, "PASSENGER_BUFFERING")) {
				RH_TRACE(client, 3, "Valid SCGI header; buffering request body");
				client->state = Client::BUFFERING_REQUEST_BODY;
				client->requestBodyIsBuffered = true;
				client->beginScopeLog(&client->scopeLogs.bufferingRequestBody, "buffering request body");
				if (client->requestBodyLength == 0) {
					client->clientInput->stop();
					state_bufferingRequestBody_onClientEof(client);
					return 0;
				}
			} else {
				RH_TRACE(client, 3, "Valid SCGI header; not buffering request body; checking out session");
				client->clientInput->stop();
				checkoutSession(client);
			}
		}
		return consumed;
	}


	/******* State: BUFFERING_REQUEST_BODY *******/

	void state_bufferingRequestBody_verifyInvariants(const ClientPtr &client) const {
		assert(client->requestBodyIsBuffered);
		assert(!client->clientBodyBuffer->isStarted());
	}

	size_t state_bufferingRequestBody_onClientData(const ClientPtr &client, const char *data, size_t size) {
		state_bufferingRequestBody_verifyInvariants(client);
		assert(!client->clientBodyBuffer->isCommittingToDisk());

		if (client->requestBodyLength >= 0) {
			size = std::min<unsigned long long>(
				size,
				(unsigned long long) client->requestBodyLength - client->requestBodyAlreadyRead
			);
		}

		if (!client->clientBodyBuffer->write(data, size)) {
			// The pipe cannot write the data to disk quickly enough, so
			// suspend reading from the client until the pipe is done.
			client->backgroundOperations++; // TODO: figure out whether this is necessary
			client->clientInput->stop();
		}
		client->requestBodyAlreadyRead += size;

		RH_TRACE(client, 3, "Buffered " << size << " bytes of client body data; total=" <<
			client->requestBodyAlreadyRead << ", content-length=" << client->requestBodyLength);
		assert(client->requestBodyLength == -1 || client->requestBodyAlreadyRead <= (unsigned long long) client->requestBodyLength);

		if (client->requestBodyLength >= 0 && client->requestBodyAlreadyRead == (unsigned long long) client->requestBodyLength) {
			if (client->clientBodyBuffer->isCommittingToDisk()) {
				RH_TRACE(client, 3, "Done buffering request body, but clientBodyBuffer not yet done committing data to disk; waiting until it's done");
				client->checkoutSessionAfterCommit = true;
			} else {
				client->clientInput->stop();
				state_bufferingRequestBody_onClientEof(client);
			}
		}

		return size;
	}

	void state_bufferingRequestBody_onClientEof(const ClientPtr &client) {
		state_bufferingRequestBody_verifyInvariants(client);

		RH_TRACE(client, 3, "Done buffering request body; checking out session");
		client->clientBodyBuffer->end();
		client->endScopeLog(&client->scopeLogs.bufferingRequestBody);
		checkoutSession(client);
	}

	void state_bufferingRequestBody_onClientBodyBufferCommit(const ClientPtr &client) {
		// Now that the pipe has committed the data to disk
		// resume reading from the client socket.
		state_bufferingRequestBody_verifyInvariants(client);
		assert(!client->clientInput->isStarted());
		client->backgroundOperations--;
		if (client->checkoutSessionAfterCommit) {
			RH_TRACE(client, 3, "Done committing request body to disk");
			state_bufferingRequestBody_onClientEof(client);
		} else {
			client->clientInput->start();
		}
	}


	/******* State: CHECKING_OUT_SESSION *******/

	void state_checkingOutSession_verifyInvariants(const ClientPtr &client) {
		assert(!client->clientInput->isStarted());
		assert(!client->clientBodyBuffer->isStarted());
	}

	void checkoutSession(const ClientPtr &client) {
		if (benchmarkPoint != BP_BEFORE_CHECKOUT_SESSION) {
			RH_TRACE(client, 2, "Checking out session: appRoot=" << client->options.appRoot);
			client->state = Client::CHECKING_OUT_SESSION;
			client->beginScopeLog(&client->scopeLogs.getFromPool, "get from pool");
			pool->asyncGet(client->options, boost::bind(&RequestHandler::sessionCheckedOut,
				this, client, _1, _2));
			if (!client->sessionCheckedOut) {
				client->backgroundOperations++;
			}
		} else {
			writeSimpleResponse(client, "Benchmark point: before_checkout_session\n");
		}
	}

	void sessionCheckedOut(ClientPtr client, const SessionPtr &session, const ExceptionPtr &e) {
		if (!pthread_equal(pthread_self(), libev->getCurrentThread())) {
			libev->runLater(boost::bind(&RequestHandler::sessionCheckedOut_real, this,
				client, session, e));
		} else {
			sessionCheckedOut_real(client, session, e);
		}
	}

	void sessionCheckedOut_real(ClientPtr client, const SessionPtr &session, const ExceptionPtr &e) {
		RH_LOG_EVENT(client, "sessionCheckedOut");
		if (!client->connected()) {
			return;
		}

		state_checkingOutSession_verifyInvariants(client);
		client->backgroundOperations--;
		client->sessionCheckedOut = true;

		if (e != NULL) {
			client->endScopeLog(&client->scopeLogs.getFromPool, false);
			{
				boost::shared_ptr<RequestQueueFullException> e2 = dynamic_pointer_cast<RequestQueueFullException>(e);
				if (e2 != NULL) {
					writeRequestQueueFullExceptionErrorResponse(client);
					return;
				}
			}
			{
				boost::shared_ptr<SpawnException> e2 = dynamic_pointer_cast<SpawnException>(e);
				if (e2 != NULL) {
					writeSpawnExceptionErrorResponse(client, e2);
					return;
				}
			}
			writeOtherExceptionErrorResponse(client, e);
		} else {
			RH_DEBUG(client, "Session checked out: pid=" << session->getPid() <<
				", gupid=" << session->getGupid());
			client->session = session;
			initiateSession(client);
		}
	}

	void writeRequestQueueFullExceptionErrorResponse(const ClientPtr &client) {
		StaticString value = client->scgiParser.getHeader("PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE");
		int requestQueueOverflowStatusCode = 503;
		if (!value.empty()) {
			requestQueueOverflowStatusCode = atoi(value.data());
		}
		writeSimpleResponse(client,
			"<h1>This website is under heavy load</h1>"
			"<p>We're sorry, too many people are accessing this website at the same "
			"time. We're working on this problem. Please try again later.</p>",
			requestQueueOverflowStatusCode);
	}

	void writeSpawnExceptionErrorResponse(const ClientPtr &client, const boost::shared_ptr<SpawnException> &e) {
		RH_ERROR(client, "Cannot checkout session because a spawning error occurred. " <<
			"The identifier of the error is " << e->get("error_id") << ". Please see earlier logs for " <<
			"details about the error.");
		writeErrorResponse(client, e->getErrorPage(), e.get());
	}

	void writeOtherExceptionErrorResponse(const ClientPtr &client, const ExceptionPtr &e) {
		string typeName;
		#ifdef CXX_ABI_API_AVAILABLE
			int status;
			char *tmp = abi::__cxa_demangle(typeid(*e).name(), 0, 0, &status);
			if (tmp != NULL) {
				typeName = tmp;
				free(tmp);
			} else {
				typeName = typeid(*e).name();
			}
		#else
			typeName = typeid(*e).name();
		#endif

		RH_WARN(client, "Cannot checkout session (exception type " <<
			typeName << "): " << e->what());

		string response = "An internal error occurred while trying to spawn the application.\n";
		response.append("Exception type: ");
		response.append(typeName);
		response.append("\nError message: ");
		response.append(e->what());
		boost::shared_ptr<tracable_exception> e3 = dynamic_pointer_cast<tracable_exception>(e);
		if (e3 != NULL) {
			response.append("\nBacktrace:\n");
			response.append(e3->backtrace());
		}

		writeErrorResponse(client, response);
	}

	void initiateSession(const ClientPtr &client) {
		assert(client->state == Client::CHECKING_OUT_SESSION);
		client->sessionCheckoutTry++;
		try {
			client->session->initiate();
		} catch (const SystemException &e2) {
			if (client->sessionCheckoutTry < 10) {
				RH_DEBUG(client, "Error checking out session (" << e2.what() <<
					"); retrying (attempt " << client->sessionCheckoutTry << ")");
				client->sessionCheckedOut = false;
				pool->asyncGet(client->options,
					boost::bind(&RequestHandler::sessionCheckedOut,
						this, client, _1, _2));
				if (!client->sessionCheckedOut) {
					client->backgroundOperations++;
				}
			} else {
				string message = "could not initiate a session (";
				message.append(e2.what());
				message.append(")");
				disconnectWithError(client, message);
			}
			return;
		}

		if (client->useUnionStation()) {
			client->endScopeLog(&client->scopeLogs.getFromPool);
			client->logMessage("Application PID: " +
				toString(client->session->getPid()) +
				" (GUPID: " + client->session->getGupid() + ")");
			client->beginScopeLog(&client->scopeLogs.requestProxying, "request proxying");
		}

		RH_DEBUG(client, "Session initiated: fd=" << client->session->fd());
		setNonBlocking(client->session->fd());
		client->appInput->reset(libev.get(), client->session->fd());
		client->appInput->start();
		client->appOutputWatcher.set(libev->getLoop());
		client->appOutputWatcher.set(client->session->fd(), ev::WRITE);
		sendHeaderToApp(client);
	}


	/******* State: SENDING_HEADER_TO_APP *******/

	void state_sendingHeaderToApp_verifyInvariants(const ClientPtr &client) {
		assert(!client->clientInput->isStarted());
		assert(!client->clientBodyBuffer->isStarted());
	}

	void sendHeaderToApp(const ClientPtr &client) {
		assert(!client->clientInput->isStarted());
		assert(!client->clientBodyBuffer->isStarted());

		RH_TRACE(client, 2, "Sending headers to application");

		if (client->session == NULL) {
			disconnectWithError(client,
				"Application sent EOF before we were able to send headers to it");
		} else if (client->session->getProtocol() == "session") {
			char sizeField[sizeof(boost::uint32_t)];
			SmallVector<StaticString, 10> data;

			data.push_back(StaticString(sizeField, sizeof(boost::uint32_t)));
			data.push_back(client->scgiParser.getHeaderData());

			data.push_back(makeStaticStringWithNull("PASSENGER_CONNECT_PASSWORD"));
			data.push_back(makeStaticStringWithNull(client->session->getConnectPassword()));

			if (client->options.analytics) {
				data.push_back(makeStaticStringWithNull("PASSENGER_TXN_ID"));
				data.push_back(makeStaticStringWithNull(client->options.transaction->getTxnId()));
			}

			boost::uint32_t dataSize = 0;
			for (unsigned int i = 1; i < data.size(); i++) {
				dataSize += (boost::uint32_t) data[i].size();
			}
			Uint32Message::generate(sizeField, dataSize);

			ssize_t ret = gatheredWrite(client->session->fd(), &data[0],
				data.size(), client->appOutputBuffer);
			if (ret == -1 && errno != EAGAIN) {
				disconnectWithAppSocketWriteError(client, errno);
			} else if (!client->appOutputBuffer.empty()) {
				client->state = Client::SENDING_HEADER_TO_APP;
				client->appOutputWatcher.start();
			} else {
				sendBodyToApp(client);
			}
		} else {
			assert(client->session->getProtocol() == "http_session");
			const ScgiRequestParser &parser = client->scgiParser;
			ScgiRequestParser::const_iterator it, end = parser.end();
			string data;

			data.reserve(parser.getHeaderData().size() + 128);
			data.append(parser.getHeader("REQUEST_METHOD"));
			data.append(" ");
			data.append(parser.getHeader("REQUEST_URI"));
			data.append(" HTTP/1.1\r\n");

			for (it = parser.begin(); it != end; it++) {
				if (startsWith(it->first, "HTTP_") && it->first != "HTTP_CONNECTION") {
					string subheader = it->first.substr(sizeof("HTTP_") - 1);
					string::size_type i;
					for (i = 0; i < subheader.size(); i++) {
						if (subheader[i] == '_') {
							subheader[i] = '-';
						} else if (i > 0 && subheader[i - 1] != '-') {
							subheader[i] = tolower(subheader[i]);
						}
					}

					data.append(subheader);
					data.append(": ");
					data.append(it->second);
					data.append("\r\n");
				}
			}

			StaticString connection = parser.getHeader("HTTP_CONNECTION");
			if (regex_match(connection.data(), connection.data() + connection.size(),
				upgradeHeaderRegex))
			{
				data.append("Connection: ");
				data.append(connection.data(), connection.size());
				data.append("\r\n");
			} else {
				data.append("Connection: close\r\n");
			}

			StaticString header = parser.getHeader("CONTENT_LENGTH");
			if (!header.empty()) {
				data.append("Content-Length: ");
				data.append(header);
				data.append("\r\n");
			}

			header = parser.getHeader("CONTENT_TYPE");
			if (!header.empty()) {
				data.append("Content-Type: ");
				data.append(header);
				data.append("\r\n");
			}

			header = parser.getHeader("HTTPS");
			if (!header.empty()) {
				data.append("X-Forwarded-Proto: https\r\n");
			}

			header = parser.getHeader("REMOTE_ADDR");
			if (!header.empty()) {
				data.append("X-Forwarded-For: ");
				data.append(header);
				data.append("\r\n");
			}

			if (client->options.analytics) {
				data.append("Passenger-Txn-Id: ");
				data.append(client->options.transaction->getTxnId());
				data.append("\r\n");
			}

			P_TRACE(3, "Sending headers to application: " << data);
			data.append("\r\n");

			StaticString datas[] = { data };
			ssize_t ret = gatheredWrite(client->session->fd(), datas,
				1, client->appOutputBuffer);
			if (ret == -1 && errno != EAGAIN) {
				disconnectWithAppSocketWriteError(client, errno);
				// TODO: what about other errors?
			} else if (!client->appOutputBuffer.empty()) {
				client->state = Client::SENDING_HEADER_TO_APP;
				client->appOutputWatcher.start();
			} else {
				sendBodyToApp(client);
			}
		}
	}

	void state_sendingHeaderToApp_onAppOutputWritable(const ClientPtr &client) {
		state_sendingHeaderToApp_verifyInvariants(client);

		if (client->session == NULL) {
			disconnectWithError(client,
				"Application sent EOF before we were able to send headers to it");
		} else {
			ssize_t ret = gatheredWrite(client->session->fd(), NULL, 0, client->appOutputBuffer);
			if (ret == -1) {
				if (errno != EAGAIN && errno != EPIPE && errno != ECONNRESET) {
					disconnectWithAppSocketWriteError(client, errno);
				}
				// TODO: what about other errors?
			} else if (client->appOutputBuffer.empty()) {
				client->appOutputWatcher.stop();
				sendBodyToApp(client);
			}
		}
	}


	/******* State: FORWARDING_BODY_TO_APP *******/

	void state_forwardingBodyToApp_verifyInvariants(const ClientPtr &client) const {
		assert(client->state == Client::FORWARDING_BODY_TO_APP);
	}

	void sendBodyToApp(const ClientPtr &client) {
		assert(client->appOutputBuffer.empty());
		assert(!client->clientBodyBuffer->isStarted());
		assert(!client->clientInput->isStarted());
		assert(!client->appOutputWatcher.is_active());

		RH_TRACE(client, 2, "Begin sending body to application");

		client->state = Client::FORWARDING_BODY_TO_APP;
		if (client->requestBodyIsBuffered) {
			client->clientBodyBuffer->start();
		} else if (client->requestBodyLength == 0) {
			state_forwardingBodyToApp_onClientEof(client);
		} else {
			client->clientInput->start();
		}
	}


	size_t state_forwardingBodyToApp_onClientData(const ClientPtr &client,
		const char *data, size_t size)
	{
		state_forwardingBodyToApp_verifyInvariants(client);
		assert(!client->requestBodyIsBuffered);

		if (client->requestBodyLength >= 0) {
			size = std::min<unsigned long long>(
				size,
				(unsigned long long) client->requestBodyLength - client->requestBodyAlreadyRead
			);
		}

		RH_TRACE(client, 3, "Forwarding " << size << " bytes of client body data to application.");

		if (client->session == NULL) {
			RH_TRACE(client, 2, "Application had already sent EOF. Stop reading client input.");
			client->clientInput->stop();
			syscalls::shutdown(client->fd, SHUT_RD);
			return 0;
		}

		ssize_t ret = syscalls::write(client->session->fd(), data, size);
		int e = errno;
		if (ret == -1) {
			RH_TRACE(client, 3, "Could not write to application socket: " << strerror(e) << " (errno=" << e << ")");
			if (e == EAGAIN) {
				RH_TRACE(client, 3, "Waiting until the application socket is writable again.");
				client->clientInput->stop();
				client->appOutputWatcher.start();
			} else if (e == EPIPE || e == ECONNRESET) {
				// Client will be disconnected after response forwarding is done.
				client->clientInput->stop();
				syscalls::shutdown(client->fd, SHUT_RD);
			} else {
				disconnectWithAppSocketWriteError(client, e);
			}
			return 0;
		} else {
			client->requestBodyAlreadyRead += ret;

			RH_TRACE(client, 3, "Managed to forward " << ret << " bytes; total=" <<
				client->requestBodyAlreadyRead << ", content-length=" << client->requestBodyLength);
			assert(client->requestBodyLength == -1 || client->requestBodyAlreadyRead <= (unsigned long long) client->requestBodyLength);
			if (client->requestBodyLength >= 0 && client->requestBodyAlreadyRead == (unsigned long long) client->requestBodyLength) {
				client->clientInput->stop();
				state_forwardingBodyToApp_onClientEof(client);
			}

			return ret;
		}
	}

	void state_forwardingBodyToApp_onClientEof(const ClientPtr &client) {
		state_forwardingBodyToApp_verifyInvariants(client);
		assert(!client->requestBodyIsBuffered);

		RH_TRACE(client, 2, "End of (unbuffered) client body reached; done sending data to application");
		client->clientInput->stop();
		if (client->session != NULL && client->shouldHalfCloseWrite()) {
			syscalls::shutdown(client->session->fd(), SHUT_WR);
		}
	}

	void state_forwardingBodyToApp_onAppOutputWritable(const ClientPtr &client) {
		state_forwardingBodyToApp_verifyInvariants(client);

		RH_TRACE(client, 3, "Application socket became writable again.");
		client->appOutputWatcher.stop();
		if (client->requestBodyIsBuffered) {
			assert(!client->clientBodyBuffer->isStarted());
			client->clientBodyBuffer->start();
		} else {
			assert(!client->clientInput->isStarted());
			client->clientInput->start();
		}
	}


	void state_forwardingBodyToApp_onClientBodyBufferData(const ClientPtr &client,
		const char *data, size_t size, const FileBackedPipe::ConsumeCallback &consumed)
	{
		state_forwardingBodyToApp_verifyInvariants(client);
		assert(client->requestBodyIsBuffered);

		RH_TRACE(client, 3, "Forwarding " << size << " bytes of buffered client body data to application.");

		if (client->session == NULL) {
			RH_TRACE(client, 2, "Application had already sent EOF. Stop reading client input.");
			syscalls::shutdown(client->fd, SHUT_RD);
			consumed(0, true);
			return;
		}

		ssize_t ret = syscalls::write(client->session->fd(), data, size);
		if (ret == -1) {
			int e = errno;
			RH_TRACE(client, 3, "Could not write to application socket: " << strerror(e) << " (errno=" << e << ")");
			if (e == EAGAIN) {
				RH_TRACE(client, 3, "Waiting until the application socket is writable again.");
				client->appOutputWatcher.start();
				consumed(0, true);
			} else if (e == EPIPE || e == ECONNRESET) {
				// Client will be disconnected after response forwarding is done.
				syscalls::shutdown(client->fd, SHUT_RD);
				consumed(0, true);
			} else {
				disconnectWithAppSocketWriteError(client, e);
			}
		} else {
			RH_TRACE(client, 3, "Managed to forward " << ret << " bytes.");
			consumed(ret, false);
		}
	}

	void state_forwardingBodyToApp_onClientBodyBufferEnd(const ClientPtr &client) {
		state_forwardingBodyToApp_verifyInvariants(client);
		assert(client->requestBodyIsBuffered);

		RH_TRACE(client, 2, "End of (buffered) client body reached; done sending data to application");
		if (client->session != NULL && client->shouldHalfCloseWrite()) {
			syscalls::shutdown(client->session->fd(), SHUT_WR);
		}
	}


public:
	// For unit testing purposes.
	unsigned int connectPasswordTimeout; // milliseconds

	BenchmarkPoint benchmarkPoint;

	RequestHandler(const SafeLibevPtr &_libev,
		const FileDescriptor &_requestSocket,
		const PoolPtr &_pool,
		const AgentOptions &_options)
		: libev(_libev),
		  requestSocket(_requestSocket),
		  pool(_pool),
		  options(_options),
		  resourceLocator(_options.passengerRoot),
		  upgradeHeaderRegex("(keep-alive, *)?upgrade(, *keep-alive)?",
		      boost::regex::perl | boost::regex::icase),
		  benchmarkPoint(getDefaultBenchmarkPoint())
	{
		accept4Available = true;
		connectPasswordTimeout = 15000;
		unionStationCore = pool->getUnionStationCore();

		requestSocketWatcher.set(_requestSocket, ev::READ);
		requestSocketWatcher.set(_libev->getLoop());
		requestSocketWatcher.set<RequestHandler, &RequestHandler::onAcceptable>(this);
		requestSocketWatcher.start();

		resumeSocketWatcherTimer.set<RequestHandler, &RequestHandler::onResumeSocketWatcher>(this);
		resumeSocketWatcherTimer.set(_libev->getLoop());
		resumeSocketWatcherTimer.set(3, 3);
	}

	template<typename Stream>
	void inspect(Stream &stream) const {
		stream << clients.size() << " clients:\n";
		HashMap<int, ClientPtr>::const_iterator it;
		for (it = clients.begin(); it != clients.end(); it++) {
			const ClientPtr &client = it->second;
			stream << "  Client " << client->fd << ":\n";
			client->inspect(stream);
		}
	}

	void resetInactivityTime() {
		libev->run(boost::bind(&RequestHandler::doResetInactivityTime, this));
	}

	unsigned long long inactivityTime() const {
		unsigned long long result;
		libev->run(boost::bind(&RequestHandler::getInactivityTime, this, &result));
		return result;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_REQUEST_HANDLER_H_ */
