#include <TestSupport.h>
#include <boost/foreach.hpp>
#include <boost/bind/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>
#include <limits>
#include <BackgroundEventLoop.h>
#include <ServerKit/HttpServer.h>
#include <LoggingKit/LoggingKit.h>
#include <FileDescriptor.h>
#include <Utils.h>
#include <IOTools/IOUtils.h>
#include <IOTools/BufferedIO.h>

using namespace Passenger;
using namespace Passenger::ServerKit;
using namespace Passenger::MemoryKit;
using namespace std;
using namespace oxt;

namespace tut {
	class MyRequest: public BaseHttpRequest {
	public:
		string body;
		bool testingHalfClose;

		DEFINE_SERVER_KIT_BASE_HTTP_REQUEST_FOOTER(MyRequest);
	};

	class MyClient: public BaseHttpClient<MyRequest> {
	public:
		MyClient(void *server)
			: BaseHttpClient<MyRequest>(server)
		{
			SERVER_KIT_BASE_HTTP_CLIENT_INIT();
		}

		DEFINE_SERVER_KIT_BASE_HTTP_CLIENT_FOOTER(MyClient, MyRequest);
	};

	class MyServer: public HttpServer<MyServer, MyClient> {
	private:
		typedef HttpServer<MyServer, MyClient> ParentClass;

		void testRequest(MyClient *client, MyRequest *req) {
			HeaderTable headers;
			const unsigned int BUFSIZE = 128;
			char *response = (char *) psg_pnalloc(req->pool, BUFSIZE);
			char *pos = response;
			const char *end = response + BUFSIZE;
			const LString *value;
			const LString::Part *part;

			headers.insert(req->pool, "date", "Thu, 11 Sep 2014 12:54:09 GMT");
			headers.insert(req->pool, "content-type", "text/plain");

			pos = appendData(pos, end, "hello ");
			part = req->path.start;
			while (part != NULL) {
				pos = appendData(pos, end, part->data, part->size);
				part = part->next;
			}

			value = req->headers.lookup("foo");
			if (value != NULL) {
				pos = appendData(pos, end, "\nFoo: ");
				part = value->start;
				while (part != NULL) {
					pos = appendData(pos, end, part->data, part->size);
					part = part->next;
				}
			}

			value = req->secureHeaders.lookup("!~Secure");
			if (value != NULL) {
				pos = appendData(pos, end, "\nSecure: ");
				part = value->start;
				while (part != NULL) {
					pos = appendData(pos, end, part->data, part->size);
					part = part->next;
				}
			}

			writeSimpleResponse(client, 200, &headers,
				StaticString(response, pos - response));
			endRequest(&client, &req);
		}

		void testBody(MyClient *client, MyRequest *req) {
			if (!req->hasBody() && !req->upgraded()) {
				writeSimpleResponse(client, 422, NULL, "Body required");
				if (!req->ended()) {
					endRequest(&client, &req);
				}
			}
		}

		void testBodyStop(MyClient *client, MyRequest *req) {
			if (!req->hasBody() && !req->upgraded()) {
				writeSimpleResponse(client, 422, NULL, "Body required");
				if (!req->ended()) {
					endRequest(&client, &req);
				}
			} else {
				refRequest(req, __FILE__, __LINE__);
				req->bodyChannel.stop();
				requestsWaitingToStartAcceptingBody.push_back(req);
				// Continues in startAcceptingBody()
			}
		}

		void startAcceptingBody(MyClient *client, MyRequest *req) {
			req->bodyChannel.start();
			// Continues in onRequestBody()
		}

		void testLargeResponse(MyClient *client, MyRequest *req) {
			const LString *value = req->headers.lookup("size");
			value = psg_lstr_make_contiguous(value, req->pool);
			unsigned int size = stringToUint(StaticString(value->start->data, value->size));
			char *body = (char *) psg_pnalloc(req->pool, size);
			memset(body, 'x', size);
			writeSimpleResponse(client, 200, NULL, StaticString(body, size));
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		}

		void testPath(MyClient *client, MyRequest *req) {
			if (req->path.start->next == NULL) {
				writeSimpleResponse(client, 200, NULL, "Contiguous: 1");
			} else {
				writeSimpleResponse(client, 500, NULL, "Contiguous: 0");
			}
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		}

		void testHalfClose(MyClient *client, MyRequest *req) {
			req->testingHalfClose = true;
			// Continues in onRequestEarlyHalfClose()
		}

		void testEarlyReadErrorDetection(MyClient *client, MyRequest *req) {
			req->nextRequestEarlyReadError = ENOSPC;
			writeSimpleResponse(client, 200, NULL, "OK");
			endRequest(&client, &req);
		}

	protected:
		virtual Channel::Result onClientDataReceived(MyClient *client, const MemoryKit::mbuf &buffer,
			int errcode)
		{
			if (errcode != 0) {
				clientDataErrors++;
			}
			return ParentClass::onClientDataReceived(client, buffer, errcode);
		}

		virtual void onRequestBegin(MyClient *client, MyRequest *req) {
			ParentClass::onRequestBegin(client, req);

			if (psg_lstr_cmp(&req->path, "/body_test")) {
				testBody(client, req);
			} else if (psg_lstr_cmp(&req->path, "/body_stop_test")) {
				testBodyStop(client, req);
			} else if (psg_lstr_cmp(&req->path, "/large_response")) {
				testLargeResponse(client, req);
			} else if (psg_lstr_cmp(&req->path, "/path_test")) {
				testPath(client, req);
			} else if (psg_lstr_cmp(&req->path, "/half_close_test")) {
				testHalfClose(client, req);
			} else if (psg_lstr_cmp(&req->path, "/early_read_error_detection_test")) {
				testEarlyReadErrorDetection(client, req);
			} else {
				testRequest(client, req);
			}
		}

		virtual Channel::Result onRequestBody(MyClient *client, MyRequest *req,
			const MemoryKit::mbuf &buffer, int errcode)
		{
			if (buffer.size() > 0) {
				// Data
				bodyBytesRead += buffer.size();
				req->body.append(buffer.start, buffer.size());
			} else if (errcode == 0) {
				// EOF
				req->body.insert(0, toString(req->body.size()) + " bytes: ");
				if (!req->testingHalfClose) {
					writeSimpleResponse(client, 200, NULL, req->body);
					endRequest(&client, &req);
				}
			} else {
				// Error
				req->body.insert(0, string("Request body error: ") +
					getErrorDesc(errcode) + "\n" +
					toString(req->body.size()) + " bytes: ");
				writeSimpleResponse(client, 422, NULL, req->body);
				if (!req->ended()) {
					endRequest(&client, &req);
				}
			}
			return Channel::Result(buffer.size(), false);
		}

		virtual void onNextRequestEarlyReadError(MyClient *client, MyRequest *req, int errcode) {
			ParentClass::onNextRequestEarlyReadError(client, req, errcode);
			if (req->testingHalfClose) {
				if (errcode == EARLY_EOF_DETECTED) {
					halfCloseDetected++;
				}
				endRequest(&client, &req);
			}
		}

		virtual void reinitializeRequest(MyClient *client, MyRequest *req) {
			ParentClass::reinitializeRequest(client, req);
			req->body.clear();
			req->testingHalfClose = false;
		}

		virtual void deinitializeRequest(MyClient *client, MyRequest *req) {
			unsigned int i;

			for (i = 0; i < requestsWaitingToStartAcceptingBody.size(); i++) {
				if (requestsWaitingToStartAcceptingBody[i] == req) {
					requestsWaitingToStartAcceptingBody.erase(
						requestsWaitingToStartAcceptingBody.begin() + i);
					unrefRequest(req, __FILE__, __LINE__);
					break;
				}
			}
			ParentClass::deinitializeRequest(client, req);
		}

		virtual bool supportsUpgrade(MyClient *client, MyRequest *req) {
			return allowUpgrades;
		}

		virtual bool shouldAutoDechunkBody(MyClient *client, MyRequest *req) {
			return enableAutoDechunkBody;
		}

	public:
		bool allowUpgrades;
		bool enableAutoDechunkBody;

		vector<MyRequest *> requestsWaitingToStartAcceptingBody;
		unsigned int bodyBytesRead;
		unsigned int halfCloseDetected;
		unsigned int clientDataErrors;

		MyServer(Context *context, const HttpServerSchema &schema,
			const Json::Value &initialConfig = Json::Value())
			: ParentClass(context, schema, initialConfig),
			  allowUpgrades(true),
			  enableAutoDechunkBody(true),
			  bodyBytesRead(0),
			  halfCloseDetected(0),
			  clientDataErrors(0)
			{ }

		void startAcceptingBody() {
			MyRequest *req;
			vector<MyRequest *> requestsWaitingToStartAcceptingBody;

			requestsWaitingToStartAcceptingBody.swap(
				this->requestsWaitingToStartAcceptingBody);

			foreach (req, requestsWaitingToStartAcceptingBody) {
				startAcceptingBody(static_cast<MyClient *>(req->client), req);
				unrefRequest(req, __FILE__, __LINE__);
			}
		}
	};

	struct ServerKit_HttpServerTest: public TestBase {
		typedef ClientRef<MyServer, MyClient> ClientRefType;

		BackgroundEventLoop bg;
		ServerKit::Schema skSchema;
		ServerKit::Context context;
		ServerKit::HttpServerSchema schema;
		boost::shared_ptr<MyServer> server;
		int serverSocket;
		FileDescriptor fd;
		BufferedIO io;

		ServerKit_HttpServerTest()
			: bg(false, true),
			  context(skSchema)
		{
			if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
				// If the user did not customize the test's log level,
				// then we'll want to tone down the noise.
				LoggingKit::setLevel(LoggingKit::WARN);
			}

			context.libev = bg.safe;
			context.libuv = bg.libuv_loop;
			context.initialize();

			serverSocket = createUnixServer("tmp.server");

			server = boost::make_shared<MyServer>(&context, schema);
			server->initialize();
			server->listen(serverSocket);
		}

		~ServerKit_HttpServerTest() {
			startLoop();
			fd.close();
			// Silence error disconnection messages during shutdown.
			LoggingKit::setLevel(LoggingKit::CRIT);
			bg.safe->runSync(boost::bind(&MyServer::shutdown, server.get(), true));
			while (getServerState() != MyServer::FINISHED_SHUTDOWN) {
				syscalls::usleep(10000);
			}
			bg.safe->runSync(boost::bind(&ServerKit_HttpServerTest::destroyServer,
				this));
			safelyClose(serverSocket);
			unlink("tmp.server");
			bg.stop();
		}

		void startLoop() {
			if (!bg.isStarted()) {
				bg.start();
			}
		}

		void destroyServer() {
			server.reset();
		}

		FileDescriptor &connectToServer() {
			startLoop();
			fd = FileDescriptor(connectToUnixServer("tmp.server", __FILE__, __LINE__), NULL, 0);
			io = BufferedIO(fd);
			return fd;
		}

		void sendRequest(const StaticString &data) {
			writeExact(fd, data);
		}

		void sendRequestAndWait(const StaticString &data) {
			unsigned long long totalBytesConsumed = getTotalBytesConsumed();
			sendRequest(data);
			EVENTUALLY(5,
				result = getTotalBytesConsumed() >= totalBytesConsumed + data.size();
			);
			ensure_equals(getTotalBytesConsumed(), totalBytesConsumed + data.size());
		}

		bool hasResponseData() {
			unsigned long long timeout = 0;
			return waitUntilReadable(fd, &timeout);
		}

		MyServer::State getServerState() {
			MyServer::State result;
			bg.safe->runSync(boost::bind(&ServerKit_HttpServerTest::_getServerState,
				this, &result));
			return result;
		}

		void _getServerState(MyServer::State *state) {
			*state = server->serverState;
		}

		unsigned long long getTotalBytesConsumed() {
			unsigned long long result;
			bg.safe->runSync(boost::bind(&ServerKit_HttpServerTest::_getTotalBytesConsumed,
				this, &result));
			return result;
		}

		void _getTotalBytesConsumed(unsigned long long *result) {
			*result = server->totalBytesConsumed;
		}

		unsigned long getTotalRequestsBegun() {
			unsigned long result;
			bg.safe->runSync(boost::bind(&ServerKit_HttpServerTest::_getTotalRequestsBegun,
				this, &result));
			return result;
		}

		void _getTotalRequestsBegun(unsigned long *result) {
			*result = server->totalRequestsBegun;
		}

		unsigned int getBodyBytesRead() {
			unsigned int result;
			bg.safe->runSync(boost::bind(&ServerKit_HttpServerTest::_getBodyBytesRead,
				this, &result));
			return result;
		}

		void _getBodyBytesRead(unsigned int *result) {
			*result = server->bodyBytesRead;
		}

		unsigned int getActiveClientCount() {
			unsigned int result;
			bg.safe->runSync(boost::bind(&ServerKit_HttpServerTest::_getActiveClientCount,
				this, &result));
			return result;
		}

		void _getActiveClientCount(unsigned int *result) {
			*result = server->activeClientCount;
		}

		unsigned int getNumRequestsWaitingToStartAcceptingBody() {
			unsigned int result;
			bg.safe->runSync(boost::bind(
				&ServerKit_HttpServerTest::_getNumRequestsWaitingToStartAcceptingBody,
				this, &result));
			return result;
		}

		void _getNumRequestsWaitingToStartAcceptingBody(unsigned int *result) {
			*result = server->requestsWaitingToStartAcceptingBody.size();
		}

		unsigned int getHalfCloseDetected() {
			unsigned int result;
			bg.safe->runSync(boost::bind(
				&ServerKit_HttpServerTest::_getHalfCloseDetected,
				this, &result));
			return result;
		}

		void _getHalfCloseDetected(unsigned int *result) {
			*result = server->halfCloseDetected;
		}

		unsigned int getClientDataErrors() {
			unsigned int result;
			bg.safe->runSync(boost::bind(
				&ServerKit_HttpServerTest::_getClientDataErrors,
				this, &result));
			return result;
		}

		void _getClientDataErrors(unsigned int *result) {
			*result = server->clientDataErrors;
		}

		void startAcceptingBody() {
			bg.safe->runLater(boost::bind(&ServerKit_HttpServerTest::_startAcceptingBody,
				this));
		}

		void _startAcceptingBody() {
			server->startAcceptingBody();
		}

		void shutdownServer() {
			bg.safe->runLater(boost::bind(&ServerKit_HttpServerTest::_shutdownServer,
				this));
		}

		void _shutdownServer() {
			server->shutdown();
		}

		string stripHeaders(const string &str) {
			string::size_type pos = str.find("\r\n\r\n");
			if (pos == string::npos) {
				return str;
			} else {
				string result = str;
				result.erase(0, pos + 4);
				return result;
			}
		}

		string readResponseHeader() {
			string result;
			string line;
			do {
				line = io.readLine();
				if (line.empty()) {
					break;
				} else {
					result.append(line);
					if (line == "\r\n") {
						break;
					}
				}
			} while (true);
			return result;
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(ServerKit_HttpServerTest, 120);


	/***** Valid HTTP header parsing *****/

	TEST_METHOD(1) {
		set_test_name("A complete header in one part");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n\r\n");
		string response = readAll(fd, 1024 * 1024).first;
		ensure_equals(response,
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: close\r\n"
			"Content-Length: 7\r\n\r\n"
			"hello /");
	}

	TEST_METHOD(2) {
		set_test_name("A complete header in multiple random-sized parts");

		connectToServer();
		sendRequestAndWait(
			"GET / HTTP/1.1\r\n"
			"Connect");
		ensure(!hasResponseData());

		sendRequestAndWait(
			"ion: close\r\n"
			"Host: fo");
		ensure(!hasResponseData());

		sendRequest(
			"o\r\n\r\n");

		string response = readAll(fd, 1024).first;
		ensure_equals(response,
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: close\r\n"
			"Content-Length: 7\r\n\r\n"
			"hello /");
	}

	TEST_METHOD(3) {
		set_test_name("A complete header in multiple complete lines");

		connectToServer();

		sendRequestAndWait("GET / HTTP/1.1\r\n");
		ensure(!hasResponseData());

		sendRequestAndWait("Connection: close\r\n");
		ensure(!hasResponseData());

		sendRequestAndWait("Host: foo\r\n");
		ensure(!hasResponseData());

		sendRequest("\r\n");

		string response = readAll(fd, 1024).first;
		ensure_equals(response,
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: close\r\n"
			"Content-Length: 7\r\n\r\n"
			"hello /");
	}

	TEST_METHOD(4) {
		set_test_name("The request path is stored in req->path, "
			"and headers are stored in req->headers");

		connectToServer();

		sendRequestAndWait("GET /");
		ensure(!hasResponseData());
		sendRequestAndWait("jo");
		ensure(!hasResponseData());
		sendRequestAndWait("o HTTP/1.1\r\n");
		ensure(!hasResponseData());

		sendRequestAndWait("Connection: close\r\n");
		ensure(!hasResponseData());

		sendRequestAndWait("Host: foo\r\n");
		ensure(!hasResponseData());

		sendRequestAndWait("F");
		ensure(!hasResponseData());
		sendRequestAndWait("oo: ");
		ensure(!hasResponseData());
		sendRequestAndWait("b");
		ensure(!hasResponseData());
		sendRequestAndWait("ar\r\n");
		ensure(!hasResponseData());

		sendRequest("\r\n");

		string response = readAll(fd, 1024).first;
		ensure_equals(response,
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: close\r\n"
			"Content-Length: 19\r\n\r\n"
			"hello /joo\n"
			"Foo: bar");
	}

	TEST_METHOD(5) {
		set_test_name("It ensures that req->path is contiguous");

		connectToServer();
		sendRequestAndWait("GET /p");
		sendRequestAndWait(
			"ath_test HTTP/1.1\r\n"
			"Connection: close\r\n\r\n");

		string response = readAll(fd, 1024).first;
		ensure(containsSubstring(response, "Contiguous: 1"));
	}


	/***** Invalid HTTP header parsing *****/

	TEST_METHOD(7) {
		set_test_name("Incomplete header, without closing connection");

		connectToServer();
		sendRequest("GET / HTT");
		SHOULD_NEVER_HAPPEN(100,
			result = hasResponseData();
		);
	}

	TEST_METHOD(8) {
		set_test_name("Incomplete header, half-closing connection");

		connectToServer();
		sendRequestAndWait("GET / HTT");
		syscalls::shutdown(fd, SHUT_WR);
		string response = readAll(fd, 1024).first;
		ensure_equals(response, "");
	}

	TEST_METHOD(9) {
		set_test_name("Invalid header data");

		connectToServer();
		sendRequest("whatever");
		string response = readAll(fd, 1024).first;
		ensure(containsSubstring(response,
			"HTTP/1.0 400 Bad Request\r\n"
			"Status: 400 Bad Request\r\n"
			"Content-Type: text/html; charset=UTF-8\r\n"));
		ensure(containsSubstring(response,
			"Connection: close\r\n"
			"Content-Length: 19\r\n"
			"cache-control: no-cache, no-store, must-revalidate\r\n\r\n"
			"invalid HTTP method"));
	}


	/***** Invalid request *****/

	TEST_METHOD(14) {
		set_test_name("HTTP > 1.1");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.2\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure(containsSubstring(response,
			"HTTP/1.0 505 HTTP Version Not Supported\r\n"
			"Status: 505 HTTP Version Not Supported\r\n"
			"Content-Type: text/html; charset=UTF-8\r\n"));
		ensure(containsSubstring(response,
			"Connection: close\r\n"
			"Content-Length: 27\r\n"
			"cache-control: no-cache, no-store, must-revalidate\r\n"
			"\r\n"
			"HTTP version not supported"));
	}

	TEST_METHOD(15) {
		set_test_name("Transfer-Encoding and Content-Length given simultaneously");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Content-Length: 3\r\n"
			"Transfer-Encoding: chunked\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure(containsSubstring(response,
			"HTTP/1.0 400 Bad Request\r\n"
			"Status: 400 Bad Request\r\n"
			"Content-Type: text/html; charset=UTF-8\r\n"));
		ensure(containsSubstring(response,
			"Connection: close\r\n"
			"Content-Length: 79\r\n"
			"cache-control: no-cache, no-store, must-revalidate\r\n"
			"\r\n"
			"Bad request (request may not contain both Content-Length and Transfer-Encoding)"));
	}

	TEST_METHOD(16) {
		set_test_name("Request Smuggling type: 1");

		connectToServer();
		sendRequest(
			"POST / HTTP/1.1\r\n"
			"Host: whatever\r\n"
			"Transfer-Encoding: ,chunked\r\n"
			"Content-Length: 5\r\n"
			"\r\n"
			"0\r\n"
			"\r\n");
		string response = readAll(fd, 1024).first;
		ensure(startsWith(response,
			"HTTP/1.0 400 Bad Request\r\n"
			"Status: 400 Bad Request\r\n"
			"Content-Type: text/html; charset=UTF-8\r\n"));
		ensure(endsWith(response,
			"Connection: close\r\n"
			"Content-Length: 42\r\n"
			"cache-control: no-cache, no-store, must-revalidate\r\n"
			"\r\n"
			"invalid character in content-length header"));
		ensure_equals(response.size(),265u);
	}

	TEST_METHOD(17) {
		set_test_name("Request Smuggling type: 2");

		connectToServer();
		sendRequest(
			"POST / HTTP/1.1\r\n"
			"Host: whatever\r\n"
			"Transfer-\r\n"
			"Encoding: chunked\r\n"
			"Content-Length: 5\r\n"
			"\r\n"
			"0\r\n"
			"\r\n");
		string response = readAll(fd, 1024).first;
		ensure(startsWith(response,
			"HTTP/1.0 400 Bad Request\r\n"
			"Status: 400 Bad Request\r\n"
			"Content-Type: text/html; charset=UTF-8\r\n"));
		ensure(endsWith(response,
			"Connection: close\r\n"
			"Content-Length: 27\r\n"
			"cache-control: no-cache, no-store, must-revalidate\r\n"
			"\r\n"
			"invalid character in header"));
		ensure_equals(response.size(),250u);
	}

	TEST_METHOD(18) {
		set_test_name("Request Smuggling type: 3");

		connectToServer();
		sendRequest(
			"POST / HTTP/1.1\r\n"
			"Host: whatever\r\n"
			"Transfer-Encoding: ,chunked\r\n"
			"\r\n"
			"0\r\n"
			"\r\n"
			);
		string response = readAll(fd, 1024).first;
		ensure_equals(response,
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: close\r\n"
			"Content-Length: 7\r\n"
			"\r\n"
			"hello /");
	}

	/***** Fixed body handling *****/

	TEST_METHOD(20) {
		set_test_name("An empty body is treated the same as no body");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Content-Length: 0\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response, "Body required"));
	}

	TEST_METHOD(21) {
		set_test_name("Non-empty body in one part");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "2 bytes: ok"));
	}

	TEST_METHOD(22) {
		set_test_name("Non-empty body in multiple parts");

		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Content-Length: 7\r\n\r\n"
			"hm");
		ensure(!hasResponseData());
		sendRequestAndWait("ok");
		ensure(!hasResponseData());
		sendRequest("!!!");

		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "7 bytes: hmok!!!"));
	}

	TEST_METHOD(23) {
		set_test_name("req->bodyChannel is stopped before request body data is received");

		connectToServer();
		sendRequest(
			"GET /body_stop_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Content-Length: 7\r\n\r\n"
			"hmok!!!");
		EVENTUALLY(5,
			result = getNumRequestsWaitingToStartAcceptingBody() == 1;
		);
		SHOULD_NEVER_HAPPEN(100,
			result = hasResponseData();
		);

		startAcceptingBody();
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "7 bytes: hmok!!!"));
	}

	TEST_METHOD(24) {
		set_test_name("req->bodyChannel is stopped before unexpected request body EOF is encountered");

		connectToServer();
		sendRequest(
			"GET /body_stop_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Content-Length: 3\r\n\r\n");
		syscalls::shutdown(fd, SHUT_WR);
		EVENTUALLY(5,
			result = getNumRequestsWaitingToStartAcceptingBody() == 1;
		);
		SHOULD_NEVER_HAPPEN(100,
			result = hasResponseData();
		);

		startAcceptingBody();
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response, "Request body error: Unexpected end-of-stream"));
	}

	TEST_METHOD(25) {
		set_test_name("Premature body termination");

		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Content-Length: 7\r\n\r\n"
			"hm");
		ensure(!hasResponseData());
		sendRequestAndWait("ok");
		ensure(!hasResponseData());
		sendRequestAndWait("!");
		syscalls::shutdown(fd, SHUT_WR);

		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response,
			"Request body error: Unexpected end-of-stream\n"
			"5 bytes: hmok!"));
	}

	TEST_METHOD(26) {
		set_test_name("Trailing data after body");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Content-Length: 2\r\n\r\n"
			"hmok");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "2 bytes: hm"));
		ensure("(3)", !containsSubstring(response, "ok"));
		EVENTUALLY(5,
			result = getTotalBytesConsumed() == strlen(
				"GET /body_test HTTP/1.1\r\n"
				"Connection: close\r\n"
				"Content-Length: 2\r\n\r\n"
				"hm");
		);
	}


	/***** Chunked body handling: auto-dechunking on *****/

	TEST_METHOD(30) {
		set_test_name("Empty body");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"0\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "0 bytes: "));
	}

	TEST_METHOD(31) {
		set_test_name("Non-empty body in one part");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"2\r\n"
			"ok\r\n"
			"0\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "2 bytes: ok"));
	}

	TEST_METHOD(32) {
		set_test_name("Non-empty body in multiple parts");

		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"2\r\n"
			"h");
		ensure(!hasResponseData());
		sendRequestAndWait("m\r");
		ensure(!hasResponseData());
		sendRequestAndWait("\n2\r");
		ensure(!hasResponseData());
		sendRequestAndWait("\no");
		ensure(!hasResponseData());
		sendRequestAndWait("k");
		ensure(!hasResponseData());
		sendRequestAndWait("\r\n3\r\n");
		ensure(!hasResponseData());
		sendRequestAndWait("!");
		ensure(!hasResponseData());
		sendRequestAndWait("!!\r\n0");
		ensure(!hasResponseData());
		sendRequest("\r\n\r\n");

		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "7 bytes: hmok!!!"));
	}

	TEST_METHOD(33) {
		set_test_name("Premature body termination");

		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"7\r\nhmok!");
		ensure(!hasResponseData());
		syscalls::shutdown(fd, SHUT_WR);

		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response,
			"Request body error: Unexpected end-of-stream\n"
			"5 bytes: hmok!"));
	}

	TEST_METHOD(34) {
		set_test_name("req->bodyChannel is stopped before request body data is received");

		connectToServer();
		sendRequest(
			"GET /body_stop_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"3\r\n"
			"abc\r\n"
			"0\r\n"
			"\r\n");
		EVENTUALLY(5,
			result = getNumRequestsWaitingToStartAcceptingBody() == 1;
		);
		SHOULD_NEVER_HAPPEN(100,
			result = hasResponseData();
		);

		startAcceptingBody();
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "3 bytes: abc"));
	}

	TEST_METHOD(35) {
		set_test_name("Trailing data after body");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"2\r\n"
			"hm\r\n"
			"0\r\n\r\n"
			"ok");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "2 bytes: hm"));
		ensure("(3)", !containsSubstring(response, "ok"));
		EVENTUALLY(5,
			result = getTotalBytesConsumed() == P_STATIC_STRING(
				"GET /body_test HTTP/1.1\r\n"
				"Connection: close\r\n"
				"Transfer-Encoding: chunked\r\n\r\n"
				"2\r\n"
				"hm\r\n"
				"0\r\n\r\n").size();
		);
	}

	TEST_METHOD(36) {
		set_test_name("Unterminated final chunk");

		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"7\r\nhmok!!!\r\n0\r\n\r");
		ensure(!hasResponseData());
		syscalls::shutdown(fd, SHUT_WR);

		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response,
			"Request body error: Unexpected end-of-stream\n"
			"7 bytes: hmok!!!"));
	}

	TEST_METHOD(37) {
		set_test_name("Invalid chunk header");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"!");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response, "0 bytes: "));
		ensure("(3)", !containsSubstring(response, "!"));
	}

	TEST_METHOD(38) {
		set_test_name("Invalid chunk footer");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"2\r\nok!");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response, "2 bytes: ok"));
		ensure("(3)", !containsSubstring(response, "!"));
	}

	TEST_METHOD(39) {
		set_test_name("Chunk larger than MAX_CHUNK_SIZE bytes");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			+ toString(ServerKit::HttpChunkedBodyParserState::MAX_CHUNK_SIZE + 1) + "\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response,
			P_STATIC_STRING("Request body error: ")
			+ ServerKit::getErrorDesc(ServerKit::CHUNK_SIZE_TOO_LARGE)
			+ "\n"));
	}


	/***** Chunked body handling: auto-dechunking off *****/

	TEST_METHOD(40) {
		set_test_name("Empty body");

		server->enableAutoDechunkBody = false;
		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"0\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "5 bytes: 0\r\n\r\n"));
	}

	TEST_METHOD(41) {
		set_test_name("Non-empty body in one part");

		server->enableAutoDechunkBody = false;
		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"2\r\n"
			"ok\r\n"
			"0\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "12 bytes: 2\r\nok\r\n0\r\n\r\n"));
	}

	TEST_METHOD(42) {
		set_test_name("Non-empty body in multiple parts");

		server->enableAutoDechunkBody = false;
		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"2\r\n"
			"h");
		ensure(!hasResponseData());
		sendRequestAndWait("m\r");
		ensure(!hasResponseData());
		sendRequestAndWait("\n2\r");
		ensure(!hasResponseData());
		sendRequestAndWait("\no");
		ensure(!hasResponseData());
		sendRequestAndWait("k");
		ensure(!hasResponseData());
		sendRequestAndWait("\r\n3\r\n");
		ensure(!hasResponseData());
		sendRequestAndWait("!");
		ensure(!hasResponseData());
		sendRequestAndWait("!!\r\n0");
		ensure(!hasResponseData());
		sendRequest("\r\n\r\n");

		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response,
			"27 bytes: 2\r\nhm\r\n2\r\nok\r\n3\r\n!!!\r\n0\r\n\r\n"));
	}

	TEST_METHOD(43) {
		set_test_name("Premature body termination");

		server->enableAutoDechunkBody = false;
		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"7\r\nhmok!");
		ensure(!hasResponseData());
		syscalls::shutdown(fd, SHUT_WR);

		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response,
			"Request body error: Unexpected end-of-stream\n"
			"8 bytes: 7\r\nhmok!"));
	}

	TEST_METHOD(44) {
		set_test_name("req->bodyChannel is stopped before request body data is received");

		server->enableAutoDechunkBody = false;
		connectToServer();
		sendRequest(
			"GET /body_stop_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"3\r\n"
			"abc\r\n"
			"0\r\n"
			"\r\n");
		EVENTUALLY(5,
			result = getNumRequestsWaitingToStartAcceptingBody() == 1;
		);
		SHOULD_NEVER_HAPPEN(100,
			result = hasResponseData();
		);

		startAcceptingBody();
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "13 bytes: 3\r\nabc\r\n0\r\n\r\n"));
	}

	TEST_METHOD(45) {
		set_test_name("Trailing data after body");

		server->enableAutoDechunkBody = false;
		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"2\r\n"
			"hm\r\n"
			"0\r\n\r\n"
			"ok");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "12 bytes: 2\r\nhm\r\n0\r\n\r\n"));
		ensure("(3)", !containsSubstring(response, "ok"));
		EVENTUALLY(5,
			result = getTotalBytesConsumed() == P_STATIC_STRING(
				"GET /body_test HTTP/1.1\r\n"
				"Connection: close\r\n"
				"Transfer-Encoding: chunked\r\n\r\n"
				"2\r\n"
				"hm\r\n"
				"0\r\n\r\n").size();
		);
	}

	TEST_METHOD(46) {
		set_test_name("Unterminated final chunk");

		server->enableAutoDechunkBody = false;
		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"7\r\nhmok!!!\r\n0\r\n\r");
		ensure(!hasResponseData());
		syscalls::shutdown(fd, SHUT_WR);

		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response,
			"Request body error: Unexpected end-of-stream\n"
			"16 bytes: 7\r\nhmok!!!\r\n0\r\n\r"));
	}

	TEST_METHOD(47) {
		set_test_name("Invalid chunk header");

		server->enableAutoDechunkBody = false;
		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"!");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response, "0 bytes: "));
		ensure("(3)", !containsSubstring(response, "!"));
	}

	TEST_METHOD(48) {
		set_test_name("Invalid chunk footer");

		server->enableAutoDechunkBody = false;
		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"2\r\nok!");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response, "5 bytes: 2\r\nok"));
		ensure("(3)", !containsSubstring(response, "!"));
	}


	/***** Upgrade handling *****/

	TEST_METHOD(50) {
		set_test_name("Empty body");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: raw\r\n\r\n");
		syscalls::shutdown(fd, SHUT_WR);
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "0 bytes: "));
	}

	TEST_METHOD(51) {
		set_test_name("Non-empty data in one part");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: raw\r\n\r\n"
			"ok");
		syscalls::shutdown(fd, SHUT_WR);
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "2 bytes: ok"));
	}

	TEST_METHOD(52) {
		set_test_name("Non-empty body in multiple parts");

		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: raw\r\n\r\n"
			"hm");
		ensure(!hasResponseData());
		sendRequestAndWait("ok");
		ensure(!hasResponseData());
		sendRequest("!!!");
		syscalls::shutdown(fd, SHUT_WR);

		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "7 bytes: hmok!!!"));
	}

	TEST_METHOD(53) {
		set_test_name("req->bodyChannel is stopped before request body data is received");

		connectToServer();
		sendRequest(
			"GET /body_stop_test HTTP/1.1\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: raw\r\n\r\n"
			"hmok!!!");
		syscalls::shutdown(fd, SHUT_WR);
		EVENTUALLY(5,
			result = getNumRequestsWaitingToStartAcceptingBody() == 1;
		);
		SHOULD_NEVER_HAPPEN(100,
			result = hasResponseData();
		);

		startAcceptingBody();
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "7 bytes: hmok!!!"));
	}

	TEST_METHOD(54) {
		set_test_name("req->bodyChannel is stopped before request body EOF is encountered");

		connectToServer();
		sendRequest(
			"GET /body_stop_test HTTP/1.1\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: raw\r\n\r\n");
		syscalls::shutdown(fd, SHUT_WR);
		EVENTUALLY(5,
			result = getNumRequestsWaitingToStartAcceptingBody() == 1;
		);
		SHOULD_NEVER_HAPPEN(100,
			result = hasResponseData();
		);

		startAcceptingBody();
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("(2)", containsSubstring(response, "0 bytes: "));
	}

	TEST_METHOD(55) {
		set_test_name("It rejects the upgrade if supportsUpgrade() returns false");

		server->allowUpgrades = false;
		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: raw\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 422 Unprocessable Entity\r\n"));
		ensure("(2)", containsSubstring(response, "Connection upgrading not allowed for this request"));
	}

	TEST_METHOD(56) {
		set_test_name("It rejects the upgrade if the request contains a request body");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: raw\r\n"
			"Content-Length: 3\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 400 Bad Request\r\n"));
		ensure("(2)", containsSubstring(response,
			"Connection upgrading is only allowed for requests without request body"));
	}

	TEST_METHOD(57) {
		set_test_name("It rejects the upgrade if the request is HEAD");

		connectToServer();
		sendRequest(
			"HEAD /body_test HTTP/1.1\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: raw\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 400 Bad Request\r\n"));
	}


	/***** Secure headers handling *****/

	TEST_METHOD(60) {
		set_test_name("It stores secure headers in req->secureHeaders");

		connectToServer();
		sendRequest(
			"GET /joo HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n"
			"!~: x\r\n"
			"!~Secure: secret\r\n"
			"\r\n");
		string response = readAll(fd, 1024).first;
		ensure_equals(response,
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: close\r\n"
			"Content-Length: 25\r\n\r\n"
			"hello /joo\n"
			"Secure: secret");
	}

	TEST_METHOD(61) {
		set_test_name("It rejects normal headers while in secure mode");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n"
			"!~: x\r\n"
			"!~Secure: secret\r\n"
			"Foo: bar\r\n"
			"\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.0 400 Bad Request\r\n"));
		ensure("(2)", containsSubstring(response,
			"A normal header was encountered after the security password header"));
	}

	TEST_METHOD(62) {
		set_test_name("It rejects secure headers while in normal mode");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n"
			"!~Secure: secret\r\n"
			"\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.0 400 Bad Request\r\n"));
		ensure("(2)", containsSubstring(response,
			"A secure header was provided, but no security password was provided"));
	}

	TEST_METHOD(63) {
		set_test_name("If no secure mode password is given in the context, "
			"switching to secure mode is always possible");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n"
			"!~: anything\r\n"
			"\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
	}

	TEST_METHOD(64) {
		set_test_name("If a secure mode password is given in the context, "
			"it rejects requests that specify the wrong secure mode password");

		Json::Value config;
		vector<ConfigKit::Error> errors;
		config["secure_mode_password"] = "secret";
		ensure(context.configure(config, errors));

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n"
			"!~: wrong\r\n"
			"\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.0 400 Bad Request\r\n"));
		ensure("(2)", containsSubstring(response,
			"Security password mismatch"));
	}

	TEST_METHOD(65) {
		set_test_name("If a secure mode password is given in the context, "
			"it accepts requests that specify the correct secure mode password");

		Json::Value config;
		vector<ConfigKit::Error> errors;
		config["secure_mode_password"] = "secret";
		ensure(context.configure(config, errors));

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n"
			"!~: secret\r\n"
			"!~Foo: bar\r\n"
			"\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
	}


	/***** Request ending *****/

	TEST_METHOD(70) {
		set_test_name("If all output data is flushed, and keep-alive is not possible, "
			"it disconnects the client immediately");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n\r\n");
		readAll(fd, std::numeric_limits<size_t>::max()); // Does not block
	}

	TEST_METHOD(71) {
		set_test_name("If all output data is flushed, and keep-alive is possible, "
			"it handles the next request immediately");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: keep-alive\r\n"
			"Host: foo\r\n\r\n"
			"GET /foo HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n\r\n");

		string response = readAll(fd, 1024).first;
		ensure_equals(response,
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: keep-alive\r\n"
			"Content-Length: 7\r\n\r\n"
			"hello /"
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: close\r\n"
			"Content-Length: 10\r\n\r\n"
			"hello /foo");
	}

	TEST_METHOD(72) {
		set_test_name("If there is unflushed output data, and keep-alive is not possible, "
			"it disconnects the client after all output data is flushed");

		connectToServer();
		sendRequest(
			"GET /large_response HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n"
			"Size: 1000000\r\n\r\n");
		string response = readAll(fd, 1024 * 1024).first;
		string body = stripHeaders(response);
		ensure(startsWith(response, "HTTP/1.1 200 OK\r\n"));
		ensure_equals(body.size(), 1000000u);
	}

	TEST_METHOD(73) {
		set_test_name("If there is unflushed output data, and keep-alive is possible, "
			"it handles the next request after all output data is flushed");

		connectToServer();
		sendRequest(
			"GET /large_response HTTP/1.1\r\n"
			"Connection: keep-alive\r\n"
			"Host: foo\r\n"
			"Size: 1000000\r\n\r\n"
			"GET /foo HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n\r\n");
		SHOULD_NEVER_HAPPEN(100,
			result = getTotalRequestsBegun() > 1;
		);

		string data = readAll(fd, 1024 * 1024).first;
		string response2 =
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: close\r\n"
			"Content-Length: 10\r\n\r\n"
			"hello /foo";

		string body = stripHeaders(data);
		ensure(startsWith(data, "HTTP/1.1 200 OK\r\n"));
		ensure_equals(body.size(), 1000000u + response2.size());
		ensure_equals(body.substr(1000000), response2);
	}

	TEST_METHOD(74) {
		set_test_name("If a request with body is ended but output is being flushed, "
			"then any received request body data will be discard");

		connectToServer();
		sendRequest(
			"GET /large_response HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n"
			"Size: 1000000\r\n"
			"Content-Length: 4\r\n\r\n");
		EVENTUALLY(1,
			result = getTotalRequestsBegun() == 1;
		);

		unsigned long long previouslyBytesConsumed;
		previouslyBytesConsumed = getTotalBytesConsumed();

		writeExact(fd, "abcd");
		string response = readAll(fd, 1024 * 1024).first;
		string body = stripHeaders(response);
		ensure(startsWith(response, "HTTP/1.1 200 OK\r\n"));
		ensure_equals(body.size(), 1000000u);
		EVENTUALLY(1,
			result = getTotalBytesConsumed() > previouslyBytesConsumed;
		);
		ensure_equals(getBodyBytesRead(), 0u);
	}

	TEST_METHOD(75) {
		set_test_name("If a request with body is ended but output is being flushed, "
			"then it won't attempt to keep-alive the connection after the output is flushed");

		connectToServer();
		sendRequest(
			"GET /large_response HTTP/1.1\r\n"
			"Connection: keep-alive\r\n"
			"Host: foo\r\n"
			"Size: 1000000\r\n"
			"Content-Length: 4\r\n\r\n");
		EVENTUALLY(1,
			result = getTotalRequestsBegun() == 1;
		);

		unsigned long long previouslyBytesConsumed;
		previouslyBytesConsumed = getTotalBytesConsumed();

		writeExact(fd,
			"abcd"
			"GET /foo HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n\r\n");
		string response = readAll(fd, 1024 * 1024).first;
		string body = stripHeaders(response);
		ensure(startsWith(response, "HTTP/1.1 200 OK\r\n"));
		ensure_equals(body.size(), 1000000u);
		EVENTUALLY(1,
			result = getTotalBytesConsumed() > previouslyBytesConsumed;
		);
		ensure_equals(getBodyBytesRead(), 0u);

		SHOULD_NEVER_HAPPEN(100,
			result = getTotalRequestsBegun() > 1;
		);
	}


	/***** Early half-close detection *****/

	TEST_METHOD(80) {
		set_test_name("Detection of half-close after non-empty body fully received");

		connectToServer();
		sendRequest(
			"GET /half_close_test HTTP/1.1\r\n"
			"Connection: keep-alive\r\n"
			"Content-Length: 2\r\n\r\n"
			"hm");
		shutdown(fd, SHUT_WR);
		EVENTUALLY(5,
			result = getHalfCloseDetected() == 1;
		);
	}

	TEST_METHOD(81) {
		set_test_name("Detection of half-close when body is empty");

		connectToServer();
		sendRequest(
			"GET /half_close_test HTTP/1.1\r\n"
			"Connection: keep-alive\r\n\r\n");
		shutdown(fd, SHUT_WR);
		EVENTUALLY(5,
			result = getHalfCloseDetected() == 1;
		);
	}

	TEST_METHOD(82) {
		set_test_name("Detection of half-close after body fully received");

		connectToServer();
		sendRequest(
			"GET /half_close_test HTTP/1.1\r\n"
			"Connection: keep-alive\r\n"
			"Transfer-Encoding: chunked\r\n\r\n"
			"2\r\n"
			"hm\r\n"
			"0\r\n\r\n");
		shutdown(fd, SHUT_WR);
		EVENTUALLY(5,
			result = getHalfCloseDetected() == 1;
		);
	}

	TEST_METHOD(83) {
		set_test_name("Normal data is not detected as early half close");

		connectToServer();
		sendRequest(
			"GET /half_close_test HTTP/1.1\r\n"
			"Connection: keep-alive\r\n\r\n"
			"hm");
		SHOULD_NEVER_HAPPEN(100,
			result = getHalfCloseDetected() > 0;
		);
	}

	TEST_METHOD(84) {
		set_test_name("Request body socket errors that occur after the body"
			" is fully received are processed at the next request");

		connectToServer();
		sendRequest(
			"GET /early_read_error_detection_test HTTP/1.1\r\n"
			"Connection: keep-alive\r\n\r\n"
			"GET / HTTP/1.1\r\n"
			"Connection: close\r\n\r\n");
		EVENTUALLY(5,
			result = getClientDataErrors() == 1;
		);
		ensure_equals(getTotalRequestsBegun(), 1u);
	}


	/***** Shutdown *****/

	TEST_METHOD(90) {
		set_test_name("Upon shutting down the server, no requests will be "
			"eligible for keep-alive");

		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: keep-alive\r\n"
			"Content-Length: 3\r\n\r\n");
		shutdownServer();

		sendRequest("ab\n"
			"GET / HTTP/1.1\r\n"
			"Connection: close\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "Connection: close"));
		ensure("(2)", !containsSubstring(response, "Connection: keep-alive"));
		ensure("(3)", !containsSubstring(response, "hello /"));
	}

	TEST_METHOD(91) {
		set_test_name("Upon shutting down the server, requests for which the "
			"headers are being parsed are not disconnected");

		connectToServer();
		sendRequestAndWait(
			"GET / HTTP/1.1\r\n");
		shutdownServer();
		EVENTUALLY(100,
			result = !hasResponseData();
		);

		sendRequest("\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "Connection: close"));
		ensure("(2)", !containsSubstring(response, "Connection: keep-alive"));
		ensure("(3)", containsSubstring(response, "hello /"));
	}

	TEST_METHOD(92) {
		set_test_name("Upon shutting down the server, requests for which the "
			"headers are being parsed are disconnected when they've been "
			"identified as upgraded requests");

		connectToServer();
		sendRequestAndWait(
			"GET / HTTP/1.1\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: tcp\r\n");
		shutdownServer();
		EVENTUALLY(100,
			result = !hasResponseData();
		);

		sendRequest("\r\n");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "503 Service Unavailable"));
		ensure("(2)", containsSubstring(response, "Connection: close"));
		ensure("(3)", !containsSubstring(response, "Connection: keep-alive"));
	}

	TEST_METHOD(93) {
		set_test_name("Upon shutting down the server, normal requests which "
			"are being processed are not disconnected");

		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Content-Length: 2\r\n\r\n");
		shutdownServer();
		EVENTUALLY(100,
			result = !hasResponseData();
		);

		sendRequest("ab");
		string response = readAll(fd, 1024).first;
		ensure("(1)", containsSubstring(response, "Connection: close"));
		ensure("(2)", !containsSubstring(response, "Connection: keep-alive"));
		ensure("(3)", containsSubstring(response, "2 bytes: ab"));
	}

	TEST_METHOD(94) {
		set_test_name("Upon shutting down the server, upgraded requests which "
			"are being processed are disconnected");

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}

		connectToServer();
		sendRequestAndWait(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: upgrade\r\n"
			"Upgrade: tcp\r\n\r\n");
		shutdownServer();
		EVENTUALLY(5,
			result = hasResponseData();
		);

		string response = readAll(fd, 1024).first;
		ensure_equals(response, "");
	}


	/***** Miscellaneous *****/

	TEST_METHOD(100) {
		set_test_name("It responds with the same HTTP version as the request");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.0\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure_equals(response,
			"HTTP/1.0 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: close\r\n"
			"Content-Length: 7\r\n\r\n"
			"hello /");
	}

	TEST_METHOD(101) {
		set_test_name("For requests without body, keep-alive is possible");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: keep-alive\r\n"
			"Host: foo\r\n\r\n");
		string header = readResponseHeader();
		ensure(containsSubstring(header, "Connection: keep-alive"));
	}

	TEST_METHOD(102) {
		set_test_name("If the request body is fully read, keep-alive is possible");

		connectToServer();
		sendRequest(
			"GET /body_test HTTP/1.1\r\n"
			"Connection: keep-alive\r\n"
			"Host: foo\r\n"
			"Content-Length: 2\r\n\r\n"
			"ok");
		string header = readResponseHeader();
		ensure(containsSubstring(header, "Connection: keep-alive"));
	}

	TEST_METHOD(103) {
		set_test_name("If the request body is not fully read, keep-alive is not possible");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Connection: keep-alive\r\n"
			"Host: foo\r\n"
			"Content-Length: 2\r\n\r\n");
		string header = readResponseHeader();
		ensure(containsSubstring(header, "Connection: close"));
	}

	TEST_METHOD(104) {
		set_test_name("It defaults to not using keep-alive for HTTP <= 1.0 requests");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.0\r\n"
			"Host: foo\r\n\r\n");
		string header = readResponseHeader();
		ensure(containsSubstring(header, "Connection: close"));
	}

	TEST_METHOD(105) {
		set_test_name("It defaults to using keep-alive for HTTP >= 1.1 requests");

		connectToServer();
		sendRequest(
			"GET / HTTP/1.1\r\n"
			"Host: foo\r\n\r\n");
		string header = readResponseHeader();
		ensure(containsSubstring(header, "Connection: keep-alive"));
	}

	TEST_METHOD(106) {
		set_test_name("writeSimpleResponse() doesn't output the body for HEAD requests");

		connectToServer();
		sendRequest(
			"HEAD / HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: foo\r\n\r\n");
		string response = readAll(fd, 1024).first;
		ensure_equals(response,
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Date: Thu, 11 Sep 2014 12:54:09 GMT\r\n"
			"Connection: close\r\n"
			"Content-Length: 7\r\n\r\n");
	}

	TEST_METHOD(107) {
		set_test_name("Client socket write error handling");

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}

		connectToServer();
		sendRequest(
			"GET /large_response HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Size: 1000000\r\n\r\n");
		fd.close();

		EVENTUALLY(5,
			result = getActiveClientCount() == 0;
		);
	}
}
