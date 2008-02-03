#ifndef _PASSENGER_APPLICATION_POOL_H_
#define _PASSENGER_APPLICATION_POOL_H_

#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>

#include <string>
#include <map>

#ifdef PASSENGER_USE_DUMMY_SPAWN_MANAGER
	#include "DummySpawnManager.h"
#else
	#include "SpawnManager.h"
#endif

namespace Passenger {

using namespace std;
using namespace boost;

class ApplicationPool {
public:
	virtual ~ApplicationPool() {};
	
	virtual pair<ApplicationPtr, Application::LockPtr>
	get(const string &appRoot, const string &user = "", const string &group = "") = 0;
};

// TODO: document this
class StandardApplicationPool: public ApplicationPool {
private:
	typedef map<string, ApplicationPtr> ApplicationMap;

	#ifdef PASSENGER_USE_DUMMY_SPAWN_MANAGER
		DummySpawnManager spawnManager;
	#else
		SpawnManager spawnManager;
	#endif
	ApplicationMap apps;
	mutex lock;
	bool threadSafe;
	
public:
	StandardApplicationPool(const string &spawnManagerCommand,
	             const string &logFile = "",
	             const string &environment = "production",
	             const string &rubyCommand = "ruby")
	#ifndef PASSENGER_USE_DUMMY_SPAWN_MANAGER
		: spawnManager(spawnManagerCommand, logFile, environment, rubyCommand)
	#endif
	{
		threadSafe = false;
	}
	
	void setThreadSafe() {
		threadSafe = true;
	}
	
	// TODO: improve algorithm
	virtual pair<ApplicationPtr, Application::LockPtr>
	get(const string &appRoot, const string &user = "", const string &group = "") {
		ApplicationPtr app;
		mutex::scoped_lock l(lock, threadSafe);
		
		ApplicationMap::iterator it(apps.find(appRoot));
		if (it == apps.end()) {
			app = spawnManager.spawn(appRoot, user, group);
			apps[appRoot] = app;
		} else {
			app = it->second;
			if (app->hasError()) {
				app = spawnManager.spawn(appRoot, user, group);
				apps[appRoot] = app;
			}
		}
		return make_pair(app, app->openSession());
	}
};

typedef shared_ptr<ApplicationPool> ApplicationPoolPtr;

}; // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_H_ */
