/**
 * This file is used as a template to test the different ApplicationPool implementations.
 * It is #included in StandardApplicationPool.cpp
 */
#ifdef USE_TEMPLATE

	TEST_METHOD(APPLICATION_POOL_TEST_START + 1) {
		// Calling ApplicationPool.get() once should return a valid Application object.
		ApplicationPtr app(pool->get("/dummy/path"));
		char buf[5];

		ensure_equals("The Application object's PID is the same as the one specified by the mock",
			app->getPid(), 1234);
		ensure_equals("Application.getWriter() is a valid, writable file descriptor",
			write(app->getWriter(), "hello", 5),
			5);
		ensure_equals("Application.getReader() is a valid, readable file descriptor",
			read(app->getReader(), buf, 5),
			5);
		ensure("The two channels are connected with each other, as specified by the mock object",
			memcmp(buf, "hello", 5) == 0);
	}
	
	TEST_METHOD(APPLICATION_POOL_TEST_START + 2) {
		
	}
	
	TEST_METHOD(APPLICATION_POOL_TEST_START + 3) {
	}

#endif /* USE_TEMPLATE */
