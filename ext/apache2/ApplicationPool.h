#ifndef _PASSENGER_APPLICATION_POOL_H_
#define _PASSENGER_APPLICATION_POOL_H_

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>

#include <string>
#include <map>
#include <list>

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
	
	virtual Application::SessionPtr get(const string &appRoot, const string &user = "", const string &group = "") = 0;
	
	virtual void setMax(unsigned int max) = 0;
	virtual unsigned int getActive() const = 0;
	virtual unsigned int getCount() const = 0;
};

// TODO: document this
class StandardApplicationPool: public ApplicationPool {
private:
	typedef list<ApplicationPtr> ApplicationList;
	typedef shared_ptr<ApplicationList> ApplicationListPtr;
	typedef map<string, ApplicationListPtr> ApplicationMap;
	
	struct SharedData {
		mutex lock;
		
		ApplicationMap apps;
		unsigned int max;
		unsigned int count;
		unsigned int active;
	};
	
	typedef shared_ptr<SharedData> SharedDataPtr;
	
	struct SessionCloseCallback {
		SharedDataPtr data;
		weak_ptr<Application> app;
		
		SessionCloseCallback(SharedDataPtr data, weak_ptr<Application> app) {
			this->data = data;
			this->app = app;
		}
		
		void operator()(Application::Session &session) {
			P_TRACE("Session closed!");
			mutex::scoped_lock l(data->lock);
			ApplicationPtr app(this->app.lock());
			
			if (app != NULL) {
				ApplicationMap::iterator it(data->apps.find(app->getAppRoot()));
				if (it != data->apps.end()) {
					if (app->getSessions() == 0) {
						// TODO: make this operation constant time
						it->second->remove(app);
						it->second->push_front(app);
						P_TRACE("Moving app to front of the list");
					}
					data->active--;
				}
			}
		}
	};

	#ifdef PASSENGER_USE_DUMMY_SPAWN_MANAGER
		DummySpawnManager spawnManager;
	#else
		SpawnManager spawnManager;
	#endif
	SharedDataPtr data;
	mutex &lock;
	ApplicationMap &apps;
	unsigned int &max;
	unsigned int &count;
	unsigned int &active;
	
	bool needsRestart(const string &appRoot) const {
		return false;
	}
	
public:
	StandardApplicationPool(const string &spawnManagerCommand,
	             const string &logFile = "",
	             const string &environment = "production",
	             const string &rubyCommand = "ruby")
	        :
		#ifndef PASSENGER_USE_DUMMY_SPAWN_MANAGER
		spawnManager(spawnManagerCommand, logFile, environment, rubyCommand),
		#endif
		data(new SharedData()),
		lock(data->lock),
		apps(data->apps),
		max(data->max),
		count(data->count),
		active(data->active)
	{
		max = 100;
		count = 0;
		active = 0;
	}
	
	virtual Application::SessionPtr
	get(const string &appRoot, const string &user = "", const string &group = "") {
		ApplicationPtr app;
		mutex::scoped_lock l(lock);
		
		if (needsRestart(appRoot)) {
			apps.erase(appRoot);
		}
		
		ApplicationMap::iterator it(apps.find(appRoot));
		if (it != apps.end()) {
			P_TRACE("AppRoot already in list");
			ApplicationList &appList(*it->second);
		
			if (appList.front()->getSessions() == 0) {
				P_TRACE("First app in list has 0 sessions");
				app = appList.front();
				appList.pop_front();
				appList.push_back(app);
				active++;
			} else if (count < max) {
				P_TRACE("Spawning new");
				app = spawnManager.spawn(appRoot, user, group);
				appList.push_back(app);
				count++;
				active++;
			} else {
				P_TRACE("Queueing to existing app");
				app = appList.front();
				appList.pop_front();
				appList.push_back(app);
				active++;
			}
		} else {
			P_TRACE("AppRoot not in list");
			//wait until count < max;
			app = spawnManager.spawn(appRoot, user, group);
			ApplicationListPtr appList(new ApplicationList());
			appList->push_back(app);
			apps[appRoot] = appList;
			count++;
			active++;
		}
		return app->connect(SessionCloseCallback(data, app));
	}
	
	virtual void setMax(unsigned int max) {
		mutex::scoped_lock l(lock);
		this->max = max;
	}
	
	virtual unsigned int getActive() const {
		return active;
	}
	
	virtual unsigned int getCount() const {
		return count;
	}
};

typedef shared_ptr<ApplicationPool> ApplicationPoolPtr;

}; // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_H_ */
