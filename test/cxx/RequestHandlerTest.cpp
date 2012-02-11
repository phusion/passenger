#include <TestSupport.h>
#include <agents/HelperAgent/RequestHandler.h>
#include <agents/HelperAgent/RequestHandler.cpp>
#include <agents/HelperAgent/AgentOptions.h>
#include <ApplicationPool2/Pool.h>
#include <Utils/IOUtils.h>

#include <boost/shared_array.hpp>
#include <string>
#include <vector>
#include <map>
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
		BackgroundEventLoop bg;
		SpawnerFactoryPtr spawnerFactory;
		PoolPtr pool;
		string serverFilename;
		FileDescriptor requestSocket;
		shared_ptr<RequestHandler> handler;
		AgentOptions agentOptions;
		FileDescriptor connection;
		map<string, string> defaultHeaders;

		string root;
		string rackAppPath;
		
		RequestHandlerTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			spawnerFactory = make_shared<SpawnerFactory>(bg.safe, *resourceLocator, generation);
			pool = make_shared<Pool>(bg.safe.get(), spawnerFactory);
			serverFilename = generation->getPath() + "/server";
			requestSocket = createUnixServer(serverFilename);
			setNonBlocking(requestSocket);

			agentOptions.passengerRoot = resourceLocator->getRoot();
			root = resourceLocator->getRoot();
			rackAppPath = root + "/test/stub/rack";
			defaultHeaders["PASSENGER_LOAD_SHELL_ENVVARS"] = "false";
			defaultHeaders["PASSENGER_APP_TYPE"] = "rack";
			defaultHeaders["PASSENGER_SPAWN_METHOD"] = "direct";
		}
		
		~RequestHandlerTest() {
			unlink(serverFilename.c_str());
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
	};

	DEFINE_TEST_GROUP(RequestHandlerTest);

	TEST_METHOD(1) {
		// Test one normal request.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", rackAppPath.c_str(),
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		string body = stripHeaders(response);
		ensure("Status line is correct", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
		ensure("Headers are correct", containsSubstring(response, "Content-Type: text/html\r\n"));
		ensure("Contains a Status header", containsSubstring(response, "Status: 200\r\n"));
		ensure_equals(body, "hello <b>world</b>");
	}

	TEST_METHOD(2) {
		// Test multiple normal requests.
		init();
		for (int i = 0; i < 10; i++) {
			connect();
			sendHeaders(defaultHeaders,
				"PASSENGER_APP_ROOT", rackAppPath.c_str(),
				"PATH_INFO", "/",
				NULL);
			string response = readAll(connection);
			string body = stripHeaders(response);
			ensure("Status line is correct", containsSubstring(response, "HTTP/1.1 200 OK\r\n"));
			ensure("Headers are correct", containsSubstring(response, "Content-Type: text/html\r\n"));
			ensure("Contains a Status header", containsSubstring(response, "Status: 200\r\n"));
			ensure_equals(body, "hello <b>world</b>");
		}
	}

	TEST_METHOD(3) {
		// The response doesn't contain an HTTP status line if PASSENGER_PRINT_STATUS_LINE is false.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", rackAppPath.c_str(),
			"PASSENGER_PRINT_STATUS_LINE", "false",
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(!containsSubstring(response, "HTTP/1.1 "));
		ensure(containsSubstring(response, "Status: 200\r\n"));
	}
	
	TEST_METHOD(10) {
		// If the app crashes at startup without an error page then it renders
		// a generic error page.
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'I have failed'");

		setLogLevel(-2);
		spawnerFactory->forwardStderr = false;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\1" + root + "/test/tmp.handler/start.rb").c_str(),
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Status: 500\r\n"));
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
		spawnerFactory->forwardStderr = false;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\1" + root + "/test/tmp.handler/start.rb").c_str(),
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Status: 500\r\n"));
		ensure(containsSubstring(response, "Content-Type: text/html; charset=UTF-8\r\n"));
		ensure(containsSubstring(response, "<html>"));
		ensure(containsSubstring(response, "I have failed"));
	}

	TEST_METHOD(12) {
		// Error pages respect the PASSENGER_PRINT_STATUS_LINE option.
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'I have failed'");

		setLogLevel(-2);
		spawnerFactory->forwardStderr = false;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\1" + root + "/test/tmp.handler/start.rb").c_str(),
			"PASSENGER_PRINT_STATUS_LINE", "false",
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(!containsSubstring(response, "HTTP/1.1 "));
		ensure(containsSubstring(response, "Status: 500\r\n"));
		ensure(containsSubstring(response, "I have failed"));
	}

	TEST_METHOD(13) {
		// If PASSENGER_FRIENDLY_ERROR_PAGES is false then it does not render
		// a friendly error page.
		TempDir tempdir("tmp.handler");
		writeFile("tmp.handler/start.rb",
			"STDERR.puts 'Error'\n"
			"STDERR.puts\n"
			"STDERR.puts 'I have failed'\n");

		setLogLevel(-2);
		spawnerFactory->forwardStderr = false;
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", (root + "/test/tmp.handler").c_str(),
			"PASSENGER_APP_TYPE", "",
			"PASSENGER_START_COMMAND", ("ruby\1" + root + "/test/tmp.handler/start.rb").c_str(),
			"PASSENGER_FRIENDLY_ERROR_PAGES", "false",
			"PATH_INFO", "/",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "HTTP/1.1 500 Internal Server Error\r\n"));
		ensure(containsSubstring(response, "Status: 500\r\n"));
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
			"PASSENGER_APP_ROOT", rackAppPath.c_str(),
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
			"PASSENGER_APP_ROOT", rackAppPath.c_str(),
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

	TEST_METHOD(30) {
		// It replaces HTTP_CONTENT_LENGTH with CONTENT_LENGTH.
		init();
		connect();
		sendHeaders(defaultHeaders,
			"PASSENGER_APP_ROOT", rackAppPath.c_str(),
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
			"PASSENGER_APP_ROOT", rackAppPath.c_str(),
			"PATH_INFO", "/env",
			"HTTP_CONTENT_TYPE", "application/json",
			NULL);
		string response = readAll(connection);
		ensure(containsSubstring(response, "CONTENT_TYPE = application/json\n"));
		ensure(!containsSubstring(response, "HTTP_CONTENT_TYPE"));
	}
}
