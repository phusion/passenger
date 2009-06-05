#include "tut.h"

#include <boost/bind.hpp>

#include "StandardApplicationPool.h"
#include "ApplicationPoolServer.h"
#include "ApplicationPoolClient.h"
#include "Utils.h"
#include <string>
#include <cstring>
#include <unistd.h>
#include <errno.h>

using namespace Passenger;
using namespace boost;
using namespace std;

namespace tut {
	struct ApplicationPoolServerTest {
		shared_ptr<StandardApplicationPool> realPool;
		shared_ptr<ApplicationPoolServer> server;
		shared_ptr<ApplicationPoolClient> pool, pool2;
		shared_ptr<oxt::thread> serverThread;
		
		~ApplicationPoolServerTest() {
			if (serverThread != NULL) {
				serverThread->interrupt_and_join();
			}
		}
		
		void initializePool() {
			string socketFilename = getPassengerTempDir() + "/master/pool_server.sock";
			realPool = ptr(new StandardApplicationPool("../bin/passenger-spawn-server"));
			server = ptr(new ApplicationPoolServer(socketFilename, "12345", realPool));
			pool = ptr(new ApplicationPoolClient(socketFilename, "12345"));
			pool2 = ptr(new ApplicationPoolClient(socketFilename, "12345"));
			serverThread = ptr(new oxt::thread(
				boost::bind(&ApplicationPoolServer::mainLoop, server.get())
			));
		}
	};

	DEFINE_TEST_GROUP(ApplicationPoolServerTest);
	
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
	
	TEST_METHOD(1) {
		// When calling get() with a PoolOptions object,
		// options.environmentVariables->getItems() isn't called unless
		// the pool had to spawn something.
		initializePool();
		
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

