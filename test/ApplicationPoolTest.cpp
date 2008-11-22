#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <signal.h>

/**
 * This file is used as a template to test the different ApplicationPool implementations.
 * It is #included in StandardApplicationPoolTest.cpp and ApplicationServer_ApplicationPoolTest.cpp
 */
#ifdef USE_TEMPLATE

	static string createRequestHeaders(const char *uri = "/foo/new") {
		string headers;
		#define ADD_HEADER(name, value) \
			headers.append(name); \
			headers.append(1, '\0'); \
			headers.append(value); \
			headers.append(1, '\0')
		ADD_HEADER("HTTP_HOST", "www.test.com");
		ADD_HEADER("QUERY_STRING", "");
		ADD_HEADER("REQUEST_URI", uri);
		ADD_HEADER("REQUEST_METHOD", "GET");
		ADD_HEADER("REMOTE_ADDR", "localhost");
		ADD_HEADER("PATH_INFO", uri);
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
				throw SystemException("Cannot read from socket", errno);
			} else {
				result.append(buf, ret);
			}
		}
		return result;
	}
	
	static Application::SessionPtr spawnRackApp(ApplicationPoolPtr pool, const char *appRoot) {
		PoolOptions options;
		options.appRoot = appRoot;
		options.appType = "rack";
		return pool->get(options);
	}
	
	static Application::SessionPtr spawnWsgiApp(ApplicationPoolPtr pool, const char *appRoot) {
		PoolOptions options;
		options.appRoot = appRoot;
		options.appType = "wsgi";
		return pool->get(options);
	}

	TEST_METHOD(1) {
		// Calling ApplicationPool.get() once should return a valid Session.
		Application::SessionPtr session(pool->get("stub/railsapp"));
		session->sendHeaders(createRequestHeaders());
		session->shutdownWriter();

		int reader = session->getStream();
		string result(readAll(reader));
		session->closeStream();
		ensure(result.find("hello world") != string::npos);
	}
	
	TEST_METHOD(2) {
		// Verify that the pool spawns a new app, and that
		// after the session is closed, the app is kept around.
		Application::SessionPtr session(spawnRackApp(pool, "stub/rack"));
		ensure_equals("Before the session was closed, the app was busy", pool->getActive(), 1u);
		ensure_equals("Before the session was closed, the app was in the pool", pool->getCount(), 1u);
		session.reset();
		ensure_equals("After the session is closed, the app is no longer busy", pool->getActive(), 0u);
		ensure_equals("After the session is closed, the app is kept around", pool->getCount(), 1u);
	}
	
	TEST_METHOD(3) {
		// If we call get() with an application root, then we close the session,
		// and then we call get() again with the same application root,
		// then the pool should not have spawned more than 1 app in total.
		Application::SessionPtr session(spawnRackApp(pool, "stub/rack"));
		session.reset();
		session = spawnRackApp(pool, "stub/rack");
		ensure_equals(pool->getCount(), 1u);
	}
	
	TEST_METHOD(4) {
		// If we call get() with an application root, then we call get() again before closing
		// the session, then the pool should have spawned 2 apps in total.
		Application::SessionPtr session(spawnRackApp(pool, "stub/rack"));
		Application::SessionPtr session2(spawnRackApp(pool, "stub/rack"));
		ensure_equals(pool->getCount(), 2u);
	}
	
	TEST_METHOD(5) {
		// If we call get() twice with different application roots,
		// then the pool should spawn two different apps.
		Application::SessionPtr session(pool->get("stub/railsapp"));
		Application::SessionPtr session2(pool2->get("stub/railsapp2"));
		ensure_equals("Before the sessions were closed, both apps were busy", pool->getActive(), 2u);
		ensure_equals("Before the sessions were closed, both apps were in the pool", pool->getCount(), 2u);
		
		session->sendHeaders(createRequestHeaders());
		string result(readAll(session->getStream()));
		ensure("Session 1 belongs to the correct app", result.find("hello world"));
		session.reset();
		
		session2->sendHeaders(createRequestHeaders());
		result = readAll(session2->getStream());
		ensure("Session 2 belongs to the correct app", result.find("this is railsapp2"));
		session2.reset();
	}
	
	TEST_METHOD(6) {
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
	
	TEST_METHOD(7) {
		// If we call get() even though the pool is already full
		// (active == max), and the application root is already
		// in the pool, then the pool must wait until there's an
		// inactive application.
		pool->setMax(1);
		// TODO: How do we test this?
	}
	
	TEST_METHOD(8) {
		// If ApplicationPool spawns a new instance,
		// and we kill it, then the next get() with the
		// same application root should throw an exception.
		// But the get() thereafter should not:
		// ApplicationPool should have spawned a new instance
		// after detecting that the original one died.
		Application::SessionPtr session(pool->get("stub/railsapp"));
		kill(session->getPid(), SIGTERM);
		session.reset();
		try {
			session = pool->get("stub/railsapp");
			fail("ApplicationPool::get() is supposed to "
				"throw an exception because we killed "
				"the app instance.");
		} catch (const exception &e) {
			session = pool->get("stub/railsapp");
			// Should not throw.
		}
	}
	
	struct TestThread1 {
		ApplicationPoolPtr pool;
		Application::SessionPtr &m_session;
		bool &m_done;
		
		TestThread1(const ApplicationPoolPtr &pool,
			Application::SessionPtr &session,
			bool &done)
		: m_session(session), m_done(done) {
			this->pool = pool;
			done = false;
		}
		
		void operator()() {
			m_session = spawnWsgiApp(pool, "stub/wsgi");
			m_done = true;
		}
	};

	TEST_METHOD(9) {
		// If we call get() even though the pool is already full
		// (active == max), and the application root is *not* already
		// in the pool, then the pool will wait until enough sessions
		// have been closed.
		pool->setMax(2);
		Application::SessionPtr session1(spawnRackApp(pool, "stub/rack"));
		Application::SessionPtr session2(spawnRackApp(pool2, "stub/rack"));
		Application::SessionPtr session3;
		bool done;
		
		thread *thr = new thread(TestThread1(pool2, session3, done));
		usleep(500000);
		ensure("ApplicationPool is waiting", !done);
		ensure_equals(pool->getActive(), 2u);
		ensure_equals(pool->getCount(), 2u);
		
		session1.reset();
		
		// Wait at most 10 seconds.
		time_t begin = time(NULL);
		while (!done && time(NULL) - begin < 10) {
			usleep(100000);
		}
		
		ensure("Session 3 is openend", done);
		ensure_equals(pool->getActive(), 2u);
		ensure_equals(pool->getCount(), 2u);
		
		thr->join();
		delete thr;
	}
	
	TEST_METHOD(10) {
		// If we call get(), and:
		// * the pool is already full, but there are inactive apps
		//   (active < count && count == max)
		// and
		// * the application root is *not* already in the pool
		// then the an inactive app should be killed in order to
		// satisfy this get() command.
		pool->setMax(2);
		Application::SessionPtr session1(pool->get("stub/railsapp"));
		Application::SessionPtr session2(pool->get("stub/railsapp"));
		session1.reset();
		session2.reset();
		
		ensure_equals(pool->getActive(), 0u);
		ensure_equals(pool->getCount(), 2u);
		session1 = pool2->get("stub/railsapp2");
		ensure_equals(pool->getActive(), 1u);
		ensure_equals(pool->getCount(), 2u);
	}
	
	TEST_METHOD(11) {
		// Test whether Session is still usable after the Application has been destroyed.
		Application::SessionPtr session(pool->get("stub/railsapp"));
		pool->clear();
		pool.reset();
		pool2.reset();
		
		session->sendHeaders(createRequestHeaders());
		session->shutdownWriter();
		
		int reader = session->getStream();
		string result(readAll(reader));
		session->closeStream();
		ensure(result.find("hello world") != string::npos);
	}
	
	TEST_METHOD(12) {
		// If tmp/restart.txt is present, then the applications under app_root
		// should be restarted.
		struct stat buf;
		Application::SessionPtr session1 = pool->get("stub/railsapp");
		Application::SessionPtr session2 = pool2->get("stub/railsapp");
		session1.reset();
		session2.reset();
		
		system("touch stub/railsapp/tmp/restart.txt");
		pool->get("stub/railsapp");
		
		ensure_equals("No apps are active", pool->getActive(), 0u);
		ensure_equals("Both apps are killed, and a new one was spawned",
			pool->getCount(), 1u);
		ensure("Restart file has been deleted",
			stat("stub/railsapp/tmp/restart.txt", &buf) == -1
			&& errno == ENOENT);
	}
	
	TEST_METHOD(13) {
		// If tmp/restart.txt is present, but cannot be deleted, then
		// the applications under app_root should still be restarted.
		// However, a subsequent get() should not result in a restart.
		pid_t old_pid, pid;
		struct stat buf;
		Application::SessionPtr session1 = pool->get("stub/railsapp");
		Application::SessionPtr session2 = pool2->get("stub/railsapp");
		session1.reset();
		session2.reset();
		
		system("mkdir -p stub/railsapp/tmp/restart.txt");
		
		old_pid = pool->get("stub/railsapp")->getPid();
		try {
			ensure("Restart file has not been deleted",
				stat("stub/railsapp/tmp/restart.txt", &buf) == 0);
			system("rmdir stub/railsapp/tmp/restart.txt");
		} catch (...) {
			system("rmdir stub/railsapp/tmp/restart.txt");
			throw;
		}
		
		pid = pool->get("stub/railsapp")->getPid();
		ensure_equals("The app was not restarted", pid, old_pid);
		unlink("stub/railsapp/tmp/restart.txt");
	}
	
	TEST_METHOD(14) {
		// If tmp/restart.txt is present, but cannot be deleted, then
		// the applications under app_root should still be restarted.
		// A subsequent get() should only restart if we've changed
		// restart.txt's mtime.
		pid_t old_pid;
		Application::SessionPtr session1 = pool->get("stub/railsapp");
		Application::SessionPtr session2 = pool2->get("stub/railsapp");
		session1.reset();
		session2.reset();
		
		setenv("nextRestartTxtDeletionShouldFail", "1", 1);
		system("touch stub/railsapp/tmp/restart.txt");
		old_pid = pool->get("stub/railsapp")->getPid();
		ensure_equals(pool->getActive(), 0u);
		ensure_equals(pool->getCount(), 1u);

		sleep(1); // Allow the next mtime to be different.
		system("touch stub/railsapp/tmp/restart.txt");
		ensure("The app is restarted, and the last app instance was not reused",
			pool2->get("stub/railsapp")->getPid() != old_pid);
		
		unlink("stub/railsapp/tmp/restart.txt");
	}
	
	TEST_METHOD(15) {
		// Test whether restarting really results in code reload.
		system("cp -f stub/railsapp/app/controllers/bar_controller_1.rb "
			"stub/railsapp/app/controllers/bar_controller.rb");
		Application::SessionPtr session = pool->get("stub/railsapp");
		session->sendHeaders(createRequestHeaders("/bar"));
		string result = readAll(session->getStream());
		ensure(result.find("bar 1!"));
		session.reset();
		
		system("cp -f stub/railsapp/app/controllers/bar_controller_2.rb "
			"stub/railsapp/app/controllers/bar_controller.rb");
		system("touch stub/railsapp/tmp/restart.txt");
		session = pool->get("stub/railsapp");
		session->sendHeaders(createRequestHeaders("/bar"));
		result = readAll(session->getStream());
		ensure("App code has been reloaded", result.find("bar 2!"));
		unlink("stub/railsapp/app/controllers/bar_controller.rb");
	}
	
	TEST_METHOD(16) {
		// The cleaner thread should clean idle applications without crashing.
		pool->setMaxIdleTime(1);
		spawnRackApp(pool, "stub/rack");
		
		time_t begin = time(NULL);
		while (pool->getCount() == 1u && time(NULL) - begin < 10) {
			usleep(100000);
		}
		ensure_equals("App should have been cleaned up", pool->getCount(), 0u);
	}
	
	TEST_METHOD(17) {
		// MaxPerApp is respected.
		pool->setMax(3);
		pool->setMaxPerApp(1);
		
		// We connect to stub/rack while it already has an instance with
		// 1 request in its queue. Assert that the pool doesn't spawn
		// another instance.
		Application::SessionPtr session1 = spawnRackApp(pool, "stub/rack");
		Application::SessionPtr session2 = spawnRackApp(pool2, "stub/rack");
		ensure_equals(pool->getCount(), 1u);
		
		// We connect to stub/wsgi. Assert that the pool spawns a new
		// instance for this app.
		ApplicationPoolPtr pool3(newPoolConnection());
		Application::SessionPtr session3 = spawnWsgiApp(pool3, "stub/wsgi");
		ensure_equals(pool->getCount(), 2u);
	}
	
	TEST_METHOD(18) {
		// Application instance is shutdown after 'maxRequests' requests.
		PoolOptions options("stub/railsapp");
		int reader;
		pid_t originalPid;
		Application::SessionPtr session;
		
		options.maxRequests = 4;
		pool->setMax(1);
		session = pool->get(options);
		originalPid = session->getPid();
		session.reset();
		
		for (unsigned int i = 0; i < 4; i++) {
			session = pool->get(options);
			session->sendHeaders(createRequestHeaders());
			session->shutdownWriter();
			reader = session->getStream();
			readAll(reader);
			// Must explicitly call reset() here because we
			// want to close the session right now.
			session.reset();
			// In case of ApplicationPoolServer, we sleep here
			// for a little while to force a context switch to
			// the server, so that the session close event may
			// be processed.
			usleep(100000);
		}
		
		session = pool->get(options);
		ensure(session->getPid() != originalPid);
	}
	
	struct SpawnRackAppFunction {
		ApplicationPoolPtr pool;
		bool *done;
		
		void operator()() {
			PoolOptions options;
			options.appRoot = "stub/rack";
			options.appType = "rack";
			options.useGlobalQueue = true;
			pool->get(options);
			*done = true;
		}
	};
	
	TEST_METHOD(19) {
		// If global queueing mode is enabled, then get() waits until
		// there's at least one idle backend process for this application
		// domain.
		pool->setMax(1);
		
		PoolOptions options;
		options.appRoot = "stub/rack";
		options.appType = "rack";
		options.useGlobalQueue = true;
		Application::SessionPtr session = pool->get(options);
		
		bool done = false;
		SpawnRackAppFunction func;
		func.pool = pool2;
		func.done = &done;
		boost::thread thr(func);
		usleep(100000);
		
		// Previous session hasn't been closed yet, so pool should still
		// be waiting.
		ensure(!done);
		
		// Close the previous session. The thread should now finish.
		session.reset();
		thr.join();
	}

#endif /* USE_TEMPLATE */
