#include <TestSupport.h>
#include <agents/HelperAgent/RequestHandler.h>
#include <agents/HelperAgent/RequestHandler.cpp>
#include <agents/HelperAgent/AgentOptions.h>
#include <ApplicationPool2/Pool.h>
#include <Utils/json.h>
#include <Utils/IOUtils.h>
#include <Utils/Timer.h>

#include <boost/shared_array.hpp>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cstdarg>
#include <sys/socket.h>

using namespace std;
using namespace boost;
using namespace Passenger;
using namespace Passenger::ApplicationPool2;

namespace tut {
	struct RequestHandlerTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		string serverFilename;
		FileDescriptor requestSocket;
		AgentOptions agentOptions;

		BackgroundEventLoop bg;
		SpawnerFactoryPtr spawnerFactory;
		PoolPtr pool;
		shared_ptr<RequestHandler> handler;
		FileDescriptor connection;
		map<string, string> defaultHeaders;

		string root;
		string rackAppPath, wsgiAppPath;
		
		RequestHandlerTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			spawnerFactory = make_shared<SpawnerFactory>(bg.safe, *resourceLocator, generation);
			pool = make_shared<Pool>(bg.safe.get(), spawnerFactory);
			pool->initialize();
			serverFilename = generation->getPath() + "/server";
			requestSocket = createUnixServer(serverFilename);
			setNonBlocking(requestSocket);
			setLogLevel(LVL_ERROR); // TODO: set to LVL_WARN

			agentOptions.passengerRoot = resourceLocator->getRoot();
			agentOptions.defaultUser   = testConfig["default_user"].asString();
			agentOptions.defaultGroup  = testConfig["default_group"].asString();
			root = resourceLocator->getRoot();
			rackAppPath = root + "/test/stub/rack";
			wsgiAppPath = root + "/test/stub/wsgi";
			defaultHeaders["PASSENGER_LOAD_SHELL_ENVVARS"] = "false";
			defaultHeaders["PASSENGER_APP_TYPE"] = "wsgi";
			defaultHeaders["PASSENGER_SPAWN_METHOD"] = "direct";
			defaultHeaders["REQUEST_METHOD"] = "GET";
		}
		
		~RequestHandlerTest() {
			setLogLevel(DEFAULT_LOG_LEVEL);
			unlink(serverFilename.c_str());
			handler.reset();
			pool->destroy();
			pool.reset();
		}

		void init() {
			handler = make_shared<RequestHandler>(bg.safe, requestSocket, pool, agentOptions);
			bg.start();
		}

		FileDescriptor &connect() {
			connection = connectToUnixServer(serverFilename);
			return connection;
		}

		void sendHeaders(const map<string, string> &headers, ...) {
			va_list ap;
			const char *arg;
			map<string, string>::const_iterator it;
			vector<StaticString> args;

			for (it = headers.begin(); it != headers.end(); it++) {
				args.push_back(StaticString(it->first.data(), it->first.size() + 1));
				args.push_back(StaticString(it->second.data(), it->second.size() + 1));
			}

			va_start(ap, headers);
			while ((arg = va_arg(ap, const char *)) != NULL) {
				args.push_back(StaticString(arg, strlen(arg) + 1));
			}
			va_end(ap);

			shared_array<StaticString> args_array(new StaticString[args.size() + 2]);
			unsigned int totalSize = 0;
			for (unsigned int i = 0; i < args.size(); i++) {
				args_array[i + 1] = args[i];
				totalSize += args[i].size();
			}
			char totalSizeString[10];
			snprintf(totalSizeString, sizeof(totalSizeString), "%u:", totalSize);
			args_array[0] = StaticString(totalSizeString);
			args_array[args.size() + 1] = ",";
			
			gatheredWrite(connection, args_array.get(), args.size() + 2, NULL);
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

		string inspect() {
			string result;
			bg.safe->runSync(boost::bind(&RequestHandlerTest::real_inspect, this, &result));
			return result;
		}

		void real_inspect(string *result) {
			stringstream stream;
			handler->inspect(stream);
			*result = stream.str();
		}

		static void writeBody(FileDescriptor conn, string body) {
			try {
				writeExact(conn, body);
			} catch (const SystemException &e) {
				if (e.code() == EPIPE) {
					// Ignore.
				} else {
					throw;
				}
			}
		}
	};

	DEFINE_TEST_GROUP(RequestHandlerTest);

	TEST_METHOD(1) {
		// Test one normal request.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		string body = stripHeaders(response);
		ensure("Status line is correct", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("Headers are correct", containsSubstring(response, "Content-Type: text/html\r\n"));
		ensure("Contains a Status header", containsSubstring(response, "Status: 200 OK\r\n"));
		ensure_equals(body, "hello <b>world</b>");
	}

	TEST_METHOD(2) {
		// Test multiple normal requests.
		init();
		for (int i = 0; i < 10; i++) {
			connect();
			sendHeaders(defaultHeaders,
				"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
				"PATH_INFO", "/",
				NULL);
			string response = readAll(connection);
			string body = stripHeaders(response);
			ensure("Status line is correct", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
			ensure("Headers are correct", containsSubstring(response, "Content-Type: text/html\r\n"));
			ensure("Contains a Status header", containsSubstring(response, "Status: 200 OK\r\n"));
			ensure_equals(body, "hello <b>world</b>");
		}
	}

	TEST_METHOD(3) {
		// Test sending request data in pieces.
		defaultHeaders["PASSENGER_APP_ROOT"] = wsgiAppPath;
		defaultHeaders["PATH_INFO"] = "/";

		string request;
		map<string, string>::const_iterator it, end = defaultHeaders.end();
		for (it = defaultHeaders.begin(); it != end; it++) {
			request.append(it->first);
			request.append(1, '\0');
			request.append(it->second);
			request.append(1, '\0');
		}
		request = toString(request.size()) + ":" + request;
		request.append(",");

		init();
		connect();
		string::size_type i = 0;
		while (i < request.size()) {
			const string piece = const_cast<const string &>(request).substr(i, 5);
			writeExact(connection, piece);
			usleep(10000);
			i += piece.size();
		}

		string response = readAll(connection);
		string body = stripHeaders(response);
		ensure("Status line is correct", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("Headers are correct", containsSubstring(response, "Content-Type: text/html\r\n"));
		ensure("Contains a Status header", containsSubstring(response, "Status: 200 OK\r\n"));
		ensure_equals(body, "hello <b>world</b>");
	}

	TEST_METHOD(4) {
		// It denies access if the connect password is wrong.
		agentOptions.requestSocketPassword = "hello world";
		setLogLevel(-1);
		init();

		connect();
		writeExact(connection, "hello world");
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/",
			NULL
		);
		ensure(containsSubstring(readAll(connection), "hello <b>world</b>"));

		connect();
		try {
			sendHeaders(defaultHeaders,
				"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
				"PATH_INFO", "/",
				NULL
			);
		} catch (const SystemException &e) {
			ensure_equals(e.code(), EPIPE);
			return;
		}
		string response;
		try {
			response = readAll(connection);
		} catch (const SystemException &e) {
			ensure_equals(e.code(), ECONNRESET);
			return;
		}
		ensure_equals(response, "");
	}

	TEST_METHOD(5) {
		// It disconnects us if the connect password is not sent within a certain time.
		agentOptions.requestSocketPassword = "hello world";
		setLogLevel(-1);
		handler = make_shared<RequestHandler>(bg.safe, requestSocket, pool, agentOptions);
		handler->connectPasswordTimeout = 40;
		bg.start();

		connect();
		Timer timer;
		readAll(connection);
		timer.stop();
		ensure(timer.elapsed() <= 60);
	}

	TEST_METHOD(6) {
		// It works correct if the connect password is sent in pieces.
		agentOptions.requestSocketPassword = "hello world";
		init();
		connect();
		writeExact(connection, "hello");
		usleep(10000);
		writeExact(connection, " world");
		usleep(10000);
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/",
			NULL
		);
		ensure(containsSubstring(readAll(connection), "hello <b>world</b>"));
	}

	TEST_METHOD(7) {
		// It closes the connection with the application if the client has closed the connection.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/stream",
			NULL
		);
		BufferedIO io(connection);
		ensure_equals(io.readLine(), "HTTP/1.1 200 OK\r\n");
		ProcessPtr process;
		{
			LockGuard l(pool->syncher);
			ensure_equals(pool->getProcessCount(false), 1u);
			SuperGroupPtr superGroup = pool->superGroups.get(wsgiAppPath);
			process = superGroup->defaultGroup->enabledProcesses.front();
			ensure_equals(process->sessions, 1);
		}
		connection.close();
		EVENTUALLY(5,
			LockGuard l(pool->syncher);
			result = process->sessions == 0;
		);
	}
	
	TEST_METHOD(10) {
		// If the app crashes at startup without an error page then it renders
		// a generic error page.
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'I have failed'");

		setLogLevel(-2);
		spawnerFactory->getConfig()->forwardStderr = false;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\t" + root + "/test/tmp.handler/start.rb").c_str(),
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Status: 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "I have failed"));
	}

	TEST_METHOD(11) {
		// If the app crashes at startup with an error page then it renders
		// a friendly error page.
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'Error'\n"
			"STDERR.puts\n"
			"STDERR.puts 'I have failed'\n");

		setLogLevel(-2);
		spawnerFactory->getConfig()->forwardStderr = false;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\t" + root + "/test/tmp.handler/start.rb").c_str(),
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Status: 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Content-Type: text/html; charset=UTF-8\r\n"));
		ensure(containsSubstring(response, "<html>"));
		ensure(containsSubstring(response, "I have failed"));
	}

	TEST_METHOD(12) {
		// If spawning fails because of an internal error then it reports the error appropriately.
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb", "");

		setLogLevel(-2);
		spawnerFactory->getConfig()->forwardStderr = false;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\t" + root + "/test/tmp.handler/start.rb").c_str(),
			"PASSENGER_RAISE_INTERNAL_ERROR", "true",
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Status: 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Content-Type: text/html; charset=UTF-8\r\n"));
		ensure(containsSubstring(response, "<html>"));
		ensure(containsSubstring(response, "An internal error occurred while trying to spawn the application."));
		ensure(containsSubstring(response, "Passenger:<wbr>:<wbr>RuntimeException"));
		ensure(containsSubstring(response, "An internal error!"));
		ensure(containsSubstring(response, "Spawner.h"));
	}

	TEST_METHOD(13) {
		// Error pages respect the PASSENGER_STATUS_LINE option.
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'I have failed'");

		setLogLevel(-2);
		spawnerFactory->getConfig()->forwardStderr = false;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\t" + root + "/test/tmp.handler/start.rb").c_str(),
			"PASSENGER_STATUS_LINE", "false",
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(!containsSubstring(response, "HTTP/1.1 "));
		ensure(containsSubstring(response, "Status: 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "I have failed"));
	}

	TEST_METHOD(14) {
		// If PASSENGER_FRIENDLY_ERROR_PAGES is false then it does not render
		// a friendly error page.
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'Error'\n"
			"STDERR.puts\n"
			"STDERR.puts 'I have failed'\n");

		setLogLevel(-2);
		spawnerFactory->getConfig()->forwardStderr = false;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\t" + root + "/test/tmp.handler/start.rb").c_str(),
			"PASSENGER_FRIENDLY_ERROR_PAGES", "false",
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Status: 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Content-Type: text/html; charset=UTF-8\r\n"));
		ensure(containsSubstring(response, "<html>"));
		ensure(!containsSubstring(response, "I have failed"));
		ensure(containsSubstring(response, "We're sorry, but something went wrong"));
	}

	TEST_METHOD(20) {
		// It streams the request body to the application.
		DeleteFileEventually file("tmp.output");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/upload",
			"HTTP_X_OUTPUT", (root + "/test/tmp.output").c_str(),
			NULL);
		writeExact(connection, "hello\n");
		EVENTUALLY(5,
			result = fileExists("tmp.output") && readAll("tmp.output") == "hello\n";
		);
		writeExact(connection, "world\n");
		EVENTUALLY(3,
			result = readAll("tmp.output") == "hello\nworld\n";
		);
		shutdown(connection, SHUT_WR);
		ensure_equals(stripHeaders(readAll(connection)), "ok");
	}

	TEST_METHOD(21) {
		// It buffers the request body if PASSENGER_BUFFERING is true.
		DeleteFileEventually file("tmp.output");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PASSENGER_BUFFERING", "true",
			"PATH_INFO", "/upload",
			"HTTP_X_OUTPUT", (root + "/test/tmp.output").c_str(),
			NULL);
		writeExact(connection, "hello\n");
		SHOULD_NEVER_HAPPEN(200,
			result = fileExists("tmp.output");
		);
		writeExact(connection, "world\n");
		SHOULD_NEVER_HAPPEN(200,
			result = fileExists("tmp.output");
		);
		shutdown(connection, SHUT_WR);
		ensure_equals(stripHeaders(readAll(connection)), "ok");
	}

	TEST_METHOD(22) {
		set_test_name("Test buffering of large request bodies that fit in neither the socket "
		              "buffer nor the FileBackedPipe memory buffer, and that the application "
		              "cannot read quickly enough.");

		DeleteFileEventually d1("/tmp/wait.txt");
		DeleteFileEventually d2("/tmp/output.txt");

		// 2.6 MB of request body. Guaranteed not to fit in any socket buffer.
		string requestBody;
		for (int i = 0; i < 204800; i++) {
			requestBody.append("hello world!\n");
		}

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/upload",
			"PASSENGER_BUFFERING", "true",
			"HTTP_X_WAIT_FOR_FILE", "/tmp/wait.txt",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		
		// Should not block.
		writeExact(connection, requestBody);
		shutdown(connection, SHUT_WR);
		
		EVENTUALLY(5,
			result = containsSubstring(inspect(), "session initiated           = true");
		);
		touchFile("/tmp/wait.txt");

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) requestBody.size());
	}

	TEST_METHOD(30) {
		// It replaces HTTP_CONTENT_LENGTH with CONTENT_LENGTH.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/env",
			"HTTP_CONTENT_LENGTH", "5",
			NULL);
		writeExact(connection, "hello");
		string response = readAll(connection);
		ensure(containsSubstring(response, "CONTENT_LENGTH = 5\n"));
		ensure(!containsSubstring(response, "HTTP_CONTENT_LENGTH"));
	}
	
	TEST_METHOD(31) {
		// It replaces HTTP_CONTENT_TYPE with CONTENT_TYPE.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/env",
			"HTTP_CONTENT_TYPE", "application/json",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "CONTENT_TYPE = application/json\n"));
		ensure(!containsSubstring(response, "HTTP_CONTENT_TYPE"));
	}

	TEST_METHOD(35) {
		// The response doesn't contain an HTTP status line if PASSENGER_STATUS_LINE is false.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PASSENGER_STATUS_LINE", "false",
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(!containsSubstring(response, "HTTP/1.1 "));
		ensure(containsSubstring(response, "Status: 200 OK\r\n"));
	}

	TEST_METHOD(36) {
		// If the application outputs a status line without a reason phrase,
		// then a reason phrase is automatically appended.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/custom_status",
			"HTTP_X_CUSTOM_STATUS", "201",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 201 Created\r\n"));
		ensure(containsSubstring(response, "Status: 201 Created\r\n"));
	}

	TEST_METHOD(37) {
		// If the application outputs a status line with a custom reason phrase,
		// then that reason phrase is used.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/custom_status",
			"HTTP_X_CUSTOM_STATUS", "201 Bunnies Jump",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 201 Bunnies Jump\r\n"));
		ensure(containsSubstring(response, "Status: 201 Bunnies Jump\r\n"));
	}
	
	TEST_METHOD(38) {
		// If the application doesn't output a status line then it rejects the application response.
		// TODO
	}

	TEST_METHOD(39) {
		// Test handling of slow clients that can't receive response data fast enough (response buffering).
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/blob",
			"HTTP_X_SIZE", "10485760",
			NULL);
		EVENTUALLY(10,
			result = containsSubstring(inspect(), "appInput reachedEnd         = true");
		);
		string result = stripHeaders(readAll(connection));
		ensure_equals(result.size(), 10485760u);
		const char *data = result.data();
		const char *end  = result.data() + result.size();
		while (data < end) {
			ensure_equals(*data, 'x');
			data++;
		}
	}

	TEST_METHOD(40) {
		set_test_name("Test that RequestHandler does not read more than CONTENT_LENGTH bytes "
		              "from the client body (when buffering is on and request body is large).");

		DeleteFileEventually d("/tmp/output.txt");

		// 2.6 MB of request body. Guaranteed not to fit in any socket buffer.
		string requestBody;
		for (int i = 0; i < 204800; i++) {
			requestBody.append("hello world!\n");
		}

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/upload",
			"CONTENT_LENGTH", toString(requestBody.size()).c_str(),
			"PASSENGER_BUFFERING", "true",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, requestBody);

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) requestBody.size());
	}

	TEST_METHOD(41) {
		set_test_name("Test that RequestHandler does not read more than CONTENT_LENGTH bytes "
		              "from the client body (when buffering is on and request body is small).");

		DeleteFileEventually d("/tmp/output.txt");
		string requestBody = "hello world";

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/upload",
			"CONTENT_LENGTH", toString(requestBody.size()).c_str(),
			"PASSENGER_BUFFERING", "true",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, requestBody);

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) requestBody.size());
	}

	TEST_METHOD(42) {
		set_test_name("Test that RequestHandler does not read more than CONTENT_LENGTH bytes "
		              "from the client body (when buffering is off and request body is large).");

		DeleteFileEventually d("/tmp/output.txt");

		// 2 MB of request body. Guaranteed not to fit in any socket buffer.
		string requestBody;
		for (int i = 0; i < 102400; i++) {
			char buf[100];
			snprintf(buf, sizeof(buf), "%06d: hello world!\n", i);
			requestBody.append(buf);
		}

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/upload",
			"CONTENT_LENGTH", toString(requestBody.size()).c_str(),
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);

		TempThread thr(boost::bind(RequestHandlerTest::writeBody, connection, requestBody));

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) requestBody.size());
	}

	TEST_METHOD(43) {
		set_test_name("Test that RequestHandler does not read more than CONTENT_LENGTH bytes "
		              "from the client body (when buffering is off and request body is small).");

		DeleteFileEventually d("/tmp/output.txt");
		string requestBody = "hello world";

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/upload",
			"CONTENT_LENGTH", toString(requestBody.size()).c_str(),
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);

		TempThread thr(boost::bind(RequestHandlerTest::writeBody, connection, requestBody));

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) requestBody.size());
	}

	TEST_METHOD(44) {
		set_test_name("Test that RequestHandler does not pass any client body data when CONTENT_LENGTH == 0 (when buffering is on).");

		DeleteFileEventually d("/tmp/output.txt");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/upload",
			"CONTENT_LENGTH", "0",
			"PASSENGER_BUFFERING", "true",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, "hello world");

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) 0);
	}

	TEST_METHOD(45) {
		set_test_name("Test that RequestHandler does not pass any client body data when CONTENT_LENGTH == 0 (when buffering is off).");

		DeleteFileEventually d("/tmp/output.txt");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/upload",
			"CONTENT_LENGTH", "0",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, "hello world");

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) 0);
	}

	TEST_METHOD(46) {
		// If the application outputs a request oobw header, handler should remove the header, mark
		// the process as oobw requested. The process should continue to process requests until the
		// spawner spawns another process (to avoid the group being empty). As soon as the new 
		// process is spawned, the original process will make the oobw request. Afterwards, the 
		// original process is re-enabled.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/oobw",
			NULL);
		string response = readAll(connection);
		ensure("status is not 200", containsSubstring(response, "Status: 200 OK\r\n"));
		ensure("contains oowb header", !containsSubstring(response, "X-Passenger-Request-OOB-Work:"));
		pid_t origPid = atoi(stripHeaders(response));
		
		// Get a reference to the orignal process and verify oobw has been requested.
		ProcessPtr origProcess;
		{
			LockGuard l(pool->syncher);
			origProcess = pool->superGroups.get(wsgiAppPath)->defaultGroup->disablingProcesses.front();
			ensure("OOBW requested", origProcess->oobwStatus == Process::OOBW_IN_PROGRESS);
		}
		ensure("sanity check", origPid == origProcess->pid); // just a sanity check
		
		// Issue requests until the new process handles it.
		pid_t pid;
		EVENTUALLY(2,
			connect();
			sendHeaders(defaultHeaders,
				"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
				"PATH_INFO", "/pid",
				NULL);
			string response = readAll(connection);
			ensure("status is 200", containsSubstring(response, "Status: 200 OK\r\n"));
			pid = atoi(stripHeaders(response));
			result = (pid != origPid);
		);
		
		// Wait for the original process to finish oobw request.
		EVENTUALLY(2,
			unique_lock<boost::mutex> lock(pool->syncher);
			result = origProcess->oobwStatus == Process::OOBW_NOT_ACTIVE;
		);
		
		// Final asserts.
		{
			unique_lock<boost::mutex> lock(pool->syncher);
			ensure_equals("2 enabled processes", pool->superGroups.get(wsgiAppPath)->defaultGroup->enabledProcesses.size(), 2u);
			ensure_equals("oobw is reset", origProcess->oobwStatus, Process::OOBW_NOT_ACTIVE);
			ensure_equals("process is enabled", origProcess->enabled, Process::ENABLED);
		}
	}

	TEST_METHOD(47) {
		set_test_name("The RequestHandler should append a Date header if the app doesn't output one.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/pid",
			NULL);

		string result = readAll(connection);
		ensure(result.find("Date: ") != string::npos);
	}

	// Test small response buffering.
	// Test large response buffering.
}
