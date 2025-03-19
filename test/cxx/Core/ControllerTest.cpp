#include <TestSupport.h>
#include <limits>
#include <cassert>
#include <Constants.h>
#include <IOTools/IOUtils.h>
#include <IOTools/BufferedIO.h>
#include <IOTools/MessageIO.h>
#include <Core/ApplicationPool/TestSession.h>
#include <Core/Controller.h>
#include <ev.h>

using namespace std;
using namespace boost;
using namespace Passenger;
using namespace Passenger::Core;

namespace tut {
	/*
	 * How this test suite works:
	 *
	 * 1. Initialization:
	 *    Call init() early, which creates a controller and starts a background event loop.
	 *
	 * 2. Mocking sessions:
	 *    Use useTestSessionObject() to mock the session object that the the controller
	 *    receives from ApplicationPool.
	 *
	 * 3. Testing requests to the controller:
	 *    1. Establish a connection with connectToServer().
	 *    2. Use sendRequest() to send an HTTP requests.
	 *    3. Optional: call waitUntilSessionInitiated() to wait until the controller
	 *       has initiated a session. This adds an extra check so that if the test fails
	 *       then you at least know whether the problem is before or after session initiation.
	 *    4. Use readPeerRequestHeader() to read the HTTP response.
	 *    5. Perform assertions on peerRequestHeader, which contains the HTTP response header.
	 */
	struct Core_ControllerTest: public TestBase {
		/*
		 * Works just like the normal Core::Controller, but allows the `appPool->asyncGet()`
		 * result as well as the session socket connect timeout to be mocked by assigning the corresponding fields.
		 * It also allows causing the session socket to delay connecting forever by setting forceSessionSocketConnectTimeout.
		 */
		class TestController: public Core::Controller {
		protected:
			virtual void asyncGetFromApplicationPool(Request *req,
				ApplicationPool2::GetCallback callback)
				override
			{
				// If this assertion fails then it means one of:
				// - You didn't call `mockNextSession()`, or:
				// - The controller called `appPool->asyncGet()` too many times.
				//   Remember that `mockNextSession()` only mocks the session object
				//   for the specified number of calls. There's probably
				//   something deeper wrong here, so increase log level to figure out
				//   what's going on. One example is an EVENTUALLY timeout causing a
				//   varying number of calls to asyncGet to happen, and you mocked too few.
				assert(mockSession != nullptr || mockException != nullptr);
				callback(mockSession, mockException);
				if (--mockedSessionCount == 0) {
					mockSession.reset();
					mockException.reset();
				}
			}

			virtual int getSessionSocketConnectIoWatchConditions() const override {
				if (forceSessionSocketConnectTimeout) {
					return EV_READ;
				} else {
					return Controller::getSessionSocketConnectIoWatchConditions();
				}
			}

			virtual double getSessionSocketEffectiveConnectTimeout(Request *req) const override {
				if (forceSessionSocketConnectTimeout) {
					return req->connectTimeout / 1000.0;
				} else {
					return Controller::getSessionSocketEffectiveConnectTimeout(req);
				}
			}

		public:
			ApplicationPool2::AbstractSessionPtr mockSession;
			ApplicationPool2::ExceptionPtr mockException;
			unsigned int mockedSessionCount;
			bool forceSessionSocketConnectTimeout;

			TestController(ServerKit::Context *context,
				const Core::ControllerSchema &schema,
				const Json::Value &initialConfig,
				const Core::ControllerSingleAppModeSchema &singleAppModeSchema,
				const Json::Value &singleAppModeConfig)
				: Core::Controller(context, schema, initialConfig, ConfigKit::DummyTranslator(),
				   &singleAppModeSchema, &singleAppModeConfig, ConfigKit::DummyTranslator()),
				  mockedSessionCount(0),
				  forceSessionSocketConnectTimeout(false)
				{ }
		};

		// Dependencies
		BackgroundEventLoop bg;
		ServerKit::Schema skSchema;
		ServerKit::Context context;
		WrapperRegistry::Registry wrapperRegistry;
		Core::ControllerSchema schema;
		Core::ControllerSingleAppModeSchema singleAppModeSchema;
		TestController *controller;
		SpawningKit::Context::Schema skContextSchema;
		SpawningKit::Context skContext;
		SpawningKit::FactoryPtr spawningKitFactory;
		ApplicationPool2::Context apContext;
		PoolPtr appPool;
		Json::Value config, singleAppModeConfig;
		int serverSocket;

		// Mock objects
		TestSession testSession;

		// Connection and response objects
		FileDescriptor clientConnection;
		BufferedIO clientConnectionIO;
		string peerRequestHeader;

		Core_ControllerTest()
			: bg(false, true),
			  context(skSchema),
			  singleAppModeSchema(&wrapperRegistry),
			  skContext(skContextSchema),
			  apContext(false)
		{
			config["thread_number"] = 1;
			config["multi_app"] = false;
			config["default_server_name"] = "localhost";
			config["default_server_port"] = 80;
			config["user_switching"] = false;

			singleAppModeConfig["app_root"] = "stub/rack";
			singleAppModeConfig["app_type"] = "rack";
			singleAppModeConfig["startup_file"] = "none";

			if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
				// If the user did not customize the test's log level,
				// then we'll want to tone down the noise.
				LoggingKit::setLevel(LoggingKit::WARN);
			}

			wrapperRegistry.finalize();
			controller = NULL;
			serverSocket = createUnixServer("tmp.server");

			context.libev = bg.safe;
			context.libuv = bg.libuv_loop;
			context.initialize();

			skContext.resourceLocator = resourceLocator;
			skContext.wrapperRegistry = &wrapperRegistry;
			skContext.integrationMode = "standalone";
			skContext.finalize();

			spawningKitFactory = boost::make_shared<SpawningKit::Factory>(&skContext);
			apContext.spawningKitFactory = spawningKitFactory;
			apContext.finalize();

			appPool = boost::make_shared<Pool>(&apContext);
			appPool->initialize();
		}

		~Core_ControllerTest() {
			startLoop();
			// Silence error disconnection messages during shutdown.
			LoggingKit::setLevel(LoggingKit::CRIT);
			clientConnection.close();
			if (controller != NULL) {
				bg.safe->runSync([&] { controller->shutdown(true); });
				while (getServerState() != TestController::FINISHED_SHUTDOWN) {
					syscalls::usleep(10000);
				}
				bg.safe->runSync([this] { delete controller; });
			}
			safelyClose(serverSocket);
			unlink("tmp.server");
			bg.stop();
		}

		void startLoop() {
			if (!bg.isStarted()) {
				bg.start("Core_ControllerTest Background Loop");
			}
		}

		void init() {
			controller = new TestController(&context, schema, config,
				singleAppModeSchema, singleAppModeConfig);
			controller->resourceLocator = resourceLocator;
			controller->wrapperRegistry = &wrapperRegistry;
			controller->appPool = appPool;
			controller->initialize();
			controller->listen(serverSocket);
			startLoop();
		}

		FileDescriptor &connectToServer() {
			startLoop();
			clientConnection = FileDescriptor(connectToUnixServer("tmp.server", __FILE__, __LINE__), NULL, 0);
			clientConnectionIO = BufferedIO(clientConnection);
			return clientConnection;
		}

		void sendRequest(const StaticString &data) {
			writeExact(clientConnection, data);
		}

		void sendRequestAndWait(const StaticString &data) {
			unsigned long long totalBytesConsumed = getTotalBytesConsumed();
			sendRequest(data);
			EVENTUALLY(5,
				result = getTotalBytesConsumed() >= totalBytesConsumed + data.size();
			);
			ensure_equals(getTotalBytesConsumed(), totalBytesConsumed + data.size());
		}

		/**
		 * Ensures that the next time the controller calls `appPool->asyncGet()`, it gets `testSession`
		 * instead of a real Session.
		 *
		 * Note that this only works for the next `sessions` number of invocations of `appPool->asyncGet()`.
		 * Subsequent calls get nullptr unless you call `mockNextSession()` again before that happens.
		 */
		void mockNextSession(unsigned int sessions = 1) {
			bg.safe->runSync([&] {
				controller->mockedSessionCount = sessions;
				controller->mockSession.reset(&testSession, false);
			});
		}

		TestController::State getServerState() {
			Controller::State result;
			bg.safe->runSync([&] { result = controller->serverState; });
			return result;
		}

		unsigned int getRemainingMockedRequests() {
			unsigned int result;
			bg.safe->runSync([&] { result = controller->mockedSessionCount; });
			return result;
		}

		Json::Value inspectStateAsJson() {
			Json::Value result;
			bg.safe->runSync([&] { result = controller->inspectStateAsJson(); });
			return result;
		}

		unsigned long long getTotalBytesConsumed() {
			unsigned long long result;
			bg.safe->runSync([&] { result = controller->totalBytesConsumed; });
			return result;
		}

		string readPeerRequestHeader(string *peerRequestHeader = NULL) {
			if (peerRequestHeader == NULL) {
				peerRequestHeader = &this->peerRequestHeader;
			}
			if (testSession.getProtocol() == "session") {
				*peerRequestHeader = readScalarMessage(testSession.peerFd());
			} else {
				*peerRequestHeader = readHeader(testSession.getPeerBufferedIO());
			}
			return *peerRequestHeader;
		}

		string readPeerBody() {
			if (testSession.getProtocol() == "session") {
				return readAll(testSession.peerFd(), std::numeric_limits<size_t>::max()).first;
			} else {
				return testSession.getPeerBufferedIO().readAll();
			}
		}

		void sendPeerResponse(const StaticString &data) {
			writeExact(testSession.peerFd(), data);
			testSession.closePeerFd();
		}

		bool tryDrainPeerConnection() {
			bool drained;
			SystemException e("", 0);

			setNonBlocking(testSession.peerFd());

			try {
				readAll(testSession.peerFd(), std::numeric_limits<size_t>::max());
				drained = true;
			} catch (const SystemException &e2) {
				e = e2;
				drained = false;
			}

			setBlocking(testSession.peerFd());
			if (drained) {
				return true;
			} else if (e.code() == EAGAIN) {
				return false;
			} else {
				throw e;
			}
		}

		void ensureNeverDrainPeerConnection() {
			SHOULD_NEVER_HAPPEN(100,
				result = tryDrainPeerConnection();
			);
		}

		void ensureEventuallyDrainPeerConnection() {
			unsigned long long timeout = 5000000;
			EVENTUALLY(5,
				if (!waitUntilReadable(testSession.peerFd(), &timeout)) {
					fail("Peer connection timed out");
				}
				result = tryDrainPeerConnection();
			);
		}

		void waitUntilSessionInitiated() {
			EVENTUALLY(5,
				result = testSession.fd() != -1;
			);
		}

		void waitUntilSessionClosed() {
			EVENTUALLY(5,
				result = testSession.isClosed();
			);
		}

		string readHeader(BufferedIO &io) {
			string result;
			do {
				string line = io.readLine();
				if (line == "\r\n" || line.empty()) {
					return result;
				} else {
					result.append(line);
				}
			} while (true);
		}

		string readResponseHeader() {
			return readHeader(clientConnectionIO);
		}

		string readResponseBody() {
			return clientConnectionIO.readAll();
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(Core_ControllerTest, 80);


	/***** Passing request information to the app *****/

	TEST_METHOD(1) {
		set_test_name("Session protocol: request URI");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello?foo=bar HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		readPeerRequestHeader();
		ensure(containsSubstring(peerRequestHeader,
			P_STATIC_STRING("REQUEST_URI\0/hello?foo=bar\0")));
	}

	TEST_METHOD(2) {
		set_test_name("HTTP protocol: request URI");

		init();
		mockNextSession();
		testSession.setProtocol("http_session");

		connectToServer();
		sendRequest(
			"GET /hello?foo=bar HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		readPeerRequestHeader();
		ensure(containsSubstring(peerRequestHeader,
			"GET /hello?foo=bar HTTP/1.1\r\n"));
	}


	/***** Passing request body to the app *****/

	TEST_METHOD(10) {
		set_test_name("When body buffering on, Content-Length given:"
			" it sets Content-Length in the forwarded request,"
			" and forwards the raw data");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"POST /hello HTTP/1.1\r\n"
			"!~: \r\n"
			"!~FLAGS: B\r\n"
			"!~: \r\n"
			"Host: localhost\r\n"
			"Content-Length: 5\r\n"
			"Connection: close\r\n"
			"\r\n"
			"hello");
		waitUntilSessionInitiated();

		Json::Value state = inspectStateAsJson();
		Json::Value reqState = state["active_clients"]["1-1"]["current_request"];
		ensure("Body buffering is on", reqState.isMember("body_bytes_buffered"));

		ensure(containsSubstring(readPeerRequestHeader(),
			P_STATIC_STRING("CONTENT_LENGTH\0005\000")));
		ensure_equals(readPeerBody(), "hello");
	}

	TEST_METHOD(11) {
		set_test_name("When body buffering on, Transfer-Encoding given:"
			" it sets Content-Length and removes Transfer-Encoding in the forwarded request,"
			" and forwards the chunked data");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"POST /hello HTTP/1.1\r\n"
			"!~: \r\n"
			"!~FLAGS: B\r\n"
			"!~: \r\n"
			"Host: localhost\r\n"
			"Transfer-Encoding: chunked\r\n"
			"Connection: close\r\n"
			"\r\n"
			"5\r\n"
			"hello\r\n"
			"0\r\n\r\n");
		waitUntilSessionInitiated();

		Json::Value state = inspectStateAsJson();
		Json::Value reqState = state["active_clients"]["1-1"]["current_request"];
		ensure("Body buffering is on", reqState.isMember("body_bytes_buffered"));

		string header = readPeerRequestHeader();
		ensure(containsSubstring(header,
			P_STATIC_STRING("CONTENT_LENGTH\0005\000")));
		ensure(!containsSubstring(header,
			P_STATIC_STRING("TRANSFER_ENCODING")));
		ensure_equals(readPeerBody(), "hello");
	}

	TEST_METHOD(12) {
		set_test_name("When body buffering on, Connection is upgrade:"
			" it refuses to buffer the request body,"
			" and forwards the raw data");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"POST /hello HTTP/1.1\r\n"
			"!~: \r\n"
			"!~FLAGS: B\r\n"
			"!~: \r\n"
			"Host: localhost\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: text\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		Json::Value state = inspectStateAsJson();
		Json::Value reqState = state["active_clients"]["1-1"]["current_request"];
		ensure("Body buffering is off", !reqState.isMember("body_bytes_buffered"));

		string header = readPeerRequestHeader();
		ensure(!containsSubstring(header,
			P_STATIC_STRING("CONTENT_LENGTH")));

		char buf[15];
		unsigned int size;

		writeExact(clientConnection, "ab");
		size = readExact(testSession.peerFd(), buf, 2);
		ensure_equals(size, 2u);
		ensure_equals(StaticString(buf, 2), "ab");

		writeExact(clientConnection, "cde");
		size = readExact(testSession.peerFd(), buf, 3);
		ensure_equals(size, 3u);
		ensure_equals(StaticString(buf, 3), "cde");
	}

	TEST_METHOD(13) {
		set_test_name("When body buffering off, Content-Length given:"
			" it sets Content-Length in the forwarded request,"
			" and forwards the raw data");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"POST /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Content-Length: 5\r\n"
			"Connection: close\r\n"
			"\r\n"
			"hello");
		waitUntilSessionInitiated();

		Json::Value state = inspectStateAsJson();
		Json::Value reqState = state["active_clients"]["1-1"]["current_request"];
		ensure("Body buffering is off", !reqState.isMember("body_bytes_buffered"));

		ensure(containsSubstring(readPeerRequestHeader(),
			P_STATIC_STRING("CONTENT_LENGTH\0005\000")));
		ensure_equals(readPeerBody(), "hello");
	}

	TEST_METHOD(14) {
		set_test_name("When body buffering off, Transfer-Encoding given:"
			" it forwards the chunked data");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"POST /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Transfer-Encoding: chunked\r\n"
			"Connection: close\r\n"
			"\r\n"
			"5\r\n"
			"hello\r\n"
			"0\r\n\r\n");
		waitUntilSessionInitiated();

		Json::Value state = inspectStateAsJson();
		Json::Value reqState = state["active_clients"]["1-1"]["current_request"];
		ensure("Body buffering is off", !reqState.isMember("body_bytes_buffered"));

		string header = readPeerRequestHeader();
		ensure(!containsSubstring(header,
			P_STATIC_STRING("CONTENT_LENGTH")));
		ensure(containsSubstring(header,
			P_STATIC_STRING("HTTP_TRANSFER_ENCODING\000chunked\000")));
		ensure_equals(readPeerBody(),
			"5\r\n"
			"hello\r\n"
			"0\r\n\r\n");
	}

	TEST_METHOD(15) {
		set_test_name("When body buffering off, Connection is upgrade:"
			" it forwards the raw data");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"POST /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: text\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		Json::Value state = inspectStateAsJson();
		Json::Value reqState = state["active_clients"]["1-1"]["current_request"];
		ensure("Body buffering is off", !reqState.isMember("body_bytes_buffered"));

		string header = readPeerRequestHeader();
		ensure(!containsSubstring(header,
			P_STATIC_STRING("CONTENT_LENGTH")));

		char buf[15];
		unsigned int size;

		writeExact(clientConnection, "ab");
		size = readExact(testSession.peerFd(), buf, 2);
		ensure_equals(size, 2u);
		ensure_equals(StaticString(buf, 2), "ab");

		writeExact(clientConnection, "cde");
		size = readExact(testSession.peerFd(), buf, 3);
		ensure_equals(size, 3u);
		ensure_equals(StaticString(buf, 3), "cde");
	}


	/***** Application response body handling *****/

	TEST_METHOD(20) {
		set_test_name("Fixed response body");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		readPeerRequestHeader();
		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Connection: close\r\n"
			"Content-Length: 5\r\n\r\n"
			"hello");

		string header = readResponseHeader();
		string body = readResponseBody();
		ensure(containsSubstring(header, "HTTP/1.1 200 OK\r\n"));
		ensure_equals(body, "hello");
	}

	TEST_METHOD(21) {
		set_test_name("Response body until EOF");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		readPeerRequestHeader();
		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Connection: close\r\n\r\n"
			"hello");

		string header = readResponseHeader();
		string body = readResponseBody();
		ensure("HTTP response OK", containsSubstring(header, "HTTP/1.1 200 OK\r\n"));
		ensure_equals(body, "hello");
	}

	TEST_METHOD(22) {
		set_test_name("Chunked response body");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		readPeerRequestHeader();
		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"5\r\n"
			"hello\r\n"
			"0\r\n\r\n");

		string header = readResponseHeader();
		string body = readResponseBody();
		ensure("HTTP response OK", containsSubstring(header, "HTTP/1.1 200 OK\r\n"));
		ensure_equals(body,
			"5\r\n"
			"hello\r\n"
			"0\r\n\r\n");
	}

	TEST_METHOD(23) {
		set_test_name("Upgraded response body");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		readPeerRequestHeader();
		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: text\r\n\r\n"
			"hello");

		string header = readResponseHeader();
		string body = readResponseBody();
		ensure("HTTP response OK", containsSubstring(header, "HTTP/1.1 200 OK\r\n"));
		ensure_equals(body, "hello");
	}


	/***** Application connection keep-alive *****/

	TEST_METHOD(30) {
		set_test_name("Perform keep-alive on application responses that allow it");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		readPeerRequestHeader();
		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");

		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
		ensure("(2)", testSession.wantsKeepAlive());
	}

	TEST_METHOD(31) {
		set_test_name("Don't perform keep-alive on application responses that don't allow it");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		readPeerRequestHeader();
		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Connection: close\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");

		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}

	TEST_METHOD(32) {
		set_test_name("Don't perform keep-alive if an error occurred");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		readPeerRequestHeader();
		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}
		sendPeerResponse("invalid response");

		waitUntilSessionClosed();
		ensure("(1)", !testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}


	/***** Passing half-close events to the app *****/

	TEST_METHOD(40) {
		set_test_name("Session protocol: on requests without body, it passes"
			" a half-close write event to the app on the next request's"
			" early read error and does not keep-alive the"
			" application connection");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();
		shutdown(clientConnection, SHUT_WR);
		ensureEventuallyDrainPeerConnection();

		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");
		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}

	TEST_METHOD(41) {
		set_test_name("Session protocol: on requests with fixed body, it passes"
			" a half-close write event to the app upon reaching the end"
			" of the request body and does not keep-alive the"
			" application connection");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Content-Length: 2\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();
		writeExact(clientConnection, "ok");
		ensureEventuallyDrainPeerConnection();

		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");
		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}

	TEST_METHOD(42) {
		set_test_name("Session protocol: on requests with chunked body, it passes"
			" a half-close write event to the app upon reaching the end"
			" of the request body and does not keep-alive the"
			" application connection");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Transfer-Encoding: chunked\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();
		writeExact(clientConnection, "0\r\n\r\n");
		ensureEventuallyDrainPeerConnection();

		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");
		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}

	TEST_METHOD(43) {
		set_test_name("Session protocol: on upgraded requests, it passes"
			" a half-close write event to the app upon reaching the end"
			" of the request body and does not keep-alive the"
			" application connection");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: text\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();
		writeExact(clientConnection, "hi");
		ensureNeverDrainPeerConnection();
		shutdown(clientConnection, SHUT_WR);
		ensureEventuallyDrainPeerConnection();

		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");
		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}

	TEST_METHOD(44) {
		set_test_name("HTTP protocol: on requests without body, it passes"
			" a half-close write event to the app on the next request's"
			" early read error and does not keep-alive the application connection");

		init();
		mockNextSession();
		testSession.setProtocol("http_session");

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();
		shutdown(clientConnection, SHUT_WR);
		ensureEventuallyDrainPeerConnection();

		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");
		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}

	TEST_METHOD(45) {
		set_test_name("HTTP protocol: on requests with fixed body, it passes"
			" a half-close write event to the app on the next request's"
			" early read error and does not keep-alive the application connection");

		init();
		mockNextSession();
		testSession.setProtocol("http_session");

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Content-Length: 2\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();
		writeExact(clientConnection, "ok");
		ensureNeverDrainPeerConnection();
		shutdown(clientConnection, SHUT_WR);
		ensureEventuallyDrainPeerConnection();

		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");
		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}

	TEST_METHOD(46) {
		set_test_name("HTTP protocol: on requests with chunked body, it passes"
			" a half-close write event to the app on the next request's early read error"
			" and does not keep-alive the application connection");

		init();
		mockNextSession();
		testSession.setProtocol("http_session");

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Transfer-Encoding: chunked\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();
		writeExact(clientConnection, "0\r\n\r\n");
		ensureNeverDrainPeerConnection();
		shutdown(clientConnection, SHUT_WR);
		ensureEventuallyDrainPeerConnection();

		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");
		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}

	TEST_METHOD(47) {
		set_test_name("HTTP protocol: on upgraded requests, it passes"
			" a half-close write event to the app upon reaching the end"
			" of the request body and does not keep-alive the"
			" application connection");

		init();
		mockNextSession();
		testSession.setProtocol("http_session");

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: text\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();
		writeExact(clientConnection, "ok");
		ensureNeverDrainPeerConnection();
		shutdown(clientConnection, SHUT_WR);
		ensureEventuallyDrainPeerConnection();

		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");
		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}

	TEST_METHOD(48) {
		set_test_name("Session protocol: if the client prematurely"
			" closes their outbound connection to us, and the"
			" application decides not to finish the response (close),"
			" we still try to send a 502 (which shouldn't log warn)");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();
		shutdown(clientConnection, SHUT_WR);
		ensureEventuallyDrainPeerConnection();

		close(testSession.peerFd());

		string header = readResponseHeader();
		ensure(containsSubstring(header, "HTTP/1.1 502"));
	}

	TEST_METHOD(49) {
		set_test_name("HTTP protocol: if the client prematurely"
			" closes their outbound connection to us, and the"
			" application decides not to finish the response (close),"
			" we still try to send a 502 (which shouldn't log warn)");

		init();
		mockNextSession();
		testSession.setProtocol("http_session");

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();
		shutdown(clientConnection, SHUT_WR);
		ensureEventuallyDrainPeerConnection();

		close(testSession.peerFd());

		string header = readResponseHeader();
		ensure(containsSubstring(header, "HTTP/1.1 502"));
	}

	TEST_METHOD(55) {
		set_test_name("Session protocol: if application decides not to"
			" finish the response (close), and the client is still there"
			" we should send a 502 (which should log warn)");

		init();
		mockNextSession();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}
		close(testSession.peerFd());

		string header = readResponseHeader();
		ensure(containsSubstring(header, "HTTP/1.1 502"));
	}

	TEST_METHOD(56) {
		set_test_name("HTTP protocol: if application decides not to"
			" finish the response (close), and the client is still there"
			" we should send a 502 (which should log warn)");

		init();
		mockNextSession();
		testSession.setProtocol("http_session");

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		ensureNeverDrainPeerConnection();

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}
		close(testSession.peerFd());

		string header = readResponseHeader();
		ensure(containsSubstring(header, "HTTP/1.1 502"));
	}

	/***** Application connection non-instant connect *****/

	TEST_METHOD(60) {
		set_test_name("If app connection is not instantly established,"
			" then it waits until establishment finishes");

		init();
		mockNextSession();
		testSession.forceNonInstantConnect();

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");
		waitUntilSessionInitiated();

		readPeerRequestHeader();
		sendPeerResponse(
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");

		waitUntilSessionClosed();
		ensure("(1)", testSession.isSuccessful());
	}

	TEST_METHOD(61) {
		set_test_name("If app connection establishment keeps timing out,"
			" then it returns a 504 Gateway Timeout error");

		init();
		mockNextSession(Controller::MAX_SESSION_CHECKOUT_TRY);
		testSession.forceNonInstantConnect();
		controller->forceSessionSocketConnectTimeout = true;

		connectToServer();
		sendRequest(
			"GET /hello HTTP/1.1\r\n"
			"!~: \r\n"
			"!~PASSENGER_APP_CONNECT_TIMEOUT: 1\r\n" // fast for EVENTUALLY timeout below
			"!~: \r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n"
			"\r\n");

		EVENTUALLY(5,
			result = getRemainingMockedRequests() == 0;
		);
		// We'll want to assert that the Core Controller responds with a timeout error.
		waitUntilSessionClosed();
		ensure_not("(1)", testSession.isSuccessful());
		string header = readResponseHeader();
		ensure(containsSubstring(header, "HTTP/1.1 504 Gateway Timeout"));
	}
}
