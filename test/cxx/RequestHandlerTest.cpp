#include <TestSupport.h>
#include <agents/HelperAgent/RequestHandler.h>
#include <agents/HelperAgent/RequestHandler.cpp>
#include <agents/HelperAgent/AgentOptions.h>
#include <ApplicationPool2/Pool.h>
#include <Utils/json.h>
#include <Utils/IOUtils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/Timer.h>
#include <Utils/BufferedIO.h>

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
		Pool::DebugSupportPtr debug;
		boost::shared_ptr<RequestHandler> handler;
		FileDescriptor connection;
		map<string, string> defaultHeaders;

		string root;
		string rackAppPath, wsgiAppPath;
		
		RequestHandlerTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			spawnerFactory = boost::make_shared<SpawnerFactory>(generation,
				make_shared<SpawnerConfig>(*resourceLocator));
			pool = boost::make_shared<Pool>(spawnerFactory);
			pool->initialize();
			serverFilename = generation->getPath() + "/server";
			requestSocket = createUnixServer(serverFilename);
			setNonBlocking(requestSocket);
			setLogLevel(LVL_ERROR); // TODO: set to LVL_WARN
			setPrintAppOutputAsDebuggingMessages(true);

			agentOptions.passengerRoot = resourceLocator->getRoot();
			agentOptions.defaultRubyCommand = DEFAULT_RUBY;
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
			setPrintAppOutputAsDebuggingMessages(false);
			if (bg.isStarted()) {
				bg.safe->runSync(boost::bind(&RequestHandlerTest::destroy, this));
			} else {
				destroy();
			}
			unlink(serverFilename.c_str());
		}

		void init() {
			handler = boost::make_shared<RequestHandler>(bg.safe, requestSocket, pool, agentOptions);
			bg.start();
		}

		void destroy() {
			handler.reset();
			pool->destroy();
			pool.reset();
			ev_break(bg.loop, EVBREAK_ALL);
		}

		void initPoolDebugging() {
			pool->initDebugging();
			debug = pool->debugSupport;
		}

		FileDescriptor &connect() {
			connection = connectToUnixServer(serverFilename);
			return connection;
		}

		void sendHeaders(const map<string, string> &headers, const char *header1,
			const char *value1, ...)
		{
			va_list ap;
			const char *arg;
			map<string, string> finalHeaders;
			map<string, string>::const_iterator it;
			vector<StaticString> args;
			unsigned int totalSize = 0;

			for (it = headers.begin(); it != headers.end(); it++) {
				string key = string(it->first.data(), it->first.size() + 1);
				string value = string(it->second.data(), it->second.size() + 1);
				finalHeaders[key] = value;
			}

			finalHeaders[makeStaticStringWithNull(header1)] =
				makeStaticStringWithNull(value1);

			va_start(ap, value1);
			while ((arg = va_arg(ap, const char *)) != NULL) {
				string key(arg, strlen(arg) + 1);
				arg = va_arg(ap, const char *);
				string value(arg, strlen(arg) + 1);
				finalHeaders[key] = value;
			}
			va_end(ap);

			for (it = finalHeaders.begin(); it != finalHeaders.end(); it++) {
				args.push_back(it->first);
				args.push_back(it->second);
				totalSize += it->first.size();
				totalSize += it->second.size();
			}

			char totalSizeString[10];
			snprintf(totalSizeString, sizeof(totalSizeString), "%u:", totalSize);
			args.insert(args.begin(), StaticString(totalSizeString));
			args.push_back(",");

			gatheredWrite(connection, &args[0], args.size(), NULL);
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

	DEFINE_TEST_GROUP_WITH_LIMIT(RequestHandlerTest, 80);


	/***** Basic tests *****/

	TEST_METHOD(1) {
		set_test_name("A request is forwarded to the app process, and its response is forwarded back.");
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		string body = stripHeaders(response);
		ensure("Status line is correct", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("Headers are correct", containsSubstring(response, "Content-Type: text/plain\r\n"));
		ensure("Contains a Status header", containsSubstring(response, "Status: 200 OK\r\n"));
		ensure_equals(body, "front page");
	}

	TEST_METHOD(2) {
		set_test_name("It can handle multiple requests in serial.");
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
			ensure("Headers are correct", containsSubstring(response, "Content-Type: text/plain\r\n"));
			ensure("Contains a Status header", containsSubstring(response, "Status: 200 OK\r\n"));
			ensure_equals(body, "front page");
		}
	}

	TEST_METHOD(3) {
		set_test_name("It can handle request data that is sent piece-wise.");
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
		ensure("Headers are correct", containsSubstring(response, "Content-Type: text/plain\r\n"));
		ensure("Contains a Status header", containsSubstring(response, "Status: 200 OK\r\n"));
		ensure_equals(body, "front page");
	}

	TEST_METHOD(4) {
		set_test_name("It closes the connection with the application if the client has closed the connection.");
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


	/***** Connect password tests *****/

	TEST_METHOD(5) {
		set_test_name("It denies access if the connect password is wrong.");
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
		ensure(containsSubstring(readAll(connection), "front page"));

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

	TEST_METHOD(6) {
		set_test_name("It disconnects the client if the connect password is not sent within a certain time.");
		agentOptions.requestSocketPassword = "hello world";
		setLogLevel(-1);
		handler = boost::make_shared<RequestHandler>(bg.safe, requestSocket, pool, agentOptions);
		handler->connectPasswordTimeout = 40;
		bg.start();

		connect();
		Timer timer;
		readAll(connection);
		timer.stop();
		ensure(timer.elapsed() <= 60);
	}

	TEST_METHOD(7) {
		set_test_name("It works correctly if the connect password is sent piece-wise.");
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
		ensure(containsSubstring(readAll(connection), "front page"));
	}


	/***** Error page tests *****/

	TEST_METHOD(10) {
		set_test_name("If the app crashes at startup without an error page, "
			"and friendly error pages are turned on, then it renders a generic error page.");
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'I have failed'");

		setLogLevel(-2);
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\t" + root + "/test/tmp.handler/start.rb").c_str(),
			"PASSENGER_FRIENDLY_ERROR_PAGES", "true",
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Status: 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "I have failed"));
	}

	TEST_METHOD(11) {
		set_test_name("If the app crashes at startup with an error page, "
			"and friendly error pages are turned on, then it renders a friendly error page.");
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'Error'\n"
			"STDERR.puts\n"
			"STDERR.puts 'I have failed'\n");

		setLogLevel(-2);
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\t" + root + "/test/tmp.handler/start.rb").c_str(),
			"PASSENGER_FRIENDLY_ERROR_PAGES", "true",
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
		set_test_name("If spawning fails because of an internal error, "
			"and friendly error pages are on, then it reports the error appropriately.");
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb", "");

		setLogLevel(-2);
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\t" + root + "/test/tmp.handler/start.rb").c_str(),
			"PASSENGER_FRIENDLY_ERROR_PAGES", "true",
			"PASSENGER_RAISE_INTERNAL_ERROR", "true",
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure("(1)", containsSubstring(response, "HTTP/1.1 500 Internal Server Error\r\n"));
		ensure("(2)", containsSubstring(response, "Status: 500 Internal Server Error\r\n"));
		ensure("(3)", containsSubstring(response, "Content-Type: text/html; charset=UTF-8\r\n"));
		ensure("(4)", containsSubstring(response, "<html>"));
		ensure("(5)", containsSubstring(response, "An internal error occurred while trying to spawn the application."));
		ensure("(6)", containsSubstring(response, "RuntimeException"));
		ensure("(7)", containsSubstring(response, "An internal error!"));
		ensure("(8)", containsSubstring(response, "Spawner.h"));
	}

	TEST_METHOD(13) {
		set_test_name("Error pages respect the PASSENGER_STATUS_LINE option.");
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'I have failed'");

		setLogLevel(-2);
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\t" + root + "/test/tmp.handler/start.rb").c_str(),
			"PASSENGER_FRIENDLY_ERROR_PAGES", "true",
			"PASSENGER_STATUS_LINE", "false",
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(!containsSubstring(response, "HTTP/1.1 "));
		ensure(containsSubstring(response, "Status: 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "I have failed"));
	}

	TEST_METHOD(14) {
		set_test_name("If PASSENGER_FRIENDLY_ERROR_PAGES is false then it does not render a friendly error page.");
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'Error'\n"
			"STDERR.puts\n"
			"STDERR.puts 'I have failed'\n");

		setLogLevel(-2);
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


	/***** Buffering tests *****/

	TEST_METHOD(21) {
		set_test_name("If PASSENGER_BUFFERING is true, and Content-Length is given, it buffers the request body.");
		DeleteFileEventually file("tmp.output");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PASSENGER_BUFFERING", "true",
			"REQUEST_METHOD", "POST",
			"PATH_INFO", "/raw_upload_to_file",
			"CONTENT_LENGTH", "12",
			"HTTP_X_OUTPUT", (root + "/test/tmp.output").c_str(),
			NULL);
		writeExact(connection, "hello\n");
		SHOULD_NEVER_HAPPEN(200,
			result = fileExists("tmp.output");
		);
		writeExact(connection, "world\n");
		EVENTUALLY(1,
			result = fileExists("tmp.output");
		);
		ensure_equals(stripHeaders(readAll(connection)), "ok");
	}

	TEST_METHOD(22) {
		set_test_name("If PASSENGER_BUFFERING is true, and Transfer-Encoding is given, it buffers the request body.");
		DeleteFileEventually file("tmp.output");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PASSENGER_BUFFERING", "true",
			"REQUEST_METHOD", "POST",
			"PATH_INFO", "/raw_upload_to_file",
			"HTTP_TRANSFER_ENCODING", "chunked",
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

	TEST_METHOD(24) {
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
			"REQUEST_METHOD", "POST",
			"PATH_INFO", "/raw_upload_to_file",
			"PASSENGER_BUFFERING", "true",
			"CONTENT_LENGTH", toString(requestBody.size()).c_str(),
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

	TEST_METHOD(25) {
		set_test_name("Test handling of slow clients that can't receive response data fast enough (response buffering).");
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


	/***** Header handling tests *****/

	TEST_METHOD(26) {
		set_test_name("It replaces HTTP_CONTENT_LENGTH with CONTENT_LENGTH.");
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"REQUEST_METHOD", "POST",
			"PATH_INFO", "/env",
			"HTTP_CONTENT_LENGTH", "5",
			NULL);
		writeExact(connection, "hello");
		string response = readAll(connection);
		ensure(containsSubstring(response, "CONTENT_LENGTH = 5\n"));
		ensure(!containsSubstring(response, "HTTP_CONTENT_LENGTH"));
	}
	
	TEST_METHOD(27) {
		set_test_name("It replaces HTTP_CONTENT_TYPE with CONTENT_TYPE.");
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

	TEST_METHOD(28) {
		set_test_name("The response doesn't contain an HTTP status line if PASSENGER_STATUS_LINE is false.");
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

	TEST_METHOD(29) {
		set_test_name("If the application outputs a status line without a reason phrase, then a reason phrase is automatically appended.");
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

	TEST_METHOD(30) {
		set_test_name("If the application outputs a status line with a custom reason phrase, then that reason phrase is used.");
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

	TEST_METHOD(31) {
		set_test_name("It appends a Date header if the app doesn't output one.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/pid",
			NULL);

		string result = readAll(connection);
		ensure(result.find("Date: ") != string::npos);
	}

	TEST_METHOD(32) {
		set_test_name("It rejects non-GET, non-HEAD requests with an Upgrade header.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/",
			"REQUEST_METHOD", "POST",
			"HTTP_UPGRADE", "WebSocket",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 400 Bad Request"));
	}

	TEST_METHOD(33) {
		set_test_name("It accepts GET/HEAD requests with a Content-Length header.");

		DeleteFileEventually d("/tmp/output.txt");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "GET",
			"CONTENT_LENGTH", "2",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, "hi");

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		ensure_equals(readAll("/tmp/output.txt"), "hi");

		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "HEAD",
			"CONTENT_LENGTH", "2",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, "ho");

		result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		ensure_equals(readAll("/tmp/output.txt"), "ho");
	}

	TEST_METHOD(34) {
		set_test_name("It rejects GET/HEAD requests with a Transfer-Encoding header.");

		DeleteFileEventually d("/tmp/output.txt");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "GET",
			"HTTP_TRANSFER_ENCODING", "chunked",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, "hi");
		shutdown(connection, SHUT_WR);

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		ensure_equals(readAll("/tmp/output.txt"), "hi");

		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "HEAD",
			"HTTP_TRANSFER_ENCODING", "chunked",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, "ho");
		shutdown(connection, SHUT_WR);

		result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		ensure_equals(readAll("/tmp/output.txt"), "ho");
	}
	

	/***** Advanced connection handling tests *****/

	TEST_METHOD(40) {
		set_test_name("It streams the request body to the application.");
		DeleteFileEventually file("tmp.output");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"REQUEST_METHOD", "POST",
			"PATH_INFO", "/raw_upload_to_file",
			"HTTP_TRANSFER_ENCODING", "chunked",
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

	TEST_METHOD(41) {
		set_test_name("If no Content-Length and no Transfer-Encoding are given, and buffering is on:  "
			"it does not pass any request body data.");
		
		DeleteFileEventually d("/tmp/output.txt");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "POST",
			"PASSENGER_BUFFERING", "true",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, "hello\n");

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) 0);
	}

	TEST_METHOD(42) {
		set_test_name("If no Content-Length and no Transfer-Encoding are given, and buffering is off: "
			"it does not pass any request body data.");
		
		DeleteFileEventually d("/tmp/output.txt");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "POST",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, "hello\n");

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) 0);
	}

	TEST_METHOD(43) {
		set_test_name("If Upgrade is given, it keeps passing the request body until end of stream.");

		DeleteFileEventually d("/tmp/output.txt");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/raw_upload_to_file",
			"HTTP_UPGRADE", "websocket",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, "hello\n");
		shutdown(connection, SHUT_WR);

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) 6);
	}

	TEST_METHOD(45) {
		set_test_name("If Content-Length is given, buffering is on, and request body is large:  "
			"it passes Content-Length bytes of the request body.");

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
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "POST",
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

	TEST_METHOD(46) {
		set_test_name("If Content-Length is given, buffering is on, and request body is small:  "
			"it passes Content-Length bytes of the request body.");

		DeleteFileEventually d("/tmp/output.txt");
		string requestBody = "hello world";

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "POST",
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

	TEST_METHOD(47) {
		set_test_name("If Content-Length is given, buffering is off, and request body is large: "
			"it passes Content-Length bytes of the request body.");

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
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "POST",
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

	TEST_METHOD(48) {
		set_test_name("If Content-Length is given, buffering is off, and request body is small: "
			"it passes Content-Length bytes of the request body.");

		DeleteFileEventually d("/tmp/output.txt");
		string requestBody = "hello world";

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "POST",
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

	TEST_METHOD(49) {
		set_test_name("If Transfer-Encoding is given and buffering is on:  "
			"it keeps passing the request body until end of stream.");

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
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "POST",
			"PASSENGER_BUFFERING", "true",
			"HTTP_TRANSFER_ENCODING", "chunked",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, requestBody);
		shutdown(connection, SHUT_WR);

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) requestBody.size());
	}

	TEST_METHOD(50) {
		set_test_name("If Transfer-Encoding is given and buffering is off: "
			"it keeps passing the request body until end of stream.");

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
			"PATH_INFO", "/raw_upload_to_file",
			"REQUEST_METHOD", "POST",
			"HTTP_TRANSFER_ENCODING", "chunked",
			"HTTP_X_OUTPUT", "/tmp/output.txt",
			NULL);
		writeExact(connection, requestBody);
		shutdown(connection, SHUT_WR);

		string result = stripHeaders(readAll(connection));
		ensure_equals(result, "ok");
		struct stat buf;
		ensure(stat("/tmp/output.txt", &buf) == 0);
		ensure_equals(buf.st_size, (off_t) requestBody.size());
	}

	TEST_METHOD(51) {
		set_test_name("If Transfer-Encoding is given and the application socket uses the HTTP protocol, "
			"rechunk the body when forwarding it to the application.");

		fprintf(stderr, "TODO: implement test 51\n");
	}

	TEST_METHOD(54) {
		set_test_name("It writes an appropriate response if the request queue is overflown.");

		initPoolDebugging();
		debug->restarting = false;
		debug->spawning = false;
		debug->testOverflowRequestQueue = true;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(response.find("Status: 503 Service Unavailable") != string::npos);
		ensure(response.find("This website is under heavy load") != string::npos);
	}

	TEST_METHOD(55) {
		set_test_name("It uses the status code dictated by PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE "
			"if the request queue is overflown");

		initPoolDebugging();
		debug->restarting = false;
		debug->spawning = false;
		debug->testOverflowRequestQueue = true;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/",
			"PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE", "504",
			NULL);
		string response = readAll(connection);
		ensure(response.find("Status: 504 Gateway Timeout") != string::npos);
		ensure(response.find("This website is under heavy load") != string::npos);
	}

	TEST_METHOD(56) {
		set_test_name("PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE should work even if it is an unknown code");

		initPoolDebugging();
		debug->restarting = false;
		debug->spawning = false;
		debug->testOverflowRequestQueue = true;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/",
			"PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE", "604",
			NULL);
		string response = readAll(connection);
		ensure(response.find("Status: 604 Unknown Reason-Phrase") != string::npos);
		ensure(response.find("This website is under heavy load") != string::npos);
	}

	TEST_METHOD(57) {
		set_test_name("It relieves the application process after having read its entire response data.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/blob",
			NULL);
		vector<ProcessPtr> processes;
		EVENTUALLY(5,
			processes = pool->getProcesses();
			result = processes.size() == 1;
		);
		EVENTUALLY(5,
			LockGuard l(pool->syncher);
			result = processes[0]->processed == 1;
		);
		{
			LockGuard l(pool->syncher);
			ensure_equals("The session is closed before the client is done reading",
				processes[0]->sessions, 0);
		}
	}

	TEST_METHOD(58) {
		set_test_name("It supports responses in chunked transfer encoding.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/chunked_stream",
			NULL
		);
		
		char buf[1024 * 10];
		unsigned long long timeout = 500000;
		unsigned int size;
		try {
			size = readExact(connection, buf, sizeof(buf), &timeout);
		} catch (const TimeoutException &) {
			fail("RequestHandler did not correctly handle chunked EOF!");
		}

		string response(buf, size);
		string body = stripHeaders(response);
		ensure(containsSubstring(response, "Counter: 0\n"));
		ensure(containsSubstring(response, "Counter: 1\n"));
		ensure(containsSubstring(response, "Counter: 2\n"));
	}

	TEST_METHOD(59) {
		set_test_name("It supports switching protocols when communicating over application session sockets.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/switch_protocol",
			"HTTP_UPGRADE", "raw",
			"HTTP_CONNECTION", "Upgrade",
			NULL
		);

		BufferedIO io(connection);
		string header;
		bool done = false;

		ensure_equals(io.readLine(), "HTTP/1.1 101 Switching Protocols\r\n");

		do {
			string line = io.readLine();
			done = line.empty() || line == "\r\n";
			if (!done) {
				header.append(line);
			}
		} while (!done);

		ensure("(1)", containsSubstring(header, "Upgrade: raw\r\n"));
		ensure("(2)", containsSubstring(header, "Connection: Upgrade\r\n"));

		writeExact(connection, "hello\n");
		ensure_equals(io.readLine(), "Echo: hello\n");
	}

	TEST_METHOD(60) {
		set_test_name("It supports switching protocols when communication over application http_session sockets.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"_PASSENGER_FORCE_HTTP_SESSION", "true",
			"PASSENGER_APP_ROOT", rackAppPath.c_str(),
			"PASSENGER_APP_TYPE", "rack",
			"REQUEST_URI", "/switch_protocol",
			"PATH_INFO", "/switch_protocol",
			"HTTP_UPGRADE", "raw",
			"HTTP_CONNECTION", "Upgrade",
			NULL
		);

		BufferedIO io(connection);
		string header;
		bool done = false;
		vector<ProcessPtr> processes;

		ensure_equals(io.readLine(), "HTTP/1.1 101 Switching Protocols\r\n");
		processes = pool->getProcesses();
		{
			LockGuard l(pool->syncher);
			ProcessPtr process = processes[0];
			ensure_equals(process->sessionSockets.top()->protocol, "http_session");
		}

		do {
			string line = io.readLine();
			done = line.empty() || line == "\r\n";
			if (!done) {
				header.append(line);
			}
		} while (!done);

		ensure("(1)", containsSubstring(header, "Upgrade: raw\r\n"));
		ensure("(2)", containsSubstring(header, "Connection: Upgrade\r\n"));

		writeExact(connection, "hello\n");
		ensure_equals(io.readLine(), "Echo: hello\n");
	}

	TEST_METHOD(61) {
		set_test_name("If the response contains Transfer-Encoding chunked, "
			"it dechunks the response body and forwards it until the "
			"zero-length chunk is encountered.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/chunked",
			NULL);

		string response = readAll(connection);
		string body = stripHeaders(response);
		ensure_equals(body,
			"chunk1\n"
			"chunk2\n"
			"chunk3\n");
	}

	TEST_METHOD(62) {
		set_test_name("If the response contains Transfer-Encoding chunked, "
			"it closes the connection with the app when the zero-length chunk is encountered.");

		DeleteFileEventually statusFile("/tmp/passenger-tail-status.txt");
		createFile("/tmp/passenger-tail-status.txt", "",
			S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/chunked",
			"HTTP_X_SLEEP_WHEN_DONE", "0.01",
			"HTTP_X_EXTRA_DATA", "true",
			"HTTP_X_TAIL_STATUS_FILE", "/tmp/passenger-tail-status.txt",
			NULL);

		string response = readAll(connection);
		string body = stripHeaders(response);
		ensure_equals(body,
			"chunk1\n"
			"chunk2\n"
			"chunk3\n");
		EVENTUALLY(5,
			result = readAll("/tmp/passenger-tail-status.txt") == "False";
		);
	}

	TEST_METHOD(63) {
		set_test_name("If the response contains Transfer-Encoding chunked, "
			"it discards any additional response body data after the zero-length chunk is encountered.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/chunked",
			"HTTP_X_EXTRA_DATA", "true",
			NULL);

		string response = readAll(connection);
		string body = stripHeaders(response);
		ensure_equals(body,
			"chunk1\n"
			"chunk2\n"
			"chunk3\n");
	}

	TEST_METHOD(64) {
		set_test_name("If the response contains Content-Length, "
			"it forwards exactly Content-Length bytes of the response body.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/blob",
			"HTTP_X_SIZE", "5000000",
			"HTTP_X_CONTENT_LENGTH", "true",
			NULL);

		string response = readAll(connection);
		string body = stripHeaders(response);
		FILE *f = fopen("/tmp/debug.txt", "w");
		fwrite(response.data(), 1, response.size(), f);
		fclose(f);
		ensure_equals(body.size(), 5000000u);
	}

	TEST_METHOD(65) {
		set_test_name("If the response contains Content-Length, "
			"it closes the connection with the app after forwarding exactly Content-Length bytes.");

		DeleteFileEventually statusFile("/tmp/passenger-tail-status.txt");
		createFile("/tmp/passenger-tail-status.txt", "",
			S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/blob",
			"HTTP_X_SIZE", "5000000",
			"HTTP_X_CONTENT_LENGTH", "true",
			"HTTP_X_SLEEP_WHEN_DONE", "0.01",
			"HTTP_X_EXTRA_DATA", "true",
			"HTTP_X_TAIL_STATUS_FILE", "/tmp/passenger-tail-status.txt",
			NULL);

		string response = readAll(connection);
		string body = stripHeaders(response);
		ensure_equals(body.size(), 5000000u);
		EVENTUALLY(5,
			result = readAll("/tmp/passenger-tail-status.txt") == "False";
		);
	}

	TEST_METHOD(66) {
		set_test_name("If the response contains Content-Length, "
			"it discards any additional response body data after Content-Length bytes.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/blob",
			"HTTP_X_SIZE", "5000000",
			"HTTP_X_CONTENT_LENGTH", "true",
			"HTTP_X_EXTRA_DATA", "true",
			NULL);

		string response = readAll(connection);
		string body = stripHeaders(response);
		ensure_equals(body.size(), 5000000u);
	}

	TEST_METHOD(67) {
		set_test_name("If the response contains neither Transfer-Encoding chunked nor Content-Length, "
			"it forwards the response body until EOF.");

		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", wsgiAppPath.c_str(),
			"PATH_INFO", "/blob",
			"HTTP_X_SIZE", "5000000",
			"HTTP_X_EXTRA_DATA", "true",
			NULL);

		string response = readAll(connection);
		string body = stripHeaders(response);
		ensure_equals(body.size(), 5000004u);
	}


	/***** Out-of-band work tests *****/

	TEST_METHOD(75) {
		set_test_name("If the application outputs a request oobw header, handler should remove the header, mark "
			"the process as oobw requested. The process should continue to process requests until the "
			"spawner spawns another process (to avoid the group being empty). As soon as the new "
			"process is spawned, the original process will make the oobw request. Afterwards, the "
			"original process is re-enabled.");
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
			boost::unique_lock<boost::mutex> lock(pool->syncher);
			result = origProcess->oobwStatus == Process::OOBW_NOT_ACTIVE;
		);
		
		// Final asserts.
		{
			boost::unique_lock<boost::mutex> lock(pool->syncher);
			ensure_equals("2 enabled processes", pool->superGroups.get(wsgiAppPath)->defaultGroup->enabledProcesses.size(), 2u);
			ensure_equals("oobw is reset", origProcess->oobwStatus, Process::OOBW_NOT_ACTIVE);
			ensure_equals("process is enabled", origProcess->enabled, Process::ENABLED);
		}
	}

	// Test small response buffering.
	// Test large response buffering.
	
	/***************************/
}
