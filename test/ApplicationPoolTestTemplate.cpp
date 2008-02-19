#include <unistd.h>
#include <errno.h>
#include <cstring>

/**
 * This file is used as a template to test the different ApplicationPool implementations.
 * It is #included in StandardApplicationPool.cpp
 */
#ifdef USE_TEMPLATE

	static string createRequestHeaders() {
		string headers;
		#define ADD_HEADER(name, value) \
			headers.append(name); \
			headers.append(1, '\0'); \
			headers.append(value); \
			headers.append(1, '\0')
		ADD_HEADER("HTTP_HOST", "www.test.com");
		ADD_HEADER("QUERY_STRING", "");
		ADD_HEADER("REQUEST_URI", "/foo/new");
		ADD_HEADER("REQUEST_METHOD", "GET");
		ADD_HEADER("REMOTE_ADDR", "localhost");
		return headers;
	}
	
	static string readAll(int fd) {
		string result;
		char buf[1024 * 32];
		ssize_t ret;
		while (true) {
			do {
				ret = read(fd, buf, sizeof(buf));
			} while (ret == -1 && errno == EINTR);
			if (ret == 0) {
				break;
			} else if (ret == -1) {
				throw strerror(errno);
			} else {
				result.append(buf, ret);
			}
		}
		return result;
	}

	TEST_METHOD(APPLICATION_POOL_TEST_START + 1) {
		// Calling ApplicationPool.get() once should return a valid Session.
		Application::SessionPtr session(pool->get("stub/railsapp"));
		session->sendHeaders(createRequestHeaders());
		session->closeWriter();
		
		int reader = session->getReader();
		string result(readAll(reader));
		session->closeReader();
		ensure(result.find("hello world") != string::npos);
	}
	
	TEST_METHOD(APPLICATION_POOL_TEST_START + 2) {
		// Verify that the pool spawns a new app, and that
		// after the session is closed, the app is kept around.
		Application::SessionPtr session(pool->get("stub/railsapp"));
		ensure_equals("Before the session was closed, the app was busy", pool->getActive(), 1u);
		ensure_equals("Before the session was closed, the app was in the pool", pool->getCount(), 1u);
		session.reset();
		ensure_equals("After the session is closed, the app is no longer busy", pool->getActive(), 0u);
		ensure_equals("After the session is closed, the app is kept around", pool->getCount(), 1u);
	}
	
	TEST_METHOD(APPLICATION_POOL_TEST_START + 4) {
		// If we call get() with an application root, then we close the session,
		// and then we call get() again with the same application root,
		// then the pool should not have spawned more than 1 app in total.
		Application::SessionPtr session(pool->get("stub/railsapp"));
		session.reset();
		session = pool->get("stub/railsapp");
		ensure_equals(pool->getCount(), 1u);
	}
	
	TEST_METHOD(APPLICATION_POOL_TEST_START + 5) {
		// If we call get() with an application root, then we call get() again before closing
		// the session, then the pool should have spawned 2 apps in total.
		Application::SessionPtr session(pool->get("stub/railsapp"));
		Application::SessionPtr session2(pool->get("stub/railsapp"));
		ensure_equals(pool->getCount(), 2u);
	}
	
	TEST_METHOD(APPLICATION_POOL_TEST_START + 6) {
		// If we call get() twice with different application roots,
		// then the pool should spawn two different apps.
		Application::SessionPtr session(pool->get("stub/railsapp"));
		Application::SessionPtr session2(pool->get("stub/railsapp2"));
		ensure_equals("Before the sessions were closed, both apps were busy", pool->getActive(), 2u);
		ensure_equals("Before the sessions were closed, both apps were in the pool", pool->getCount(), 2u);
		
		session->sendHeaders(createRequestHeaders());
		string result(readAll(session->getReader()));
		ensure("Session 1 belongs to the correct app", result.find("hello world"));
		session.reset();
		
		session2->sendHeaders(createRequestHeaders());
		result = readAll(session2->getReader());
		ensure("Session 2 belongs to the correct app", result.find("this is railsapp2"));
		session2.reset();
	}
	
	TEST_METHOD(APPLICATION_POOL_TEST_START + 7) {
		// If we call get() twice with different application roots,
		// and we close both sessions, then both 2 apps should still
		// be in the pool.
		Application::SessionPtr session(pool->get("stub/railsapp"));
		Application::SessionPtr session2(pool->get("stub/railsapp2"));
		session.reset();
		session2.reset();
		ensure_equals(pool->getActive(), 0u);
		ensure_equals(pool->getCount(), 2u);
	}
	
	#if 0
	TEST_METHOD(APPLICATION_POOL_TEST_START + 8) {
		// If we call get() even though the pool is already full
		// (active == max), and the application root is already
		// in the pool, then the pool should have tried to open
		// a session in an already active app.
		
		// TODO: How do we test this? Creating session2 will deadlock because
		// Application.connect() is waiting for the app to accept the connection,
		// and the app is waiting for session1 to finish.
		return;
		pool->setMax(1);
		Application::SessionPtr session1(pool->get("stub/railsapp"));
		Application::SessionPtr session2(pool->get("stub/railsapp"));
		ensure_equals("An attempt to open a session at an already busy app was made", pool->getActive(), 2u);
		ensure_equals("No new app has been spawned", pool->getCount(), 1u);
	}
	
	TEST_METHOD(APPLICATION_POOL_TEST_START + 9) {
		// If we call get() even though the pool is already full
		// (active == max), and the application root is *not* already
		// in the pool, then the pool will wait until enough sessions
		// have been closed.
		// TODO
	}
	
	TEST_METHOD(APPLICATION_POOL_TEST_START + 10) {
		// Test whether get() throws the right exceptions
		// TODO
	}
	
	TEST_METHOD(APPLICATION_POOL_TEST_START + 11) {
		// Test whether Session is still usable after the Application has been destroyed.
	}
	#endif
	
	// TODO: test spawning application as a different user
	// TODO: test restarting of applications

#endif /* USE_TEMPLATE */
