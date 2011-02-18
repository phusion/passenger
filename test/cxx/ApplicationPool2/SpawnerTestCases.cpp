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
		} catch (const TimeoutException &) {
			// Pass.
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
		} catch (const IOException &) {
			// Pass.
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
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			// Pass.
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
	
	// Environment variables.
	// User switching.
