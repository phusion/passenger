// Included in DirectSpawnerTest.cpp and SmartSpawnerTest.cpp.

	typedef boost::shared_ptr<Spawner> SpawnerPtr;

	TEST_METHOD(1) {
		set_test_name("Basic spawning test");
		SpawningKit::AppPoolOptions options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby start.rb";
		options.startupFile  = "start.rb";
		SpawnerPtr spawner = createSpawner(options);
		result = spawner->spawn(options);
		ensure_equals(result.sockets.size(), 1u);

		FileDescriptor fd(connectToServer(result.sockets[0].address,
			__FILE__, __LINE__), NULL, 0);
		writeExact(fd, "ping\n");
		ensure_equals(readAll(fd), "pong\n");
	}

	TEST_METHOD(2) {
		set_test_name("It enforces the given start timeout");
		SpawningKit::AppPoolOptions options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "sleep 60";
		options.startupFile  = ".";
		options.startTimeout = 100;
		LoggingKit::setLevel(LoggingKit::CRIT);

		EVENTUALLY(5,
			SpawnerPtr spawner = createSpawner(options);
			try {
				spawner->spawn(options);
				fail("SpawnException expected");
			} catch (const SpawnException &e) {
				result = e.getErrorCategory() == SpawningKit::TIMEOUT_ERROR;
				if (!result) {
					// It didn't work, maybe because the server is too busy.
					// Try again with higher timeout.
					options.startTimeout = std::min<unsigned int>(
						options.startTimeout * 2, 1000);
				}
				if (!result) {
					// It didn't work, maybe because the server is too busy.
					// Try again with higher timeout.
					options.startTimeout = std::min<unsigned int>(
						options.startTimeout * 2, 1000);
				}
			}
		);
	}

	TEST_METHOD(6) {
		set_test_name("The reported PID is correct");
		SpawningKit::AppPoolOptions options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby start.rb";
		options.startupFile  = "start.rb";
		SpawnerPtr spawner = createSpawner(options);
		result = spawner->spawn(options);
		ensure_equals(result.sockets.size(), 1u);

		FileDescriptor fd(connectToServer(result.sockets[0].address,
			__FILE__, __LINE__), NULL, 0);
		writeExact(fd, "pid\n");
		ensure_equals(readAll(fd), toString(result.pid) + "\n");
	}
