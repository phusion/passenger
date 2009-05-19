#include "tut.h"
#include "ApplicationPoolServer.h"
#include "Utils.h"
#include <cstring>
#include <unistd.h>
#include <errno.h>

using namespace Passenger;

namespace tut {
	struct ApplicationPoolServerTest {
		ApplicationPoolServerPtr server;
		ApplicationPoolPtr pool, pool2;
		
		ApplicationPoolServerTest() {
			server = ptr(new ApplicationPoolServer(
				"./ApplicationPoolServerExecutable",
				"stub/spawn_server.rb"));
		}
	};

	DEFINE_TEST_GROUP(ApplicationPoolServerTest);

	TEST_METHOD(1) {
		// Constructor and destructor should not crash or block indefinitely.
		// (And yes, this test method is intended to be blank.)
	}
	
	TEST_METHOD(2) {
		// Connecting to the ApplicationPoolServer, as well as destroying the
		// returned ApplicationPool object, should not crash.
		server->connect();
	}
	
	/* A StringListCreator which not only returns a dummy value, but also
	 * increments a counter each time getItems() is called. */
	class DummyStringListCreator: public StringListCreator {
	public:
		mutable int counter;
		
		DummyStringListCreator() {
			counter = 0;
		}
		
		virtual const StringListPtr getItems() const {
			StringListPtr result = ptr(new StringList());
			counter++;
			result->push_back("hello");
			result->push_back("world");
			return result;
		}
	};
	
	TEST_METHOD(5) {
		// When calling get() with a PoolOptions object,
		// options.environmentVariables->getItems() isn't called unless
		// the pool had to spawn something.
		ApplicationPoolServerPtr server = ptr(new ApplicationPoolServer(
			"./ApplicationPoolServerExecutable",
			"../bin/passenger-spawn-server"));
		ApplicationPoolPtr pool = server->connect();
		
		shared_ptr<DummyStringListCreator> strList = ptr(new DummyStringListCreator());
		PoolOptions options("stub/rack");
		options.appType = "rack";
		options.environmentVariables = strList;
		
		Application::SessionPtr session1 = pool->get(options);
		session1.reset();
		ensure_equals(strList->counter, 1);
		
		session1 = pool->get(options);
		session1.reset();
		ensure_equals(strList->counter, 1);
	}
}

