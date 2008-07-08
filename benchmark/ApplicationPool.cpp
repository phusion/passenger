//#define USE_SERVER

#include <iostream>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#ifdef USE_SERVER
	#include "ApplicationPoolServer.h"
#else
	#include "StandardApplicationPool.h"
#endif
#include "Utils.h"
#include "Logging.h"

using namespace std;
using namespace boost;
using namespace Passenger;

#define TRANSACTIONS 20000
#define CONCURRENCY 24

ApplicationPoolPtr pool;

static void
threadMain(unsigned int times) {
	for (unsigned int i = 0; i < times; i++) {
		Application::SessionPtr session(pool->get("test/stub/minimal-railsapp"));
		for (int x = 0; x < 200000; x++) {
			// Do nothing.
		}
	}
}

int
main() {
	thread_group tg;
	#ifdef USE_SERVER
		ApplicationPoolServer server("ext/apache2/ApplicationPoolServerExecutable",
			"bin/passenger-spawn-server");
		pool = server.connect();
	#else
		pool = ptr(new StandardApplicationPool("bin/passenger-spawn-server"));
	#endif
	pool->setMax(6);
	
	for (int i = 0; i < CONCURRENCY; i++) {
		tg.create_thread(boost::bind(&threadMain, TRANSACTIONS / CONCURRENCY));
	}
	
	tg.join_all();
	return 0;
}

