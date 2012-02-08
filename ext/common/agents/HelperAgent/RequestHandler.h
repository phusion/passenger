/*
   STAGES
   
     Accept connect password
              |
             \|/
          Read header
              |
             \|/
       +------+-----+
       |            |
       |            |
      \|/           |
     Buffer         |
     request        |
     body           |
       |            |
       |            |
      \|/           |
    Checkout <------+
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
   and forward
   response to
     client



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

 */

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/make_shared.hpp>
#include <ev++.h>

#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <cassert>

#include <Logging.h>
#include <EventedBufferedInput.h>
#include <MessageReadersWriters.h>
#include <ApplicationPool2/Pool.h>
#include <Utils/StrIntUtils.h>
#include <Utils/IOUtils.h>
#include <agents/HelperAgent/AgentOptions.h>
#include <agents/HelperAgent/FileBackedPipe.h>
#include <agents/HelperAgent/ScgiRequestParser.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;
using namespace ApplicationPool2;

class RequestHandler;

#define RH_WARN(client, x) P_WARN("[Client " << client->name() << "] " << x)
#define RH_DEBUG(client, x) P_DEBUG("[Client " << client->name() << "] " << x)
#define RH_TRACE(client, level, x) P_TRACE(level, "[Client " << client->name() << "] " << x)


class Client: public enable_shared_from_this<Client> {
private:
	struct ev_loop *getLoop() const;
	const SafeLibevPtr &getSafeLibev() const;

	static size_t onClientInputData(weak_ptr<Client> wself, const StaticString &data);
	static void onClientInputError(weak_ptr<Client> wself, const char *message, int errnoCode);

	static void onClientBodyBufferData(const FileBackedPipePtr &source,
		const char *data, size_t size,
		const FileBackedPipe::ConsumeCallback &callback);
	static void onClientBodyBufferEnd(const FileBackedPipePtr &source);
	static void onClientBodyBufferError(const FileBackedPipePtr &source, int errorCode);
	static void onClientBodyBufferDrained(const FileBackedPipePtr &source);
	
	static void onClientOutputPipeData(const FileBackedPipePtr &source,
		const char *data, size_t size,
		const FileBackedPipe::ConsumeCallback &callback);
	static void onClientOutputPipeEnd(const FileBackedPipePtr &source);
	static void onClientOutputPipeError(const FileBackedPipePtr &source, int errorCode);
	static void onClientOutputPipeDrained(const FileBackedPipePtr &source);
	
	void onClientOutputWritable(ev::io &io, int revents);
	
	static size_t onAppInputData(weak_ptr<Client> wself, const StaticString &data);
	static void onAppInputError(weak_ptr<Client> wself, const char *message, int errnoCode);
	
	void onAppOutputWritable(ev::io &io, int revents);


	void resetPrimitiveFields() {
		requestHandler = NULL;
		state = DISCONNECTED;
		backgroundOperations = 0;
		requestBodyIsBuffered = false;
		freeBufferedConnectPassword();
		sessionCheckedOut = false;
		sessionCheckoutTry = 0;
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

	Options options;
	ScgiRequestParser scgiParser;
	SessionPtr session;
	bool requestBodyIsBuffered;
	bool sessionCheckedOut;
	unsigned int sessionCheckoutTry;


	Client() {
		fdnum = -1;

		clientInput = make_shared< EventedBufferedInput<> >();
		
		clientBodyBuffer = make_shared<FileBackedPipe>("/tmp");
		clientBodyBuffer->userData  = this;
		clientBodyBuffer->onData    = onClientBodyBufferData;
		clientBodyBuffer->onEnd     = onClientBodyBufferEnd;
		clientBodyBuffer->onError   = onClientBodyBufferError;
		clientBodyBuffer->onBufferDrained = onClientBodyBufferDrained;

		clientOutputPipe = make_shared<FileBackedPipe>("/tmp");
		clientOutputPipe->userData  = this;
		clientOutputPipe->onData    = onClientOutputPipeData;
		clientOutputPipe->onEnd     = onClientOutputPipeEnd;
		clientOutputPipe->onError   = onClientOutputPipeError;
		clientOutputPipe->onBufferDrained = onClientOutputPipeDrained;

		clientOutputWatcher.set<Client, &Client::onClientOutputWritable>(this);

		
		appInput = make_shared< EventedBufferedInput<> >();
		
		appOutputWatcher.set<Client, &Client::onAppOutputWritable>(this);
		

		bufferedConnectPassword.data = NULL;
		bufferedConnectPassword.alreadyRead = 0;
		resetPrimitiveFields();
	}

	~Client() {
		clientBodyBuffer->userData = NULL;
		clientOutputPipe->userData = NULL;
		freeBufferedConnectPassword();
	}

	void initialize() {
		clientInput->onData = boost::bind(onClientInputData,
			weak_ptr<Client>(shared_from_this()), _1);
		clientInput->onError = boost::bind(onClientInputError,
			weak_ptr<Client>(shared_from_this()), _1, _2);
		
		appInput->onData = boost::bind(onAppInputData,
			weak_ptr<Client>(shared_from_this()), _1);
		appInput->onError = boost::bind(onAppInputError,
			weak_ptr<Client>(shared_from_this()), _1, _2);
	}

	void associate(RequestHandler *handler, const FileDescriptor &_fd) {
		assert(requestHandler == NULL);
		requestHandler = handler;
		fd = _fd;
		fdnum = _fd;
		state = BEGIN_READING_CONNECT_PASSWORD;

		clientInput->reset(getSafeLibev().get(), _fd);
		clientInput->start();
		clientBodyBuffer->reset(getSafeLibev());
		clientOutputPipe->reset(getSafeLibev());
		clientOutputPipe->start();
		clientOutputWatcher.set(getLoop());

		appOutputWatcher.set(getLoop());
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
		
		scgiParser.reset();
		session.reset();
	}

	void discard() {
		assert(requestHandler != NULL);
		resetPrimitiveFields();
		fd = FileDescriptor();

		clientInput->stop();
		clientBodyBuffer->stop();
		clientOutputPipe->stop();
		clientOutputWatcher.stop();

		appInput->stop();
		appOutputWatcher.stop();
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

	void verifyInvariants() const {
		assert((requestHandler == NULL) == (fd == -1));
		assert((requestHandler == NULL) == (state == DISCONNECTED));
	}

	template<typename Stream>
	void inspect(Stream &stream) const {
		const char *indent = "    ";
		stream
			<< indent << "state = " << getStateName() << "\n"
			<< indent << "requestBodyIsBuffered = " << requestBodyIsBuffered << "\n"
			<< indent << "clientInput.started = " << clientInput->started() << "\n";
	}
};

typedef shared_ptr<Client> ClientPtr;


class RequestHandler {
private:
	friend class Client;

	const SafeLibevPtr libev;
	FileDescriptor requestSocket;
	PoolPtr pool;
	const AgentOptions &options;
	ev::io requestSocketWatcher;
	HashMap<int, ClientPtr> clients;
	bool accept4Available;


	void disconnect(const ClientPtr &client) {
		// Prevent Client object from being destroyed until we're done.
		ClientPtr reference = client;

		clients.erase(client->fd);
		client->discard();
		client->verifyInvariants();
		RH_DEBUG(client, "Disconnected; new client count = " << clients.size());
	}

	void disconnectWithError(const ClientPtr &client, const StaticString &message) {
		RH_WARN(client, "Disconnecting with error: " << message);
		disconnect(client);
	}

	void disconnectWithClientSocketWriteError(const ClientPtr &client, int e) {
		stringstream message;
		message << "client socket write error: ";
		message << strerror(e);
		message << " (errno " << e << ")";
		disconnectWithError(client, message.str());
	}

	void disconnectWithAppSocketWriteError(const ClientPtr &client, int e) {
		stringstream message;
		message << "app socket write error: ";
		message << strerror(e);
		message << " (errno " << e << ")";
		disconnectWithError(client, message.str());
	}

	void disconnectWithWarning(const ClientPtr &client, const StaticString &message) {
		P_DEBUG("Disconnected client " << client->name() << " with warning: " << message);
		disconnect(client);
	}

	void writeSimpleResponse(const ClientPtr &client, const StaticString &data) {
		assert(client->state < Client::FORWARDING_BODY_TO_APP);
		client->state = Client::WRITING_SIMPLE_RESPONSE;

		stringstream str;
		str << "Status: 500\r\n";
		str << "Content-Length: " << data.size() << "\r\n";
		str << "Content-Type: text/plain\r\n";
		str << "\r\n";

		const string &header = str.str();
		client->clientOutputPipe->write(header.data(), header.size());
		client->clientOutputPipe->write(data.data(), data.size());
		client->clientOutputPipe->end();
	}


	/*****************************************************
	 * COMPONENT: appInput -> clientOutputPipe plumbing
	 *
	 * The following code handles forwarding data from
	 * appInput to clientOutputPipe.
	 *****************************************************/
	
	size_t onAppInputData(const ClientPtr &client, const StaticString &data) {
		if (!client->connected()) {
			return 0;
		}

		if (!data.empty()) {
			RH_TRACE(client, 3, "Application sent data: \"" << cEscapeString(data) << "\"");
			if (!client->clientOutputPipe->write(data.data(), data.size())) {
				client->backgroundOperations++;
				client->appInput->stop();
			}
			return data.size();
		} else {
			onAppInputEof(client);
			return 0;
		}
	}

	void onAppInputEof(const ClientPtr &client) {
		if (!client->connected()) {
			return;
		}

		RH_TRACE(client, 3, "Application sent EOF");
		client->clientOutputPipe->end();
		client->appInput->stop();
	}

	void onAppInputError(const ClientPtr &client, const char *message, int errorCode) {
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
			message << " (errno " << errorCode << ")";
			disconnectWithError(client, message.str());
		}
	}

	void onClientOutputPipeDrained(const ClientPtr &client) {
		if (!client->connected()) {
			return;
		}

		client->backgroundOperations--;
		client->appInput->start();
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
		if (!client->connected()) {
			return;
		}

		ssize_t ret = syscalls::write(client->fd, data, size);
		if (ret == -1) {
			if (errno == EAGAIN) {
				// Wait until the client socket is writable before resuming writing data.
				client->clientOutputWatcher.start();
			} else if (errno == EPIPE) {
				// If the client closed the connection then disconnect quietly.
				disconnect(client);
			} else {
				disconnectWithClientSocketWriteError(client, errno);
			}
			consumed(0, true);
		} else {
			consumed(ret, false);
		}
	}

	void onClientOutputPipeEnd(const ClientPtr &client) {
		if (!client->connected()) {
			return;
		}

		RH_TRACE(client, 2, "Client output pipe ended; disconnecting client");
		disconnect(client);
	}

	void onClientOutputPipeError(const ClientPtr &client, int errorCode) {
		if (!client->connected()) {
			return;
		}

		stringstream message;
		message << "client output pipe error: ";
		message << strerror(errorCode);
		message << " (errno " << errorCode << ")";
		disconnectWithError(client, message.str());
	}

	void onClientOutputWritable(const ClientPtr &client) {
		if (!client->connected()) {
			return;
		}

		// Continue forwarding output data to the client.
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
			FileDescriptor fd = callAccept4(requestSocket,
				(struct sockaddr *) &u, &addrlen, O_NONBLOCK);
			if (fd == -1 && errno == ENOSYS) {
				accept4Available = false;
				return acceptNonBlockingSocket(sock);
			} else {
				return fd;
			}
		} else {
			FileDescriptor fd = syscalls::accept(requestSocket,
				(struct sockaddr *) &u, &addrlen);
			if (fd != -1) {
				int e = errno;
				setNonBlocking(fd);
				errno = e;
			}
			return fd;
		}
	}


	void onAcceptable(ev::io &io, int revents) {
		bool endReached = false;
		unsigned int count = 0;

		while (!endReached && count < 10) {
			FileDescriptor fd = acceptNonBlockingSocket(requestSocket);
			if (fd == -1) {
				if (errno == EAGAIN) {
					endReached = true;
				} else {
					int e = errno;
					throw SystemException("Cannot accept client", e);
				}
			} else {
				ClientPtr client = make_shared<Client>();
				client->initialize();
				client->associate(this, fd);
				clients.insert(make_pair<int, ClientPtr>(fd, client));
				count++;
				RH_DEBUG(client, "New client accepted; new client count = " << clients.size());
			}
		}
	}


	size_t onClientInputData(const ClientPtr &client, const StaticString &data) {
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

		while (consumed < size && client->connected() && client->clientInput->started()) {
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
		RH_TRACE(client, 3, "Client sent EOF");
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
		if (!client->connected()) {
			return;
		}

		if (errnoCode == ECONNRESET) {
			// We might as well treat ECONNRESET like an EOF.
			// http://stackoverflow.com/questions/2974021/what-does-econnreset-mean-in-the-context-of-an-af-local-socket
			onClientEof(client);
		} else {
			stringstream message;
			message << "client socket read error: ";
			message << strerror(errnoCode);
			message << " (errno " << errnoCode << ")";
			disconnectWithError(client, message.str());
		}
	}


	void onClientBodyBufferData(const ClientPtr &client, const char *data, size_t size, const FileBackedPipe::ConsumeCallback &consumed) {
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
		if (!client->connected()) {
			return;
		}

		stringstream message;
		message << "client body buffer error: ";
		message << strerror(errorCode);
		message << " (errno " << errorCode << ")";
		disconnectWithError(client, message.str());
	}

	void onClientBodyBufferEnd(const ClientPtr &client) {
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

	void onClientBodyBufferDrained(const ClientPtr &client) {
		if (!client->connected()) {
			return;
		}

		switch (client->state) {
		case Client::BUFFERING_REQUEST_BODY:
			state_bufferingRequestBody_onClientBodyBufferDrained(client);
			break;
		default:
			abort();
		}
	}

	void onAppOutputWritable(const ClientPtr &client) {
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


	/*****************************************************
	 * COMPONENT: client -> application plumbing
	 *
	 * The following code implements forwarding data from
	 * the client to the application. Code is seperated
	 * by client state.
	 *****************************************************/


	/******* State: BEGIN_READING_CONNECT_PASSWORD *******/

	void checkConnectPassword(const ClientPtr &client, const char *data, unsigned int len) {
		RH_TRACE(client, 2, "Given connect password: \"" << cEscapeString(StaticString(data, len)) << "\"");
		if (StaticString(data, len) == options.requestSocketPassword) {
			RH_TRACE(client, 2, "Connect password is correct; reading header");
			client->state = Client::READING_HEADER;
			client->freeBufferedConnectPassword();
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
		size_t consumed = std::min<size_t>(size, options.requestSocketPassword.size() -
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

	size_t state_readingHeader_onClientData(const ClientPtr &client, const char *data, size_t size) {
		size_t consumed = client->scgiParser.feed(data, size);
		if (!client->scgiParser.acceptingInput()) {
			if (client->scgiParser.getState() == ScgiRequestParser::ERROR) {
				disconnectWithError(client, "invalid SCGI header");
			} else if (client->scgiParser.getHeader("PASSENGER_BUFFERING") == "true") {
				RH_TRACE(client, 3, "Valid SCGI header; buffering request body");
				client->state = Client::BUFFERING_REQUEST_BODY;
				client->requestBodyIsBuffered = true;
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

		if (!client->clientBodyBuffer->write(data, size)) {
			// The pipe cannot write the data to disk quickly enough, so
			// suspend reading from the client until the pipe is done.
			client->backgroundOperations++; // TODO: figure out whether this is necessary
			client->clientInput->stop();
		}
		return size;
	}

	void state_bufferingRequestBody_onClientEof(const ClientPtr &client) {
		state_bufferingRequestBody_verifyInvariants(client);

		RH_TRACE(client, 3, "Done buffering request body; checking out session");
		client->clientInput->stop();
		client->clientBodyBuffer->end();
		checkoutSession(client);
	}

	void state_bufferingRequestBody_onClientBodyBufferDrained(const ClientPtr &client) {
		// Now that the pipe has committed the data to disk
		// resume reading from the client socket.
		state_bufferingRequestBody_verifyInvariants(client);
		assert(!client->clientInput->started());
		client->backgroundOperations--;
		client->clientInput->start();
	}


	/******* State: CHECKING_OUT_SESSION *******/

	void state_checkingOutSession_verifyInvariants(const ClientPtr &client) {
		assert(!client->clientInput->started());
		assert(!client->clientBodyBuffer->isStarted());
	}

	void checkoutSession(const ClientPtr &client) {
		const ScgiRequestParser &parser = client->scgiParser;
		Options &options = client->options;

		options.appRoot = parser.getHeader("PASSENGER_APP_ROOT");
		options.appType = parser.getHeader("PASSENGER_APP_TYPE");
		// TODO

		RH_TRACE(client, 2, "Checking out session: appRoot=" << options.appRoot);
		client->state = Client::CHECKING_OUT_SESSION;
		pool->asyncGet(client->options, boost::bind(&RequestHandler::sessionCheckedOut,
			this, client, _1, _2));
		if (!client->sessionCheckedOut) {
			client->backgroundOperations++;
		}
	}

	void sessionCheckedOut(ClientPtr client, const SessionPtr &session, const ExceptionPtr &e) {
		if (!pthread_equal(pthread_self(), libev->getCurrentThread())) {
			libev->runAsync(boost::bind(&RequestHandler::sessionCheckedOut_real, this,
				client, session, e));
		} else {
			sessionCheckedOut_real(client, session, e);
		}
	}

	void sessionCheckedOut_real(ClientPtr client, const SessionPtr &session, const ExceptionPtr &e) {
		if (!client->connected()) {
			return;
		}

		state_checkingOutSession_verifyInvariants(client);
		client->backgroundOperations--;
		client->sessionCheckedOut = true;

		if (e != NULL) {
			try {
				shared_ptr<SpawnException> e2 = dynamic_pointer_cast<SpawnException>(e);
				RH_WARN(client, "Cannot checkout session. " << e2->what() <<
					"\n" << e2->getErrorPage());
				writeSimpleResponse(client, e2->getErrorPage());
			} catch (const bad_cast &) {
				RH_WARN(client, "Cannot checkout session; error messages can be found above");
				writeSimpleResponse(client, e->what());
			}
		} else {
			RH_TRACE(client, 3, "Session checked out: pid=" << session->getPid() <<
				", gupid=" << session->getGupid());
			client->session = session;
			initiateSession(client);
		}
	}

	void initiateSession(const ClientPtr &client) {
		assert(client->state == Client::CHECKING_OUT_SESSION);
		client->sessionCheckoutTry++;
		try {
			client->session->initiate();
		} catch (const SystemException &e2) {
			if (client->sessionCheckoutTry < 10) {
				RH_TRACE(client, 2, "Error checking out session (" << e2.what() <<
					"); retrying (attempt " << client->sessionCheckoutTry << ")");
				client->sessionCheckedOut = false;
				pool->asyncGet(client->options,
					boost::bind(&RequestHandler::sessionCheckedOut,
						this, client, _1, _2));
				if (!client->sessionCheckedOut) {
					client->backgroundOperations++;
				}
			} else {
				disconnectWithError(client, "could not initiate a session");
			}
			return;
		}
		
		client->appInput->reset(libev.get(), client->session->fd());
		client->appInput->start();
		client->appOutputWatcher.set(libev->getLoop());
		client->appOutputWatcher.set(client->session->fd());
		sendHeaderToApp(client);
	}


	/******* State: SENDING_HEADER_TO_APP *******/

	static StaticString makeStaticStringWithNull(const char *data) {
		return StaticString(data, strlen(data) + 1);
	}

	static StaticString makeStaticStringWithNull(const string &data) {
		return StaticString(data.c_str(), data.size() + 1);
	}

	void state_sendingHeaderToApp_verifyInvariants(const ClientPtr &client) {
		assert(!client->clientInput->started());
		assert(!client->clientBodyBuffer->isStarted());
	}

	void sendHeaderToApp(const ClientPtr &client) {
		assert(!client->clientInput->started());
		assert(!client->clientBodyBuffer->isStarted());

		RH_TRACE(client, 2, "Sending headers to application");

		char sizeField[sizeof(uint32_t)];
		StaticString data[] = {
			StaticString(sizeField, sizeof(uint32_t)),
			client->scgiParser.getHeaderData(),

			makeStaticStringWithNull("PASSENGER_CONNECT_PASSWORD"),
			makeStaticStringWithNull(client->session->getConnectPassword())
		};

		uint32_t dataSize = 0;
		for (unsigned int i = 1; i < sizeof(data) / sizeof(StaticString); i++) {
			dataSize += (uint32_t) data[i].size();
		}
		Uint32Message::generate(sizeField, dataSize);

		ssize_t ret = gatheredWrite(client->session->fd(), data,
			sizeof(data) / sizeof(StaticString), client->appOutputBuffer);
		if (ret == -1 && errno != EAGAIN) {
			disconnectWithAppSocketWriteError(client, errno);
		} else if (!client->appOutputBuffer.empty()) {
			client->state = Client::SENDING_HEADER_TO_APP;
			client->appOutputWatcher.start();
		} else {
			sendBodyToApp(client);
		}
	}

	void state_sendingHeaderToApp_onAppOutputWritable(const ClientPtr &client) {
		state_sendingHeaderToApp_verifyInvariants(client);

		ssize_t ret = gatheredWrite(client->session->fd(), NULL, 0, client->appOutputBuffer);
		if (ret == -1) {
			if (errno != EAGAIN && errno != EPIPE) {
				disconnectWithAppSocketWriteError(client, errno);
			}
		} else if (client->appOutputBuffer.empty()) {
			client->appOutputWatcher.stop();
			sendBodyToApp(client);
		}
	}


	/******* State: FORWARDING_BODY_TO_APP *******/

	void state_forwardingBodyToApp_verifyInvariants(const ClientPtr &client) {
		assert(client->state == Client::FORWARDING_BODY_TO_APP);
	}

	void sendBodyToApp(const ClientPtr &client) {
		assert(client->appOutputBuffer.empty());
		assert(!client->clientBodyBuffer->isStarted());
		assert(!client->clientInput->started());
		assert(!client->appOutputWatcher.is_active());

		RH_TRACE(client, 2, "Sending body to application");

		client->state = Client::FORWARDING_BODY_TO_APP;
		if (client->requestBodyIsBuffered) {
			client->clientBodyBuffer->start();
		} else {
			client->clientInput->start();
		}
	}


	size_t state_forwardingBodyToApp_onClientData(const ClientPtr &client,
		const char *data, size_t size)
	{
		state_forwardingBodyToApp_verifyInvariants(client);
		assert(!client->requestBodyIsBuffered);

		ssize_t ret = syscalls::write(client->session->fd(), data, size);
		if (ret == -1) {
			if (errno == EAGAIN) {
				// App is not ready yet to receive this data. Try later
				// when the app socket is writable.
				client->clientInput->stop();
				client->appOutputWatcher.start();
			} else if (errno == EPIPE) {
				// Client will be disconnected after response forwarding is done.
				client->clientInput->stop();
				syscalls::shutdown(client->fd, SHUT_RD);
			} else {
				disconnectWithAppSocketWriteError(client, errno);
			}
			return 0;
		} else {
			return ret;
		}
	}

	void state_forwardingBodyToApp_onClientEof(const ClientPtr &client) {
		state_forwardingBodyToApp_verifyInvariants(client);
		assert(!client->requestBodyIsBuffered);

		RH_TRACE(client, 2, "End of (unbuffered) client body reached; done sending data to application");
		client->clientInput->stop();
		syscalls::shutdown(client->session->fd(), SHUT_WR);
	}

	void state_forwardingBodyToApp_onAppOutputWritable(const ClientPtr &client) {
		state_forwardingBodyToApp_verifyInvariants(client);
		assert(!client->requestBodyIsBuffered);

		client->clientInput->start();
		client->clientOutputWatcher.stop();
	}


	void state_forwardingBodyToApp_onClientBodyBufferData(const ClientPtr &client,
		const char *data, size_t size, const FileBackedPipe::ConsumeCallback &consumed)
	{
		state_forwardingBodyToApp_verifyInvariants(client);
		assert(client->requestBodyIsBuffered);

		ssize_t ret = syscalls::write(client->session->fd(), data, size);
		if (ret == -1) {
			if (errno == EAGAIN) {
				// App is not ready yet to receive this data. Try later
				// when the app socket is writable.
				client->clientBodyBuffer->stop();
				client->appOutputWatcher.start();
			} else if (errno == EPIPE) {
				// Client will be disconnected after response forwarding is done.
				syscalls::shutdown(client->fd, SHUT_RD);
			} else {
				disconnectWithAppSocketWriteError(client, errno);
			}
			consumed(0, true);
		} else {
			consumed(ret, false);
		}
	}

	void state_forwardingBodyToApp_onClientBodyBufferEnd(const ClientPtr &client) {
		state_forwardingBodyToApp_verifyInvariants(client);
		assert(client->requestBodyIsBuffered);

		RH_TRACE(client, 2, "End of (buffered) client body reached; done sending data to application");
		syscalls::shutdown(client->session->fd(), SHUT_WR);
	}


public:
	RequestHandler(const SafeLibevPtr &_libev, const FileDescriptor &_requestSocket,
		const PoolPtr &_pool, const AgentOptions &_options)
		: libev(_libev),
		  requestSocket(_requestSocket),
		  pool(_pool),
		  options(_options)
	{
		accept4Available = true;
		requestSocketWatcher.set(_requestSocket, ev::READ);
		requestSocketWatcher.set(_libev->getLoop());
		requestSocketWatcher.set<RequestHandler, &RequestHandler::onAcceptable>(this);
		requestSocketWatcher.start();
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
};


} // namespace Passenger
