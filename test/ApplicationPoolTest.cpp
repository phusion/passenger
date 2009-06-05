#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <sys/types.h>
#include <signal.h>
#include <utime.h>

/**
 * This file is used as a template to test the different ApplicationPool implementations.
 * It is #included in StandardApplicationPoolTest.cpp and ApplicationServer_ApplicationPoolTest.cpp
 */
#ifdef USE_TEMPLATE
	/** Create some stub request headers. */
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
		ADD_HEADER("SCRIPT_NAME", "");
		ADD_HEADER("PATH_INFO", uri);
		return headers;
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
		Application::SessionPtr session(spawnRackApp(pool, "stub/rack"));
		session->sendHeaders(createRequestHeaders());
		session->shutdownWriter();

		int reader = session->getStream();
		string result(readAll(reader));
		session->closeStream();
		ensure(result.find("hello <b>world</b>") != string::npos);
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
		Application::SessionPtr session2(spawnRackApp(pool2, "stub/rack"));
		ensure_equals(pool->getCount(), 2u);
	}
	
	TEST_METHOD(5) {
		// If we call get() twice with different application roots,
		// then the pool should spawn two different apps.
		TempDirCopy c1("stub/rack", "rackapp1.tmp");
		TempDirCopy c2("stub/rack", "rackapp2.tmp");
		replaceStringInFile("rackapp2.tmp/config.ru", "world", "world 2");
		Application::SessionPtr session(spawnRackApp(pool, "rackapp1.tmp"));
		Application::SessionPtr session2(spawnRackApp(pool2, "rackapp2.tmp"));
		ensure_equals("Before the sessions were closed, both apps were busy", pool->getActive(), 2u);
		ensure_equals("Before the sessions were closed, both apps were in the pool", pool->getCount(), 2u);
		
		session->sendHeaders(createRequestHeaders());
		string result(readAll(session->getStream()));
		ensure("Session 1 belongs to the correct app", result.find("hello <b>world</b>") != string::npos);
		session.reset();
		
		session2->sendHeaders(createRequestHeaders());
		result = readAll(session2->getStream());
		ensure("Session 2 belongs to the correct app", result.find("hello <b>world 2</b>") != string::npos);
		session2.reset();
	}
	
	TEST_METHOD(6) {
		// If we call get() twice with different application roots,
		// and we close both sessions, then both 2 apps should still
		// be in the pool.
		TempDirCopy c1("stub/rack", "rackapp1.tmp");
		TempDirCopy c2("stub/rack", "rackapp2.tmp");
		Application::SessionPtr session(spawnRackApp(pool, "rackapp1.tmp"));
		Application::SessionPtr session2(spawnRackApp(pool, "rackapp2.tmp"));
		session.reset();
		session2.reset();
		ensure_equals("There are 0 active apps", pool->getActive(), 0u);
		ensure_equals("There are 2 apps in total", pool->getCount(), 2u);
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
		Application::SessionPtr session(spawnRackApp(pool, "stub/rack"));
		kill(session->getPid(), SIGTERM);
		session.reset();
		try {
			session = spawnRackApp(pool, "stub/rack");
			fail("ApplicationPool::get() is supposed to "
				"throw an exception because we killed "
				"the app instance.");
		} catch (const exception &e) {
			session = spawnRackApp(pool, "stub/rack");
			// Should not throw.
		}
	}
	
	struct PoolWaitTestThread {
		ApplicationPoolPtr pool;
		Application::SessionPtr &m_session;
		bool &m_done;
		
		PoolWaitTestThread(const ApplicationPoolPtr &pool,
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
		
		shared_ptr<boost::thread> thr = ptr(new boost::thread(PoolWaitTestThread(pool2, session3, done)));
		usleep(500000);
		ensure("ApplicationPool is still waiting", !done);
		ensure_equals(pool->getActive(), 2u);
		ensure_equals(pool->getCount(), 2u);
		
		// Now release one slot from the pool.
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
		thr.reset();
	}
	
	TEST_METHOD(10) {
		// If we call get(), and:
		// * the pool is already full, but there are inactive apps
		//   (active < count && count == max)
		// and
		// * the application root for this get() is *not* already in the pool
		// then the an inactive app should be killed in order to
		// satisfy this get() command.
		TempDirCopy c1("stub/rack", "rackapp1.tmp");
		TempDirCopy c2("stub/rack", "rackapp2.tmp");
		pool->setMax(2);
		Application::SessionPtr session1(spawnRackApp(pool, "rackapp1.tmp"));
		Application::SessionPtr session2(spawnRackApp(pool, "rackapp1.tmp"));
		session1.reset();
		session2.reset();
		
		ensure_equals(pool->getActive(), 0u);
		ensure_equals(pool->getCount(), 2u);
		session1 = spawnRackApp(pool, "rackapp2.tmp");
		ensure_equals(pool->getActive(), 1u);
		ensure_equals(pool->getCount(), 2u);
	}
	
	TEST_METHOD(11) {
		// A Session should still be usable after the pool has been destroyed.
		Application::SessionPtr session(spawnRackApp(pool, "stub/rack"));
		pool->clear();
		pool.reset();
		pool2.reset();
		
		session->sendHeaders(createRequestHeaders());
		session->shutdownWriter();
		
		int reader = session->getStream();
		string result(readAll(reader));
		session->closeStream();
		ensure(result.find("hello <b>world</b>") != string::npos);
	}
	
	TEST_METHOD(12) {
		// If tmp/restart.txt didn't exist but has now been created,
		// then the applications under app_root should be restarted.
		struct stat buf;
		TempDirCopy c("stub/rack", "rackapp.tmp");
		Application::SessionPtr session1 = spawnRackApp(pool, "rackapp.tmp");
		Application::SessionPtr session2 = spawnRackApp(pool, "rackapp.tmp");
		session1.reset();
		session2.reset();
		
		touchFile("rackapp.tmp/tmp/restart.txt");
		spawnRackApp(pool, "rackapp.tmp");
		
		ensure_equals("No apps are active", pool->getActive(), 0u);
		ensure_equals("Both apps are killed, and a new one was spawned",
			pool->getCount(), 1u);
		ensure("Restart file still exists",
			stat("rackapp.tmp/tmp/restart.txt", &buf) == 0);
	}
	
	TEST_METHOD(13) {
		// If tmp/restart.txt was present, and its timestamp changed
		// since the last check, then the applications under app_root
		// should still be restarted. However, a subsequent get()
		// should not result in a restart.
		pid_t old_pid;
		TempDirCopy c("stub/rack", "rackapp.tmp");
		TempDir d("rackapp.tmp/tmp/restart.txt");
		Application::SessionPtr session = spawnRackApp(pool, "rackapp.tmp");
		old_pid = session->getPid();
		session.reset();
		
		touchFile("rackapp.tmp/tmp/restart.txt", 10);
		
		session = spawnRackApp(pool, "rackapp.tmp");
		ensure("The app was restarted", session->getPid() != old_pid);
		old_pid = session->getPid();
		session.reset();
		
		session = spawnRackApp(pool, "rackapp.tmp");
		ensure_equals("The app was not restarted",
			old_pid, session->getPid());
	}
	
	TEST_METHOD(15) {
		// Test whether restarting with restart.txt really results in code reload.
		TempDirCopy c("stub/rack", "rackapp.tmp");
		Application::SessionPtr session = spawnRackApp(pool, "rackapp.tmp");
		session->sendHeaders(createRequestHeaders());
		string result = readAll(session->getStream());
		ensure(result.find("hello <b>world</b>") != string::npos);
		session.reset();
		
		touchFile("rackapp.tmp/tmp/restart.txt");
		replaceStringInFile("rackapp.tmp/config.ru", "world", "world 2");
		
		session = spawnRackApp(pool, "rackapp.tmp");
		session->sendHeaders(createRequestHeaders());
		result = readAll(session->getStream());
		ensure("App code has been reloaded", result.find("hello <b>world 2</b>") != string::npos);
	}
	
	TEST_METHOD(16) {
		// If tmp/always_restart.txt is present and is a file,
		// then the application under app_root should be always restarted.
		struct stat buf;
		pid_t old_pid;
		TempDirCopy c("stub/rack", "rackapp.tmp");
		Application::SessionPtr session1 = spawnRackApp(pool, "rackapp.tmp");
		Application::SessionPtr session2 = spawnRackApp(pool2, "rackapp.tmp");
		session1.reset();
		session2.reset();
		
		touchFile("rackapp.tmp/tmp/always_restart.txt");
		
		// This get() results in a restart.
		session1 = spawnRackApp(pool, "rackapp.tmp");
		old_pid = session1->getPid();
		session1.reset();
		ensure_equals("First restart: no apps are active", pool->getActive(), 0u);
		ensure_equals("First restart: the first 2 apps were killed, and a new one was spawned",
			pool->getCount(), 1u);
		ensure("always_restart file has not been deleted",
			stat("rackapp.tmp/tmp/always_restart.txt", &buf) == 0);
		
		// This get() results in a restart as well.
		session1 = spawnRackApp(pool, "rackapp.tmp");
		ensure(old_pid != session1->getPid());
		session1.reset();
		ensure_equals("Second restart: no apps are active", pool->getActive(), 0u);
		ensure_equals("Second restart: the last app was killed, and a new one was spawned",
			pool->getCount(), 1u);
		ensure("always_restart file has not been deleted",
			stat("rackapp.tmp/tmp/always_restart.txt", &buf) == 0);
	}
	
	TEST_METHOD(17) {
		// If tmp/always_restart.txt is present and is a directory,
		// then the application under app_root should be always restarted.
		struct stat buf;
		pid_t old_pid;
		TempDirCopy c("stub/rack", "rackapp.tmp");
		Application::SessionPtr session1 = spawnRackApp(pool, "rackapp.tmp");
		Application::SessionPtr session2 = spawnRackApp(pool, "rackapp.tmp");
		session1.reset();
		session2.reset();
		
		TempDir d("rackapp.tmp/tmp/always_restart.txt");
		
		// This get() results in a restart.
		session1 = spawnRackApp(pool, "rackapp.tmp");
		old_pid = session1->getPid();
		session1.reset();
		ensure_equals("First restart: no apps are active", pool->getActive(), 0u);
		ensure_equals("First restart: the first 2 apps were killed, and a new one was spawned",
			pool->getCount(), 1u);
		ensure("always_restart directory has not been deleted",
			stat("rackapp.tmp/tmp/always_restart.txt", &buf) == 0);
		
		// This get() results in a restart as well.
		session1 = spawnRackApp(pool, "rackapp.tmp");
		ensure(old_pid != session1->getPid());
		session1.reset();
		ensure_equals("Second restart: no apps are active", pool->getActive(), 0u);
		ensure_equals("Second restart: the last app was killed, and a new one was spawned",
			pool->getCount(), 1u);
		ensure("always_restart directory has not been deleted",
			stat("rackapp.tmp/tmp/always_restart.txt", &buf) == 0);
	}
	
	TEST_METHOD(18) {
		// Test whether restarting with tmp/always_restart.txt really results in code reload.
		TempDirCopy c("stub/rack", "rackapp.tmp");
		Application::SessionPtr session = spawnRackApp(pool, "rackapp.tmp");
		session->sendHeaders(createRequestHeaders());
		string result = readAll(session->getStream());
		ensure(result.find("hello <b>world</b>") != string::npos);
		session.reset();

		touchFile("rackapp.tmp/tmp/always_restart.txt");
		replaceStringInFile("rackapp.tmp/config.ru", "world", "world 2");
		
		session = spawnRackApp(pool, "rackapp.tmp");
		session->sendHeaders(createRequestHeaders());
		result = readAll(session->getStream());
		ensure("App code has been reloaded (1)", result.find("hello <b>world 2</b>") != string::npos);
		session.reset();
		
		replaceStringInFile("rackapp.tmp/config.ru", "world 2", "world 3");
		session = spawnRackApp(pool, "rackapp.tmp");
		session->sendHeaders(createRequestHeaders());
		result = readAll(session->getStream());
		ensure("App code has been reloaded (2)", result.find("hello <b>world 3</b>") != string::npos);
		session.reset();
	}
	
	TEST_METHOD(19) {
		// If tmp/restart.txt and tmp/always_restart.txt are present, 
		// the application under app_root should still be restarted and
		// both files must be kept.
		pid_t old_pid, pid;
		struct stat buf;
		TempDirCopy c("stub/rack", "rackapp.tmp");
		Application::SessionPtr session1 = spawnRackApp(pool, "rackapp.tmp");
		Application::SessionPtr session2 = spawnRackApp(pool2, "rackapp.tmp");
		session1.reset();
		session2.reset();
		
		touchFile("rackapp.tmp/tmp/restart.txt");
		touchFile("rackapp.tmp/tmp/always_restart.txt");
		
		old_pid = spawnRackApp(pool, "rackapp.tmp")->getPid();
		ensure("always_restart.txt file has not been deleted",
			stat("rackapp.tmp/tmp/always_restart.txt", &buf) == 0);
		ensure("restart.txt file has not been deleted",
			stat("rackapp.tmp/tmp/restart.txt", &buf) == 0);
		
		pid = spawnRackApp(pool, "rackapp.tmp")->getPid();
		ensure("The app was restarted", pid != old_pid);
	}
	
	TEST_METHOD(20) {
		// It should look for restart.txt in the directory given by
		// the restartDir option, if available.
		struct stat buf;
		char path[1024];
		PoolOptions options("stub/rack");
		options.appType = "rack";
		options.restartDir = string(getcwd(path, sizeof(path))) + "/stub/rack";
		
		Application::SessionPtr session1 = pool->get(options);
		Application::SessionPtr session2 = pool2->get(options);
		session1.reset();
		session2.reset();
		
		DeleteFileEventually f("stub/rack/restart.txt");
		touchFile("stub/rack/restart.txt");
		
		pool->get(options);
		
		ensure_equals("No apps are active", pool->getActive(), 0u);
		ensure_equals("Both apps are killed, and a new one was spawned",
			pool->getCount(), 1u);
		ensure("Restart file still exists",
			stat("stub/rack/restart.txt", &buf) == 0);
	}
	
	TEST_METHOD(21) {
		// restartDir may also be a directory relative to the
		// application root.
		struct stat buf;
		PoolOptions options("stub/rack");
		options.appType = "rack";
		options.restartDir = "public";
		
		Application::SessionPtr session1 = pool->get(options);
		Application::SessionPtr session2 = pool2->get(options);
		session1.reset();
		session2.reset();
		
		DeleteFileEventually f("stub/rack/public/restart.txt");
		touchFile("stub/rack/public/restart.txt");
		
		pool->get(options);
		
		ensure_equals("No apps are active", pool->getActive(), 0u);
		ensure_equals("Both apps are killed, and a new one was spawned",
			pool->getCount(), 1u);
		ensure("Restart file still exists",
			stat("stub/rack/public/restart.txt", &buf) == 0);
	}
	
	TEST_METHOD(22) {
		// The cleaner thread should clean idle applications without crashing.
		pool->setMaxIdleTime(1);
		spawnRackApp(pool, "stub/rack");
		
		time_t begin = time(NULL);
		while (pool->getCount() == 1u && time(NULL) - begin < 10) {
			usleep(100000);
		}
		ensure_equals("App should have been cleaned up", pool->getCount(), 0u);
	}
	
	TEST_METHOD(23) {
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
		TempDirCopy c("stub/wsgi", "wsgiapp.tmp");
		ApplicationPoolPtr pool3(newPoolConnection());
		Application::SessionPtr session3 = spawnWsgiApp(pool3, "wsgiapp.tmp");
		ensure_equals(pool->getCount(), 2u);
	}
	
	TEST_METHOD(24) {
		// Application instance is shutdown after 'maxRequests' requests.
		PoolOptions options("stub/rack");
		int reader;
		pid_t originalPid;
		Application::SessionPtr session;
		
		options.appType = "rack";
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
	
	TEST_METHOD(25) {
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
	
	TEST_METHOD(26) {
		// When a previous application domain spinned down, and we touched
		// restart.txt and try to spin up a new process for this domain,
		// then any ApplicationSpawner/FrameworkSpawner processes should be
		// killed first.
		
		// TODO: to test this we first need to be able to move
		// ApplicationPoolServer in-process so that we can use mock objects
	}
	
	/*************************************/
	
#endif /* USE_TEMPLATE */
