#ifndef _PASSENGER_APPLICATION_POOL_H_
#define _PASSENGER_APPLICATION_POOL_H_

#include <boost/shared_ptr.hpp>
#include <map>

#include "Application.h"
#include "SpawnManager.h"

namespace Passenger {

using namespace std;
using namespace boost;

// TODO: document this
class ApplicationPool {
private:
	typedef map<string, ApplicationPtr> ApplicationMap;

	SpawnManager spawnManager;
	ApplicationMap apps;
	
	string normalizePath(const string &path) {
		// TODO
		return path;
	}
	
public:
	ApplicationPool(const string &spawnManagerCommand,
	                const string &logFile = "",
	                const string &environment = "production",
	                const string &rubyCommand = "ruby")
	: spawnManager(spawnManagerCommand, logFile, environment, rubyCommand) {}
	
	ApplicationPtr get(const string &appRoot) {
		return get(appRoot, "");
	}
	
	// TODO: improve algorithm
	// TODO: make thread-safe
	// TODO: make it possible to share an ApplicationPool between processes
	ApplicationPtr get(const string &appRoot, const string &username) {
		string normalizedAppRoot(normalizePath(appRoot));
		//scoped_lock l(lock);
		
		ApplicationPtr app;
		//ApplicationMap::iterator it(apps.find(appRoot));
		//if (it == apps.end()) {
			app = spawnManager.spawn(appRoot, username);
		//	apps[appRoot] = app;
		//} else {
		//	app = it->second;
		//}
		return app;
	}
};

typedef shared_ptr<ApplicationPool> ApplicationPoolPtr;

}; // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_H_ */
