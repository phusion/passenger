#include "tut.h"

#include <boost/bind.hpp>

#include "ApplicationPool/Pool.h"
#include "ApplicationPool/Server.h"
#include "ApplicationPool/Client.h"
#include "Utils.h"
#include <string>
#include <cstring>
#include <unistd.h>
#include <errno.h>

using namespace Passenger;
using namespace boost;
using namespace std;

namespace tut {
	struct ApplicationPool_ServerTest {
		ApplicationPool::AccountsDatabasePtr accountsDatabase;
		shared_ptr<ApplicationPool::Pool> realPool;
		shared_ptr<ApplicationPool::Server> server;
		shared_ptr<ApplicationPool::Client> pool, pool2;
		shared_ptr<oxt::thread> serverThread;
		
		~ApplicationPool_ServerTest() {
			if (serverThread != NULL) {
				serverThread->interrupt_and_join();
			}
		}
		
		void initializePool() {
			string socketFilename = getPassengerTempDir() + "/master/pool_server.sock";
			accountsDatabase = ptr(new ApplicationPool::AccountsDatabase());
			accountsDatabase->add("test", "12345", false);
			
			realPool = ptr(new ApplicationPool::Pool("../bin/passenger-spawn-server"));
			server   = ptr(new ApplicationPool::Server(socketFilename, accountsDatabase, realPool));
			pool     = ptr(new ApplicationPool::Client(socketFilename, "test", "12345"));
			pool2    = ptr(new ApplicationPool::Client(socketFilename, "test", "12345"));
			serverThread = ptr(new oxt::thread(
				boost::bind(&ApplicationPool::Server::mainLoop, server.get())
			));
		}
	};

	DEFINE_TEST_GROUP(ApplicationPool_ServerTest);
	
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

