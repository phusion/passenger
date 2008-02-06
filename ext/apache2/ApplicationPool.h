#ifndef _PASSENGER_APPLICATION_POOL_H_
#define _PASSENGER_APPLICATION_POOL_H_

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/bind.hpp>

#include <string>
#include <map>
#include <list>

#include <time.h>

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
	static const int CLEAN_INTERVAL = 62;
	static const int MAX_IDLE_TIME = 60;

	typedef list<ApplicationPtr> ApplicationList;
	typedef shared_ptr<ApplicationList> ApplicationListPtr;
	typedef map<string, ApplicationListPtr> ApplicationMap;
	
	struct SharedData {
		mutex lock;
		condition countOrMaxChanged;
		
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
			mutex::scoped_lock l(data->lock);
			ApplicationPtr app(this->app.lock());
			
			if (app != NULL) {
				ApplicationMap::iterator it(data->apps.find(app->getAppRoot()));
				if (it != data->apps.end()) {
					if (app->getSessions() == 0) {
						// TODO: make this operation constant time
						it->second->remove(app);
						it->second->push_front(app);
					}
					data->active--;
				}
				app->setLastUsed(time(NULL));
			}
		}
	};

	#ifdef PASSENGER_USE_DUMMY_SPAWN_MANAGER
		DummySpawnManager spawnManager;
	#else
		SpawnManager spawnManager;
	#endif
	SharedDataPtr data;
	thread *cleanerThread;
	bool done;
	condition cleanerThreadSleeper;
	
	mutex &lock;
	condition &countOrMaxChanged;
	ApplicationMap &apps;
	unsigned int &max;
	unsigned int &count;
	unsigned int &active;
	
	bool needsRestart(const string &appRoot) const {
		return false;
	}
	
	void cleanerThreadMainLoop() {
		while (!done) {
			mutex::scoped_lock l(lock);
			
			xtime xt;
			xtime_get(&xt, TIME_UTC);
			xt.sec += CLEAN_INTERVAL;
			cleanerThreadSleeper.timed_wait(l, xt);
			if (done) {
				break;
			}
			
			ApplicationMap::iterator appsIter;
			time_t now = time(NULL);
			for (appsIter = apps.begin(); appsIter != apps.end(); appsIter++) {
				ApplicationList &appList(*appsIter->second);
				ApplicationList::iterator listIter;
				list<ApplicationList::iterator> elementsToRemove;
				
				for (listIter = appList.begin(); listIter != appList.end(); listIter++) {
					Application &app(**listIter);
					if (now - app.getLastUsed() > MAX_IDLE_TIME) {
						P_TRACE("Cleaning idle app " << app.getAppRoot());
						elementsToRemove.push_back(listIter);
					}
				}
				
				list<ApplicationList::iterator>::iterator it;
				for (it = elementsToRemove.begin(); it != elementsToRemove.end(); it++) {
					appList.erase(*it);
				}
			}
		}
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
		countOrMaxChanged(data->countOrMaxChanged),
		apps(data->apps),
		max(data->max),
		count(data->count),
		active(data->active)
	{
		done = false;
		max = 100;
		count = 0;
		active = 0;
		cleanerThread = new thread(bind(&StandardApplicationPool::cleanerThreadMainLoop, this));
	}
	
	virtual ~StandardApplicationPool() {
		done = true;
		{
			mutex::scoped_lock l(lock);
			cleanerThreadSleeper.notify_one();
		}
		cleanerThread->join();
		delete cleanerThread;
	}
	
	virtual Application::SessionPtr
	get(const string &appRoot, const string &user = "", const string &group = "") {
		/*
		 * See "doc/ApplicationPool Algorithm.txt" for a more readable description
		 * of the algorithm.
		 */
		ApplicationPtr app;
		mutex::scoped_lock l(lock);
		
		if (needsRestart(appRoot)) {
			apps.erase(appRoot);
		}
		
		ApplicationMap::iterator it(apps.find(appRoot));
		if (it != apps.end()) {
			ApplicationList &appList(*it->second);
		
			if (appList.front()->getSessions() == 0) {
				app = appList.front();
				appList.pop_front();
				appList.push_back(app);
				active++;
			} else if (count < max) {
				app = spawnManager.spawn(appRoot, user, group);
				appList.push_back(app);
				count++;
				countOrMaxChanged.notify_all();
				active++;
			} else {
				app = appList.front();
				appList.pop_front();
				appList.push_back(app);
				active++;
			}
		} else {
			while (count >= max) {
				countOrMaxChanged.wait(l);
			}
			app = spawnManager.spawn(appRoot, user, group);
			ApplicationListPtr appList(new ApplicationList());
			appList->push_back(app);
			apps[appRoot] = appList;
			count++;
			countOrMaxChanged.notify_all();
			active++;
		}
		app->setLastUsed(time(NULL));
		return app->connect(SessionCloseCallback(data, app));
	}
	
	virtual void setMax(unsigned int max) {
		mutex::scoped_lock l(lock);
		this->max = max;
		countOrMaxChanged.notify_all();
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
