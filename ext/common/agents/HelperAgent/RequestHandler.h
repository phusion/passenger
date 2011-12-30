class Client: public enable_shared_from_this<Client> {
private:
	static size_t onClientSocketData(weak_ptr<Client> wself, const StaticString &data) {
		shared_ptr<Client> self = wself.lock();
		if (self != NULL) {
			return self->requestHandler->onClientData(self, data);
		}
	}

	static size_t onClientSocketError(weak_ptr<Client> wself, const char *message, int errnoCode) {
		shared_ptr<Client> self = wself.lock();
		if (self != NULL) {
			return self->requestHandler->onClientError(self, message, errnoCode);
		}
	}

	static size_t onAppSocketData(weak_ptr<Client> wself, const StaticString &data) {
		shared_ptr<Client> self = wself.lock();
		if (self != NULL) {
			return self->requestHandler->onAppSocketData(self, data);
		}
	}

	static size_t onAppSocketError(weak_ptr<Client> wself, const char *message, int errnoCode) {
		shared_ptr<Client> self = wself.lock();
		if (self != NULL) {
			return self->requestHandler->onAppSocketError(self, message, errnoCode);
		}
	}

	static FileBackedPipe::ConsumeResult onRequestBodyPipeData(
		weak_ptr<Client> wself, const char *data, size_t size, bool direct)
	{
		shared_ptr<Client> self = wself.lock();
		if (self != NULL) {
			return self->requestHandler->onRequestBodyPipeData(self, data, size, direct);
		}
	}

	static FileBackedPipe::ConsumeResult onRequestBodyPipeEof(
		weak_ptr<Client> wself, bool direct)
	{
		shared_ptr<Client> self = wself.lock();
		if (self != NULL) {
			return self->requestHandler->onRequestBodyPipeEof(self, direct);
		}
	}

	static FileBackedPipe::ConsumeResult onRequestBodyPipeCommit(weak_ptr<Client> wself,
		bool direct)
	{
		shared_ptr<Client> self = wself.lock();
		if (self != NULL) {
			return self->requestHandler->onRequestBodyPipeCommit(self, direct);
		}
	}

	static FileBackedPipe::ConsumeResult onResponsePipeData(
		weak_ptr<Client> wself, const char *data, size_t size, bool direct)
	{
		shared_ptr<Client> self = wself.lock();
		if (self != NULL) {
			return self->requestHandler->onResponsePipeData(self, data, size, direct);
		}
	}

	static FileBackedPipe::ConsumeResult onResponsePipeEof(
		weak_ptr<Client> wself, bool direct)
	{
		shared_ptr<Client> self = wself.lock();
		if (self != NULL) {
			return self->requestHandler->onResponsePipeEof(self, direct);
		}
	}

	static FileBackedPipe::ConsumeResult onResponsePipeCommit(weak_ptr<Client> wself,
		bool direct)
	{
		shared_ptr<Client> self = wself.lock();
		if (self != NULL) {
			return self->requestHandler->onResponsePipeCommit(self, direct);
		}
	}

	void resetPrimitiveFields() {
		state = DISCONNECTED;
		requestHandler = NULL;
		backgroundOperating = false;
		freeBufferedConnectPassword();
		sessionCheckedOut = false;
		sessionCheckoutTry = 0;
	}

public:
	enum {
		BEGIN_READING_CONNECT_PASSWORD,
		STILL_READING_CONNECT_PASSWORD,
		READING_HEADER,
		BUFFERING_REQUEST_BODY,
		CHECKING_OUT_SESSION,
		SENDING_HEADER_TO_APP,
		FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE,

		// Special states
		WRITING_CLIENT_BUFFER,
		DISCONNECTED
	};

	RequestHandler *requestHandler;
	FileDescriptor fd;
	EventedBufferedInputPtr clientInput;
	EventedBufferedInputPtr appInput;
	ev::io clientWriteWatcher;
	ev::io appWriteWatcher;

	/* Whether a background operation is currently in progress, e.g.
	 * an asyncGet() or bodyBuffer.add(). If the client is disconnected
	 * while this flag is true, then the Client object is not returned
	 * into Client object pool in order to give the completion callbacks
	 * a chance to cancel properly.
	 *
	 * One should also check clientInput->hasBackgroundOperations.
	 */
	bool backgroundOperating;

	struct {
		char *data;
		unsigned int alreadyRead;
	} bufferedConnectPassword;

	FileBackedPipePtr requestBodyPipe;
	FileBackedPipePtr responsePipe;
	string writeBuffer;
	string appWriteBuffer;

	Options options;
	ScgiRequestparser scgiParser;
	bool sessionCheckedOut;
	SessionPtr session;
	unsigned int sessionCheckoutTry;

	Client() {
		bufferedConnectPassword.data = NULL;
		bufferedConnectPassword.alreadyRead = 0;
		clientInput = make_shared<EventedBufferedInput>();
		clientInput->onData = boost::bind(onClientSocketData,
			weak_ptr<Client>(shared_from_this()), _1);
		clientInput->onError = boost::bind(onClientSocketError,
			weak_ptr<Client>(shared_from_this()), _1);
		
		appInput = make_shared<EventedBufferedInput>();
		appInput->onData = boost::bind(onAppSocketData,
			weak_ptr<Client>(shared_from_this()), _1);
		appInput->onError = boost::bind(onAppSocketError,
			weak_ptr<Client>(shared_from_this()), _1);
		
		requestBodyPipe = make_shared<FileBackedPipe>(threadPool);
		requestBodyPipe->onData = boost::bind(onRequestBodyPipeData,
			weak_ptr<Client>(shared_from_this()), _1, _2, _3);
		requestBodyPipe->onEnd = boost::bind(onRequestBodyPipeEnd,
			weak_ptr<Client>(shared_from_this()), _1);
		requestBodyPipe->onCommit = boost::bind(onRequestBodyPipeCommit,
			weak_ptr<Client>(shared_from_this()), _1);
		
		responsePipe = make_shared<FileBackedPipe>(threadPool);
		responsePipe->onData = boost::bind(onResponsePipeData,
			weak_ptr<Client>(shared_from_this()), _1, _2, _3);
		responsePipe->onEnd = boost::bind(onResponsePipeEnd,
			weak_ptr<Client>(shared_from_this()), _1);
		responsePipe->onCommit = boost::bind(onResponsePipeCommit,
			weak_ptr<Client>(shared_from_this()), _1);
		
		resetPrimitiveFields();
	}

	~Client() {
		freeBufferedConnectPassword();
	}

	bool resetable() {
		return !backgroundOperating
			&& clientInput->resetable()
			&& appInput->resetable()
			&& requestBodyPipe->resetable()
			&& responsePipe->resetable();
	}

	void reset() {
		assert(resetable());
		resetPrimitiveFields();
		clientWriteBuffer.resize(0);
		appWriteBuffer.resize(0);
		scgiParser.reset();
		session.reset();
		clientInput->reset(NULL, FileDescriptor());
		appInput->reset(NULL, FileDescriptor());
		requestBodyPipe->reset();
		responsePipe->reset();
	}

	void freeBufferedConnectPassword() {
		if (bufferedConnectPassword.data != NULL) {
			free(bufferedConnectPassword.data);
			bufferedConnectPassword.data = NULL;
			bufferedConnectPassword.alreadyRead = 0;
		}
	}

	void _onClientSocketWritable(ev::io &io, int revents) {
		requestHandler->onClientSocketWritable(shared_from_this());
	}

	void _onAppSocketWritable(ev::io &io, int revents) {
		requestHandler->onAppSocketWritable(shared_from_this());
	}
};


/*
   Stages:
   
   Accept connect password
            |
            |
       Read header
            |
            |
           / \
          /   \
         /     \
        /       \
       /         \
    Buffer        \
    request       /
    body         /
      |         /
      |        /
   Checkout --+
   session
      |
      |
 Send header
   to app
      |
      |
 Send request
  body to app
  and forward
  response to
    client

 */

class RequestHandler {
private:
	friend class Client;

	SafeLibev *libev;
	HashMap<int, ClientPtr> clients;
	bool accept4Available;


	FileDescriptor acceptNonBlockingSocket(int sock) {
		if (accept4Available) {
			FileDescriptor fd = callAccept4(requestSocket, ..., O_NONBLOCK);
			if (fd == -1 && errno == ENOSYS) {
				accept4Available = false;
				return acceptNonBlockingSocket(sock);
			} else {
				return fd;
			}
		} else {
			FileDescriptor fd = syscalls::accept(requestSocket, ...);
			if (fd != -1) {
				int e = errno;
				makeNonBlock(fd);
				errno = e;
			}
			return fd;
		}
	}

	void disconnect(const ClientPtr &client) {
		// Prevent Client object from being destroyed until we're done.
		ClientPtr reference = client;

		clients.erase(client->fd);
		client->state = Client::DISCONNECTED;
		client->requestHandler = NULL;
		client->clientInput.stop();
		client->appInput.stop();
		if (client->writeWatcher.started()) {
			client->writeWatcher.stop();
		}
		if (client->appWriteWatcher.started()) {
			client->appWriteWatcher.stop();
		}
	}

	void disconnectWithError(const ClientPtr &client, const StaticString &message) {
		P_WARN("Disconnected client " << client->name() << " with error: " << message);
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


	void onAcceptable(ev::io &io, int revents) {
		bool done = false;

		while (!done) {
			FileDescriptor fd = acceptNonBlockingSocket(requestSocket);
			if (fd == -1) {
				if (errno == EAGAIN) {
					done = true;
				} else {
					int e = errno;
					throw SystemException("Cannot accept client", e);
				}
			} else {
				client = make_shared<Client>();
				client->fd = fd;
				client->clientInput->reset(libev, fd);
				client->clientInput->start();
				client->writeWatcher.set(libev->getLoop());
				client->writeWatcher.set<Client, Client::_onClientSocketWritable>(client.get());
				clients.insert(make_pair((int) fd, client));
			}
		}
	}


	size_t onClientData(const ClientPtr &client, const StaticString &data) {
		if (!client->connected()) {
			return;
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

		while (consumed < size && client->state != Client::DISCONNECTED && client->clientInput.started()) {
			const char *data = buf + consumed;
			size_t len       = ret - consumed;

			switch (client->state) {
			case BEGIN_READING_CONNECT_PASSWORD:
				consumed += state_beginReadingConnectPassword_onClientData(data, len);
				break;
			case STILL_READING_CONNECT_PASSWORD:
				consumed += state_stillReadingConnectPassword_onClientData(data, len);
				break;
			case READING_HEADER:
				consumed += state_readingHeader_onClientData(data, len);
				break;
			case BUFFERING_REQUEST_BODY:
				consumed += state_bufferingRequestBody_onClientData(data, len);
				break;
			case FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE:
				consumed += state_forwardingBodyToAppAndReadingItsResponse_onClientData(data, len);
				break;
			default:
				abort();
			}

			assert(consumed <= size);
		}

		return consumed;
	}

	void onClientEof(const ClientPtr &client) {
		switch (client->state) {
		case BUFFERING_REQUEST_BODY:
			state_bufferingRequestBody_onClientEof(client);
			break;
		case FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE:
			state_forwardingBodyToAppAndReadingItsResponse_onClientEof(client);
			break;
		default:
			disconnect(client);
			break;
		}
	}

	void onClientError(const ClientPtr &client, const char *message, int errnoCode) {
		if (!client->connected()) {
			return;
		}
		if (errnoCode == ECONNRESET) {
			// We might as well treat ECONNRESET like an EOF.
			// http://stackoverflow.com/questions/2974021/what-does-econnreset-mean-in-the-context-of-an-af-local-socket
			onClientEof(client);
		} else {
			switch (client->state) {
			default:
				stringstream message;
				message << "client socket read error: ";
				message << strerror(errnoCode);
				message << " (errno " << errnoCode << ")";
				disconnectWithError(client, message.str());
				break;
			}
		}
	}

	void onClientWritable(const ClientPtr &client) {
		if (!client->connected()) {
			return;
		}
		switch (client->state) {
		case SENDING_HEADER_TO_APP:
			state_sendingHeaderToApp_onClientWritable(client);
			break;
		case WRITING_CLIENT_BUFFER:
			state_writingClientBuffer_onClientWritable(client);
			break;
		case FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE:
			state_forwardingBodyToAppAndReadingItsResponse_onClientWritable(client);
			break;
		default:
			abort();
		}
	}

	size_t onAppSocketData(const ClientPtr &client, const StaticString &data) {
		if (!client->connected()) {
			return;
		}
		if (data.empty()) {
			onAppSocketEof(client);
			return 0;
		} else {
			return onAppSocketRealData(client, data.data(), data.size());
		}
	}

	size_t onAppSocketRealData(const ClientPtr &client, const char *data, size_t size) {
		size_t consumed = 0;

		while (consumed < size && client->state != Client::DISCONNECTED && client->clientInput.started()) {
			const char *data = buf + consumed;
			size_t len       = ret - consumed;

			switch (client->state) {
			case SENDING_HEADER_TO_APP:
				consumed += state_sendingHeaderToApp_onAppSocketData(data, len);
				break;
			case FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE:
				consumed += state_forwardingBodyToAppAndReadingItsResponse_onAppSocketData(data, len);
				break;
			default:
				abort();
			}

			assert(consumed <= size);
		}

		return consumed;
	}

	void onAppSocketEof(const ClientPtr &client) {
		switch (client->state) {
		case SENDING_HEADER_TO_APP:
			state_sendingHeaderToApp_onAppSocketEof(client);
			break;
		case FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE:
			state_forwardingBodyToAppAndReadingItsResponse_onAppSocketEof(client);
			break;
		default:
			abort();
		}
	}

	void onAppSocketError(const ClientPtr &client, const char *message, int errnoCode) {
		if (!client->connected()) {
			return;
		}
		if (e == ECONNRESET) {
			// We might as well treat ECONNRESET like an EOF.
			// http://stackoverflow.com/questions/2974021/what-does-econnreset-mean-in-the-context-of-an-af-local-socket
			onAppSocketEof(client);
		} else {
			stringstream message;
			message << "client socket read error: ";
			message << strerror(errnoCode);
			message << " (errno " << errnoCode << ")";
			disconnectWithError(client, message.str());
		}
	}

	void onAppSocketWritable(const ClientPtr &client) {
		if (!client->connected()) {
			return;
		}
		switch (client->state) {
		case SENDING_HEADER_TO_APP:
			state_sendingHeaderToApp_onAppSocketWritable(client);
			break;
		case FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE:
			state_forwardingBodyToAppAndReadingItsResponse_onAppSocketWritable(client);
			break;
		default:
			abort();
		}
	}

	void onRequestBodyPipeData(const ClientPtr &client, const char *data, size_t size, bool direct) {
		if (!client->connected()) {
			return;
		}
		switch (client->state) {
		case FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE:
			state_forwardingBodyToAppAndReadingItsResponse_onRequestBodyPipeData(client, data, size, direct);
			break;
		default:
			abort();
		}
	}

	void onRequestBodyPipeEof(const ClientPtr &client, bool direct) {
		if (!client->connected()) {
			return;
		}
		switch (client->state) {
		case FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE:
			state_forwardingBodyToAppAndReadingItsResponse_onRequestBodyPipeEof(client, direct);
			break;
		default:
			abort();
		}
	}

	void onRequestBodyPipeBufferDrained(const ClientPtr &client, bool direct) {
		if (!client->connected()) {
			return;
		}
		switch (client->state) {
		case BUFFERING_REQUEST_BODY:
			state_bufferingRequestBody_onRequestBodyPipeBufferDrained(client, direct);
			break;
		default:
			abort();
		}
	}

	void onResponsePipeData(const ClientPtr &client, const char *data, size_t size, bool direct) {
		if (!client->connected()) {
			return;
		}
		switch (client->state) {
		default:
			abort();
		}
	}

	void onResponsePipeBufferDrained(const ClientPtr &client, bool direct) {
		if (!client->connected()) {
			return;
		}
		switch (client->state) {
		default:
			abort();
		}
	}


	/******* State: BEGIN_READING_CONNECT_PASSWORD *******/

	void checkConnectPassword(const ClientPtr &client, const char *data, unsigned int len) {
		if (StaticString(data, len) == options.connectPassword) {
			client->state = Client::READING_HEADER;
			client->freeBufferedConnectPassword();
		} else {
			disconnectWithError(client, "wrong connect password");
		}
	}

	size_t state_beginReadingConnectPassword_onClientData(const ClientPtr &client, const char *data, size_t size) {
		if (size >= options.connectPassword.size()) {
			checkConnectPassword(client, data, options.connectPassword.size());
			return options.connectPassword.size();
		} else {
			client->bufferedConnectPassword.data = malloc(options.connectPassword.size());
			client->bufferedConnectPassword.alreadyRead = size;
			memcpy(client->bufferedConnectPassword.data, data, size);
			client->state = Client::STILL_READING_CONNECT_PASSWORD;
			return size;
		}
	}


	/******* State: STILL_READING_CONNECT_PASSWORD *******/

	size_t state_stillReadingConnectPassword_onClientData(const ClientPtr &client, const char *data, size_t size) {
		size_t consumed = std::min<size_t>(len, options.connectPassword.size() -
			client->bufferedConnectPassword.alreadyRead);
		memcpy(client->bufferedConnectPassword.data + client->bufferedConnectPassword.alreadyRead,
			data, consumed);
		client->bufferedConnectPassword.alreadyRead += consumed;
		if (client->bufferedConnectPassword.alreadyRead == options.connectPassword.size()) {
			checkConnectPassword(client, client->bufferedConnectPassword.data,
				options.connectPassword.size());
		}
		return consumed;
	}


	/******* State: READING_HEADER *******/

	size_t state_readingHeader_onClientData(const ClientPtr &client, const char *data, size_t size) {
		size_t consumed = client->scgiParser.feed(data, len);
		if (!client->scgiParser.acceptingInput()) {
			if (client->scgiParser.hasError()) {
				disconnectWithError(client, "invalid SCGI header");
			} else if (client->scgiParser.getHeader("PASSENGER_BUFFERING") == "true") {
				client->state = Client::BUFFERING_REQUEST_BODY;
				client->requestBodyIsBuffered = true;
			} else {
				client->clientInput.stop();
				checkoutSession(client);
			}
		}
		return consumed;
	}


	/******* State: BUFFERING_REQUEST_BODY *******/

	void state_bufferingRequestBody_verifyInvariants(const ClientPtr &client) const {
		assert(client->requestBodyIsBuffered);
		assert(!client->requestBodyPipe->started());
	}

	size_t state_bufferingRequestBody_onClientData(const ClientPtr &client, const char *data, size_t size) {
		state_bufferingRequestBody_verifyInvariants(client);

		if (!client->requestBodyPipe->write(data, size)) {
			// The pipe cannot write the data to disk quickly enough, so
			// suspend reading from the client until the pipe is done.
			client->backgroundOperations++;
			client->clientInput->stop();
		}
		return size;
	}

	void state_bufferingRequestBody_onClientEof(const ClientPtr &client) {
		state_bufferingRequestBody_verifyInvariants(client);

		client->clientInput->stop();
		client->requestBodyPipe->end();
		checkoutSession(client);
	}

	void state_bufferingRequestBody_onRequestBodyPipeBufferDrained(const ClientPtr &client) {
		if (!client->connected()) {
			return;
		}
		// Now that the pipe has committed the data to disk.
		// Resume reading from the client socket.
		state_bufferingRequestBody_verifyInvariants(client);
		assert(!client->clientInput->started());
		client->backgroundOperations--;
		client->clientInput->start();
	}


	/******* State: CHECKING_OUT_SESSION *******/

	void state_checkingOutSession_verifyInvariants(const ClientPtr &client) {
		assert(!client->clientInput.started());
		assert(!client->requestBodyPipe.started());
	}

	void checkoutSession(const ClientPtr &client) {
		const ScgiRequestParser &parser = client->scgiParser;
		Options &options = client->options;

		options.appRoot = parser.getHeader("PASSENGER_APP_ROOT");
		// ...

		client->state = Client::CHECKING_OUT_SESSION;
		pool->asyncGet(client->options, boost::bind(&RequestHandler::sessionCheckedOut,
			this, client, _1, _2));
		if (!client->sessionCheckedOut) {
			client->backgroundOperating = true;
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
		client->backgroundOperating = false;
		client->sessionCheckedOut   = true;

		if (e != NULL) {
			try {
				shared_ptr<SpawnException> e2 = dynamic_pointer_cast<SpawnException>(e);
				writeSimpleResponse(client, e2.getErrorPage());
			} catch (const bad_cast &) {
				writeSimpleResponse(client, e.what());
			}
		} else {
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
				client->sessionCheckedOut = false;
				pool->asyncGet(client->options,
					boost::bind(&RequestHandler::sessionCheckedOut,
						this, client, _1, _2));
				if (!client->sessionCheckedOut) {
					client->backgroundOperating = true;
				}
			} else {
				disconnectWithError(client, "could not initiate a session");
			}
			return;
		}
		
		client->appInput.reset(libev, client->session->fd());
		client->appInput.start();
		client->appWriteWatcher.set(libev->getLoop());
		client->appWriteWatcher.set(client->session->fd());
		sendHeaderToApp(client);
	}


	/******* Common code for states SENDING_HEADER_TO_APP        *******
	 ******* and FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE *******/

	/* In these two states, the RequestHandler tries to forward the app
	 * response to the client. The setup looks like this:
	 *
	 *   app    <===================>  client
	 *  socket      response pipe
	 *
	 * So the code here is split into two components:
	 * - Code for reading app socket data and writing the data
	 *   to the response pipe.
	 * - Code for forwarding the data in the response pipe
	 *   to the client.
	 */

	void state_common_verifyInvariants(const ClientPtr &client) {
		switch (client->state) {
		case SENDING_HEADER_TO_APP:
			state_sendingHeaderToApp_verifyInvariants(client);
			break;
		case FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE:
			state_forwardingBodyToAppAndReadingItsResponse_verifyInvariants(client);
			break;
		default:
			abort();
		}
	}

	// Component: app socket -> response pipe

	size_t state_common_onAppSocketData(const ClientPtr &client, const char *data,
		size_t size)
	{
		state_common_verifyInvariants(client);

		if (!client->responsePipe.write(data, size)) {
			client->backgroundOperations++;
			client->appInput->stop();
		}
		return size;
	}

	void state_common_onAppSocketEof(const ClientPtr &client) {
		state_common_verifyInvariants(client);

		client->responsePipe->end();
		client->appInput->stop();
	}

	void state_common_onResponsePipeBufferDrained(const ClientPtr &client) {
		if (!client->connected()) {
			return;
		}
		state_common_verifyInvariants(client);
		client->backgroundOperations--;
		client->appInput->start();
	}

	// Component: response pipe -> client

	void state_common_onResponsePipeData(const ClientPtr &client,
		const char *data, size_t size, const FileBackedPipe::ConsumeCallback &consumed)
	{
		if (!client->connected()) {
			consumed(0, true);
			return;
		}
		state_common_verifyInvariants(client);

		ssize_t ret = syscalls::write(client->fd, data, size);
		if (ret == -1) {
			if (errno == EAGAIN) {
				// Wait until the client socket is writable before resuming
				// forwarding app response.
				client->clientWriteWatcher.start();
			} else if (errno == EPIPE) {
				// If the client closed the connection then disconnect quietly.
				disconnect(client);
			} else {
				disconnectWithClientSocketWriteError(errno);
			}
			consumed(0, true);
		} else {
			consumed(ret, false);
		}
	}

	void state_common_onClientWritable(const ClientPtr &client) {
		state_common_verifyInvariants(client);

		// Continue forwarding data from the response pipe to the client.
		client->clientWriteWatcher.stop();
		assert(!client->responsePipe->started());
		client->responsePipe->start();
	}

	void state_common_onResponsePipeEof(const ClientPtr &client) {
		state_sendingHeaderToApp_verifyInvariants(client);

		syscalls::shutdown(client->fd, SHUT_WR);
	}


	/******* State: SENDING_HEADER_TO_APP *******/

	void state_sendingHeaderToApp_verifyInvariants(const ClientPtr &client) {
		assert(!client->clientInput->started());
		assert(!client->requestBodyPipe->started());
	}

	void sendHeaderToApp(const ClientPtr &client) {
		assert(!client->requestBodyPipe->started());
		ssize_t ret = gatheredWrite(client->session->fd(), data, 1, client->appWriteBuffer);
		if (ret == -1 && errno != EAGAIN) {
			disconnectWithAppSocketWriteError(client, errno);
		} else if (!client->appWriteBuffer.empty()) {
			client->state = Client::SENDING_HEADER_TO_APP;
			client->appWriteWatcher.start();
		} else {
			sendBodyToApp(client);
		}
	}

	// Component: header -> app socket

	void state_sendingHeaderToApp_onAppSocketWritable(const ClientPtr &client) {
		state_sendingHeaderToApp_verifyInvariants(client);

		ssize_t ret = gatheredWrite(client->session->fd(), NULL, 0, client->appWriteBuffer);
		if (ret == -1) {
			if (errno != EAGAIN && errno != EPIPE) {
				disconnectWithAppSocketWriteError(client, errno);
			}
		} else if (client->appWriteBuffer.empty()) {
			client->appWriteWatcher.stop();
			sendBodyToApp(client);
		}
	}


	/******* State: FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE *******/

	void state_forwardingBodyToAppAndReadingItsResponse_verifyInvariants(const ClientPtr &client) {
		assert(client->requestBodyPipe->started());
		assert(client->requestBodyIsBuffered == !client->clientInput->started());
	}

	void sendBodyToApp(const ClientPtr &client) {
		assert(client->appWriteBuffer.empty());
		assert(!client->clientInput->started());
		assert(!client->clientWriteWatcher.started());
		assert(!client->appWriteWatcher.started());
		client->state = Client::FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE;
		client->requestBodyPipe.start();
		if (!client->requestBodyIsBuffered) {
			client->clientInput->start();
		}
		client->appInput.start();
	}

	size_t state_forwardingBodyToAppAndReadingItsResponse_onClientData(const ClientPtr &client,
		const char *data, size_t size)
	{
		state_forwardingBodyToAppAndReadingItsResponse_verifyInvariants(client);
		assert(!client->requestBodyIsBuffered);

		ssize_t ret = syscalls::write(client->session->fd(), data, size);
		if (ret == -1) {
			if (errno == EAGAIN) {
				// App is not ready yet to receive this data. Try later
				// when the app socket is writable.
				client->clientInput->stop();
				client->appWriteWatcher.start();
			} else if (errno == EPIPE) {
				// Disconnect after response forwarding is done.
				client->clientInput->stop();
			} else {
				disconnectWithAppSocketWriteError(client, errno);
			}
			return 0;
		} else {
			return ret;
		}
	}

	void   state_forwardingBodyToAppAndReadingItsResponse_onClientEof(const ClientPtr &client) {
		state_forwardingBodyToAppAndReadingItsResponse_verifyInvariants(client);
		assert(!client->requestBodyIsBuffered);

		client->appInput->stop();
		syscalls::shutdown(client->session->fd(), SHUT_WR);
	}

	void   state_forwardingBodyToAppAndReadingItsResponse_onClientWritable(const ClientPtr &client) {
		state_forwardingBodyToAppAndReadingItsResponse_verifyInvariants(client);

		client->appInput.start();
		client->clientWriteWatcher.stop();
	}

	size_t state_forwardingBodyToAppAndReadingItsResponse_onRequestBodyPipeData(const ClientPtr &client,
		const char *data, size_t size, bool direct)
	{
		if (direct) {
			StaticString str(data, size);
			return state_forwardingBodyToAppAndReadingItsResponse_onRequestBodyPipeData_real<StaticString>(
				client, str);
		} else {
			string str(data, size);
			libev->runAsync(boost::bind(
				&RequestHandler::state_forwardingBodyToAppAndReadingItsResponse_onRequestBodyPipeData_real,
				this, client, str
			));
			return size;
		}
	}

	template<typename StringType>
	size_t state_forwardingBodyToAppAndReadingItsResponse_onRequestBodyPipeData_real(
		const ClientPtr &client, StringType data)
	{
		state_forwardingBodyToAppAndReadingItsResponse_verifyInvariants(client);
		assert(client->requestBodyIsBuffered);

		ssize_t ret = syscalls::write(client->session->fd(), data, size);
		if (ret == -1) {
			if (errno == EAGAIN) {
				// App is not ready yet to receive this data. Try later
				// when the app socket is writable.
				client->requestBodyPipe->stop();
				client->appWriteWatcher.start();
			} else if (errno == EPIPE) {
				// Disconnect after response forwarding is done.
				client->requestBodyPipe->stop();
			} else {
				disconnectWithAppSocketWriteError(client, errno);
			}
			return 0;
		} else {
			return ret;
		}
	}

	void   state_forwardingBodyToAppAndReadingItsResponse_onRequestBodyPipeEof(const ClientPtr &client) {
		state_forwardingBodyToAppAndReadingItsResponse_verifyInvariants(client);
		assert(client->requestBodyIsBuffered);

		client->appInput->stop();
		syscalls::shutdown(client->session->fd(), SHUT_WR);
	}

	size_t state_forwardingBodyToAppAndReadingItsResponse_onAppSocketData(const ClientPtr &client,
		const char *data, size_t size)
	{
		state_forwardingBodyToAppAndReadingItsResponse_verifyInvariants(client);

		ssize_t ret = syscalls::write(client->fd, data, size);
		if (ret == -1) {
			if (errno == EAGAIN) {
				// Wait until the client socket is writable before resuming
				// forwarding app response.
				client->appInput.stop();
				client->clientWriteWatcher.start();
			} else {
				disconnectWithClientSocketWriteError(errno);
			}
			return 0;
		} else {
			return ret;
		}
	}

	void   state_forwardingBodyToAppAndReadingItsResponse_onAppSocketEof(const ClientPtr &client) {
		state_forwardingBodyToAppAndReadingItsResponse_verifyInvariants(client);

		/* When we're here, we can be sure that all app socket data has
		 * been forwarded to the client socket, so we stop here regardless
		 * of whether the app has consumed the header or not. It's quite
		 * possible that the app sent a response without really reading
		 * the request.
		 */
		disconnect(client);
	}

	void   state_forwardingBodyToAppAndReadingItsResponse_onAppSocketWritable(const ClientPtr &client) {
		state_forwardingBodyToAppAndReadingItsResponse_verifyInvariants(client);

		client->clientInput->start();
		client->appWriteWatcher.stop();
	}


	/******* State: WRITING_CLIENT_BUFFER *******/

	void writeSimpleResponse(const ClientPtr &client, const StaticString &response) {
		assert(client->state < Client::FORWARDING_BODY_TO_APP_AND_READING_ITS_RESPONSE);
		client->clientInput.stop();
		ssize_t ret = gatheredWrite(client->fd, &response, 1, client->writeBuffer);
		if (ret == -1) {
			if (errno == EAGAIN) {
				state = Client::WRITING_CLIENT_BUFFER;
				client->writeWatcher.start();
			} else if (errno == EPIPE) {
				disconnect(client);
			} else {
				disconnectWithClientWriteError(client, errno);
			}
		} else if ((size_t) ret != response.size()) {
			state = Client::WRITING_CLIENT_BUFFER;
			client->writeWatcher.start();
		} else {
			disconnect(client);
		}
	}

	void state_writingClientBuffer_onClientWritable(const ClientPtr &client) {
		StaticString data = client->writeBuffer;
		ssize_t ret = gatheredWrite(client->fd, &data, 1, client->writeBuffer);
		if (ret == -1) {
			if (errno == EPIPE) {
				disconnect(client);
			} else if (errno != EAGAIN) {
				disconnectWithClientSocketWriteError(client, errno);
			}
		} else if ((size_t) ret == data.size()) {
			disconnect(client);
		}
	}

public:
	RequestHandler(SafeLibev *libev) {
		this->libev = libev;
		accept4Available = true;
	}
};
