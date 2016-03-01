#include <TestSupport.h>
#include <Constants.h>
#include <Utils/IOUtils.h>
#include <Utils/BufferedIO.h>
#include <Utils/MessageIO.h>
#include <Core/ApplicationPool/TestSession.h>
#include <Core/Controller.h>

using namespace std;
using namespace boost;
using namespace Passenger;
using namespace Passenger::Core;

namespace tut {
	struct Core_ControllerTest {
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

			MyController(ServerKit::Context *context, const VariantMap *agentsOptions)
				: Core::Controller(context, agentsOptions)
				{ }
		};

		BackgroundEventLoop bg;
		ServerKit::Context context;
		MyController *controller;
		VariantMap options;
		int serverSocket;
		TestSession testSession;
		FileDescriptor clientConnection;
		BufferedIO clientConnectionIO;
		string peerRequestHeader;

		Core_ControllerTest()
			: bg(false, true),
			  context(bg.safe, bg.libuv_loop)
		{
			options.setInt("stat_throttle_rate", DEFAULT_STAT_THROTTLE_RATE);
			options.setInt("response_buffer_high_watermark", DEFAULT_RESPONSE_BUFFER_HIGH_WATERMARK);
			options.setBool("show_version_in_header", true);
			options.setBool("sticky_sessions", false);
			options.setBool("core_graceful_exit", true);
			options.setBool("multi_app", false);
			options.set("environment", DEFAULT_APP_ENV);
			options.set("app_root", "stub/rack");
			options.set("app_type", "dummy");
			options.set("startup_file", "none");
			options.set("default_ruby", DEFAULT_RUBY);
			options.set("default_server_name", "localhost");
			options.setInt("default_server_port", 80);
			options.set("server_software", PROGRAM_NAME);
			options.set("sticky_sessions_cookie_name", DEFAULT_STICKY_SESSIONS_COOKIE_NAME);
			options.setBool("user_switching", false);
			options.setInt("min_instances", 1);
			options.setInt("max_preloader_idle_time", DEFAULT_MAX_PRELOADER_IDLE_TIME);
			options.setInt("max_request_queue_size", DEFAULT_MAX_REQUEST_QUEUE_SIZE);
			options.setBool("abort_websockets_on_process_shutdown", true);
			options.setInt("force_max_concurrent_requests_per_process", -1);
			options.set("spawn_method", DEFAULT_SPAWN_METHOD);
			options.setBool("load_shell_envvars", false);

			setLogLevel(LVL_WARN);
			controller = NULL;
			serverSocket = createUnixServer("tmp.server");
		}

		~Core_ControllerTest() {
			startLoop();
			// Silence error disconnection messages during shutdown.
			setLogLevel(LVL_CRIT);
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
			setLogLevel(DEFAULT_LOG_LEVEL);
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
			controller = new MyController(&context, &options);
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

		void sendPeerResponse(const StaticString &data) {
			writeExact(testSession.peerFd(), data);
			testSession.closePeerFd();
		}

		bool tryDrainPeerConnection() {
			bool drained;
			SystemException e("", 0);

			setNonBlocking(testSession.peerFd());

			try {
				readAll(testSession.peerFd());
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

	DEFINE_TEST_GROUP(Core_ControllerTest);


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


	/***** Application response body handling *****/

	TEST_METHOD(10) {
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

	TEST_METHOD(11) {
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

	TEST_METHOD(12) {
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

	TEST_METHOD(13) {
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

	TEST_METHOD(20) {
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

	TEST_METHOD(21) {
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

	TEST_METHOD(22) {
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
		setLogLevel(LVL_CRIT);
		sendPeerResponse("invalid response");

		waitUntilSessionClosed();
		ensure("(1)", !testSession.isSuccessful());
		ensure("(2)", !testSession.wantsKeepAlive());
	}


	/***** Passing half-close events to the app *****/

	TEST_METHOD(30) {
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

	TEST_METHOD(31) {
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

	TEST_METHOD(32) {
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

	TEST_METHOD(33) {
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

	TEST_METHOD(34) {
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

	TEST_METHOD(35) {
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

	TEST_METHOD(36) {
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

	TEST_METHOD(37) {
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
}
