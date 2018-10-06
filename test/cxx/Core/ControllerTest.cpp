#include <TestSupport.h>
#include <limits>
#include <Constants.h>
#include <IOTools/IOUtils.h>
#include <IOTools/BufferedIO.h>
#include <IOTools/MessageIO.h>
#include <Core/ApplicationPool/TestSession.h>
#include <Core/Controller.h>

using namespace std;
using namespace boost;
using namespace Passenger;
using namespace Passenger::Core;

namespace tut {
	struct Core_ControllerTest: public TestBase {
		class MyController: public Core::Controller {
		protected:
			virtual void asyncGetFromApplicationPool(Request *req,
				ApplicationPool2::GetCallback callback)
			{
				callback(sessionToReturn, exceptionToReturn);
				sessionToReturn.reset();
			}

		public:
			ApplicationPool2::AbstractSessionPtr sessionToReturn;
			ApplicationPool2::ExceptionPtr exceptionToReturn;

			MyController(ServerKit::Context *context,
				const Core::ControllerSchema &schema,
				const Json::Value &initialConfig,
				const Core::ControllerSingleAppModeSchema &singleAppModeSchema,
				const Json::Value &singleAppModeConfig)
				: Core::Controller(context, schema, initialConfig, ConfigKit::DummyTranslator(),
					&singleAppModeSchema, &singleAppModeConfig, ConfigKit::DummyTranslator())
				{ }
		};

		BackgroundEventLoop bg;
		ServerKit::Schema skSchema;
		ServerKit::Context context;
		WrapperRegistry::Registry wrapperRegistry;
		Core::ControllerSchema schema;
		Core::ControllerSingleAppModeSchema singleAppModeSchema;
		MyController *controller;
		SpawningKit::Context::Schema skContextSchema;
		SpawningKit::Context skContext;
		SpawningKit::FactoryPtr spawningKitFactory;
		ApplicationPool2::Context apContext;
		PoolPtr appPool;
		Json::Value config, singleAppModeConfig;
		int serverSocket;
		TestSession testSession;
		FileDescriptor clientConnection;
		BufferedIO clientConnectionIO;
		string peerRequestHeader;

		Core_ControllerTest()
			: bg(false, true),
			  context(skSchema),
			  singleAppModeSchema(&wrapperRegistry),
			  skContext(skContextSchema)
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
				bg.safe->runSync(boost::bind(&MyController::shutdown, controller, true));
				while (getServerState() != MyController::FINISHED_SHUTDOWN) {
					syscalls::usleep(10000);
				}
				bg.safe->runSync(boost::bind(&Core_ControllerTest::destroyController, this));
			}
			safelyClose(serverSocket);
			unlink("tmp.server");
			bg.stop();
		}

		void startLoop() {
			if (!bg.isStarted()) {
				bg.start();
			}
		}

		void destroyController() {
			delete controller;
		}

		void init() {
			controller = new MyController(&context, schema, config,
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

		void useTestSessionObject() {
			bg.safe->runSync(boost::bind(&Core_ControllerTest::_setTestSessionObject, this));
		}

		void _setTestSessionObject() {
			controller->sessionToReturn.reset(&testSession, false);
		}

		MyController::State getServerState() {
			Controller::State result;
			bg.safe->runSync(boost::bind(&Core_ControllerTest::_getServerState,
				this, &result));
			return result;
		}

		void _getServerState(MyController::State *state) {
			*state = controller->serverState;
		}

		Json::Value inspectStateAsJson() {
			Json::Value result;
			bg.safe->runSync(boost::bind(&Core_ControllerTest::_inspectStateAsJson,
				this, &result));
			return result;
		}

		void _inspectStateAsJson(Json::Value *result) {
			*result = controller->inspectStateAsJson();
		}

		unsigned long long getTotalBytesConsumed() {
			unsigned long long result;
			bg.safe->runSync(boost::bind(&Core_ControllerTest::_getTotalBytesConsumed,
				this, &result));
			return result;
		}

		void _getTotalBytesConsumed(unsigned long long *result) {
			*result = controller->totalBytesConsumed;
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
		useTestSessionObject();

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
		useTestSessionObject();
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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();

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
		useTestSessionObject();
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
		useTestSessionObject();
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
		useTestSessionObject();
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
		useTestSessionObject();
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
		useTestSessionObject();

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
		useTestSessionObject();
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
		set_test_name("Session protocol: if application decides not to "
			" finish the response (close), and the client is still there "
			" we should send a 502 (which should log warn)");

		init();
		useTestSessionObject();

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
		set_test_name("HTTP protocol: if application decides not to "
			" finish the response (close), and the client is still there "
			" we should send a 502 (which should log warn)");

		init();
		useTestSessionObject();
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
}
