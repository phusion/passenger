// Included in DirectSpawnerTest.cpp and SmartSpawnerTest.cpp.

	typedef shared_ptr<Spawner> SpawnerPtr;
	
	static void checkin(ProcessPtr process, Connection *conn) {
		process->sockets->front().checkinConnection(*conn);
	}
	
	TEST_METHOD(1) {
		// Basic spawning test.
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\1" "start.rb";
		options.startupFile  = "stub/rack/start.rb";
		SpawnerPtr spawner = createSpawner(options);
		ProcessPtr process = spawner->spawn(options);
		ensure_equals(process->sockets->size(), 1u);
		
		Connection conn = process->sockets->front().checkoutConnection();
		ScopeGuard guard(boost::bind(checkin, process, &conn));
		writeExact(conn.fd, "ping\n");
		ensure_equals(readAll(conn.fd), "pong\n");
	}

	TEST_METHOD(2) {
		// It enforces the given start timeout.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "sleep\1" "60";
		options.startupFile  = ".";
		options.startTimeout = 300;
		SpawnerPtr spawner = createSpawner(options);
		try {
			spawner->spawn(options);
			fail("Timeout expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::APP_STARTUP_TIMEOUT);
		}
	}

	TEST_METHOD(3) {
		// Any protocol errors during startup are caught and result
		// in exceptions.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "echo\1" "hello world";
		options.startupFile  = ".";
		SpawnerPtr spawner = createSpawner(options);
		try {
			spawner->spawn(options);
			fail("Exception expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::APP_STARTUP_PROTOCOL_ERROR);
		}
	}

	TEST_METHOD(4) {
		// The application may respond with a special Error response,
		// which will result in a SpawnException with the content.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "perl\1" "start_error.pl";
		options.startupFile  = "stub/start_error.pl";
		SpawnerPtr spawner = createSpawner(options);
		try {
			spawner->spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::APP_STARTUP_EXPLAINABLE_ERROR);
			ensure_equals(e.getErrorPage(),
				"He's dead, Jim!\n"
				"Relax, I'm a doctor.\n");
		}
	}

	TEST_METHOD(5) {
		// The start timeout is enforced even while reading the error
		// response.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "perl\1" "start_error.pl\1" "freeze";
		options.startupFile  = "stub/start_error.pl";
		options.startTimeout = 300;
		SpawnerPtr spawner = createSpawner(options);
		try {
			spawner->spawn(options);
			fail("Timeout expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::APP_STARTUP_TIMEOUT);
		}
	}

	TEST_METHOD(6) {
		// The reported PID is correct.
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\1" "start.rb";
		options.startupFile  = "stub/rack/start.rb";
		SpawnerPtr spawner = createSpawner(options);
		ProcessPtr process = spawner->spawn(options);
		ensure_equals(process->sockets->size(), 1u);
		
		Connection conn = process->sockets->front().checkoutConnection();
		ScopeGuard guard(boost::bind(checkin, process, &conn));
		writeExact(conn.fd, "pid\n");
		ensure_equals(readAll(conn.fd), toString(process->pid) + "\n");
	}
	
	TEST_METHOD(7) {
		// Custom environment variables can be passed.
		Options options = createOptions();
		options.appRoot = "stub/rack";
		options.startCommand = "ruby\1" "start.rb";
		options.startupFile  = "stub/rack/start.rb";
		options.environmentVariables.push_back(make_pair("PASSENGER_FOO", "foo"));
		options.environmentVariables.push_back(make_pair("PASSENGER_BAR", "bar"));
		SpawnerPtr spawner = createSpawner(options);
		ProcessPtr process = spawner->spawn(options);
		ensure_equals(process->sockets->size(), 1u);
		
		Connection conn = process->sockets->front().checkoutConnection();
		ScopeGuard guard(boost::bind(checkin, process, &conn));
		writeExact(conn.fd, "envvars\n");
		string envvars = readAll(conn.fd);
		ensure("(1)", envvars.find("PASSENGER_FOO = foo\n") != string::npos);
		ensure("(2)", envvars.find("PASSENGER_BAR = bar\n") != string::npos);
	}

	TEST_METHOD(8) {
		// Any raised SpawnExceptions take note of the process's environment variables.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "echo\1" "hello world";
		options.startupFile  = ".";
		options.environmentVariables.push_back(make_pair("PASSENGER_FOO", "foo"));
		SpawnerPtr spawner = createSpawner(options);
		try {
			spawner->spawn(options);
			fail("Exception expected");
		} catch (const SpawnException &e) {
			ensure(containsSubstring(e["envvars"], "PASSENGER_FOO=foo\n"));
		}
	}

	TEST_METHOD(9) {SHOW_EXCEPTION_BACKTRACE(
		// It raises an exception if the user does not have a access to one
		// of the app root's parent directories, or the app root itself.
		system("mkdir -p tmp.check/a/b/c");
		TempDirCopy dir("stub/rack", "tmp.check/a/b/c/d");
		TempDir dir2("tmp.check");

		char buffer[PATH_MAX];
		string cwd = getcwd(buffer, sizeof(buffer));

		Options options = createOptions();
		options.appRoot = "tmp.check/a/b/c/d";
		options.appType = "rack";
		SpawnerPtr spawner = createSpawner(options);

		if (getuid() != 0) {
			// TODO: implement this test for root too
			system("chmod 000 tmp.check/a/b/c/d");
			system("chmod 600 tmp.check/a/b/c");
			system("chmod 600 tmp.check/a");

			try {
				spawner->spawn(options);
				fail("SpawnException expected");
			} catch (const SpawnException &e) {
				ensure(containsSubstring(e.getErrorPage(),
					"the parent directory '" + cwd + "/tmp.check/a' has wrong permissions"));
			}

			system("chmod 700 tmp.check/a");
			try {
				spawner->spawn(options);
				fail("SpawnException expected");
			} catch (const SpawnException &e) {
				ensure(containsSubstring(e.getErrorPage(),
					"the parent directory '" + cwd + "/tmp.check/a/b/c' has wrong permissions"));
			}

			system("chmod 700 tmp.check/a/b/c");
			try {
				spawner->spawn(options);
				fail("SpawnException expected");
			} catch (const SpawnException &e) {
				ensure(containsSubstring(e.getErrorPage(),
					"However this directory is not accessible because it has wrong permissions."));
			}

			system("chmod 700 tmp.check/a/b/c/d");
			//spawner->spawn(options); // Should not throw.
		}
	);}
	
	// User switching works.
	// It raises an exception if getStartupCommand() is empty.
