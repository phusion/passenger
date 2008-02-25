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

#include <ctime>

#ifdef PASSENGER_USE_DUMMY_SPAWN_MANAGER
	#include "DummySpawnManager.h"
#else
	#include "SpawnManager.h"
#endif

namespace Passenger {

using namespace std;
using namespace boost;

/**
 * A persistent pool of Applications.
 *
 * Spawning Ruby on Rails application instances is a very expensive operation.
 * Despite best efforts to make the operation less expensive (see SpawnManager),
 * it remains expensive compared to the cost of processing an HTTP request/response.
 * So, in order to solve this, some sort of caching/pooling mechanism will be required.
 * ApplicationPool provides this.
 *
 * Normally, one would use SpawnManager to spawn a new RoR application instance,
 * then use Application::connect() to create a new session with that application
 * instance, and then use the returned Session object to send the request and
 * to read the HTTP response. ApplicationPool replaces the first step with
 * a call to Application::get(). For example:
 * @code
 *   ApplicationPool pool = some_function_which_creates_an_application_pool();
 *   
 *   // Connect to the application and get the newly opened session.
 *   Application::SessionPtr session(pool->get("/home/webapps/foo"));
 *   
 *   // Send the request headers and request body data.
 *   session->sendHeaders(...);
 *   session->sendBodyBlock(...);
 *   // Done sending data, so we close the writer channel.
 *   session->closeWriter();
 *
 *   // Now read the HTTP response.
 *   string responseData = readAllDataFromSocket(session->getReader());
 *   // Done reading data, so we close the reader channel.
 *   session->closeReader();
 *
 *   // This session has now finished, so we close the session by resetting
 *   // the smart pointer to NULL (thereby destroying the Session object).
 *   session.reset();
 *
 *   // We can connect to an Application multiple times. Just make sure
 *   // the previous session is closed.
 *   session = app->connect("/home/webapps/bar")
 * @endcode
 *
 * Internally, ApplicationPool::get() will keep spawned applications instances in
 * memory, and reuse them if possible. It will try to keep spawning to a minimum.
 * Furthermore, if an application instance hasn't been used for a while, it
 * will be automatically shutdown in order to save memory. Restart requests are
 * honored: if an application has the file 'restart.txt' in its 'tmp' folder,
 * then get() will shutdown existing instances of that application and spawn
 * a new instance (this is useful when a new version of an application has been
 * deployed). And finally, one can set a hard limit on the maximum number of
 * applications instances that may be spawned (see ApplicationPool::setMax()).
 *
 * Note that ApplicationPool is just an interface (i.e. a pure virtual class).
 * For concrete classes, see StandardApplicationPool and ApplicationPoolServer.
 * The exact pooling algorithm depends on the implementation class.
 *
 * @ingroup Support
 */
class ApplicationPool {
public:
	virtual ~ApplicationPool() {};
	
	/**
	 * Open a new session with the application specified by <tt>appRoot</tt>.
	 * See the class description for ApplicationPool, as well as Application::connect(),
	 * on how to use the returned session object.
	 *
	 * Internally, this method may either spawn a new application instance, or use
	 * an existing one.
	 *
	 * If <tt>lowerPrivilege</tt> is true, then any newly spawned application
	 * instances will have lower privileges. See SpawnManager::SpawnManager()'s
	 * description of <tt>lowerPrivilege</tt> and <tt>lowestUser</tt> for details.
	 *
	 * @param appRoot The application root of a RoR application, i.e. the folder that
	 *             contains 'app/', 'public/', 'config/', etc. This must be a valid
	 *             directory, but does not have to be an absolute path.
	 * @param lowerPrivilege Whether to lower the application's privileges.
	 * @param lowestUser The user to fallback to if lowering privilege fails.
	 * @return A session object.
	 * @throw SpawnException An attempt was made to spawn a new application instance, but that attempt failed.
	 * @throw IOException Something else went wrong.
	 * @note Applications are uniquely identified with the application root
	 *       string. So although <tt>appRoot</tt> does not have to be absolute, it
	 *       should be. If one calls <tt>get("/home/foo")</tt> and
	 *       <tt>get("/home/../home/foo")</tt>, then ApplicationPool will think
	 *       they're 2 different applications, and thus will spawn 2 application instances.
	 */
	virtual Application::SessionPtr get(const string &appRoot, bool lowerPrivilege = true, const string &lowestUser = "nobody") = 0;
	
	/**
	 * Set a hard limit on the number of application instances that this ApplicationPool
	 * may spawn. The exact behavior depends on the used algorithm, and is not specified by
	 * these API docs.
	 *
	 * It is allowed to set a limit lower than the current number of spawned applications.
	 */
	virtual void setMax(unsigned int max) = 0;
	
	/**
	 * Get the number of active applications in the pool.
	 *
	 * This method exposes an implementation detail of the underlying pooling algorithm.
	 * It is used by unit tests to verify that the implementation is correct,
	 * and thus should not be called directly.
	 */
	virtual unsigned int getActive() const = 0;
	
	/**
	 * Get the number of active applications in the pool.
	 *
	 * This method exposes an implementation detail of the underlying pooling algorithm.
	 * It is used by unit tests to verify that the implementation is correct,
	 * and thus should not be called directly.
	 */
	virtual unsigned int getCount() const = 0;
	
	/**
	 * Get the process ID of the spawn server that is used.
	 *
	 * This method exposes an implementation detail. It is used by unit tests to verify
	 * that the implementation is correct, and thus should not be used directly.
	 */
	virtual pid_t getSpawnServerPid() const = 0;
};

class ApplicationPoolServer;

/**
 * A standard implementation of ApplicationPool for single-process environments.
 *
 * The environment may or may not be multithreaded - StandardApplicationPool is completely
 * thread-safe. Apache with the threading MPM is an example of a multithreaded single-process
 * environment.
 *
 * This class is unusable in multi-process environments such as Apache with the prefork MPM.
 * The reasons are as follows:
 *  - StandardApplicationPool uses threads internally. Because threads disappear after a fork(),
 *    a StandardApplicationPool object will become unusable after a fork().
 *  - StandardApplicationPool stores its internal cache on the heap. Different processes
 *    cannot share their heaps, so they will not be able to access each others' pool cache.
 *  - StandardApplicationPool has a connection to the spawn server. If there are multiple
 *    processes, and they all use the spawn servers's connection at the same time without
 *    some sort of synchronization, then bad things will happen.
 *
 * (Of course, StandardApplicationPool <em>is</em> usable if each process creates its own
 * StandardApplicationPool object, but that would defeat the point of having a shared pool.)
 *
 * For multi-process environments, one should use ApplicationPoolServer instead.
 *
 * @ingroup Support
 */
class StandardApplicationPool: public ApplicationPool {
private:
	static const int CLEAN_INTERVAL = 62;
	static const int MAX_IDLE_TIME = 60;

	friend class ApplicationPoolServer;
	struct AppContainer;
	
	typedef shared_ptr<AppContainer> AppContainerPtr;
	typedef list<AppContainerPtr> AppContainerList;
	typedef shared_ptr<AppContainerList> AppContainerListPtr;
	typedef map<string, AppContainerListPtr> ApplicationMap;
	
	struct AppContainer {
		ApplicationPtr app;
		time_t lastUsed;
		unsigned int sessions;
		AppContainerList::iterator iterator;
		AppContainerList::iterator ia_iterator;
	};
	
	struct SharedData {
		mutex lock;
		condition activeOrMaxChanged;
		
		ApplicationMap apps;
		unsigned int max;
		unsigned int count;
		unsigned int active;
		AppContainerList inactiveApps;
		map<string, time_t> restartFileTimes;
	};
	
	typedef shared_ptr<SharedData> SharedDataPtr;
	
	struct SessionCloseCallback {
		SharedDataPtr data;
		weak_ptr<AppContainer> container;
		
		SessionCloseCallback(SharedDataPtr data,
		                     const weak_ptr<AppContainer> &container) {
			this->data = data;
			this->container = container;
		}
		
		void operator()() {
			mutex::scoped_lock l(data->lock);
			AppContainerPtr container(this->container.lock());
			
			if (container == NULL) {
				return;
			}
			
			ApplicationMap::iterator it;
			it = data->apps.find(container->app->getAppRoot());
			if (it != data->apps.end()) {
				AppContainerListPtr list(it->second);
				container->lastUsed = time(NULL);
				container->sessions--;
				if (container->sessions == 0) {
					list->erase(container->iterator);
					list->push_front(container);
					container->iterator = list->begin();
					data->inactiveApps.push_back(container);
					container->ia_iterator = data->inactiveApps.end();
					container->ia_iterator--;
				}
				data->active--;
				data->activeOrMaxChanged.notify_all();
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
	bool detached;
	bool done;
	condition cleanerThreadSleeper;
	
	// Shortcuts to save typing in get().
	mutex &lock;
	condition &activeOrMaxChanged;
	ApplicationMap &apps;
	unsigned int &max;
	unsigned int &count;
	unsigned int &active;
	AppContainerList &inactiveApps;
	map<string, time_t> &restartFileTimes;
	
	bool needsRestart(const string &appRoot) const {
		return false;
	}
	
	void cleanerThreadMainLoop() {
		mutex::scoped_lock l(lock);
		while (!done) {
			xtime xt;
			xtime_get(&xt, TIME_UTC);
			xt.sec += CLEAN_INTERVAL;
			cleanerThreadSleeper.timed_wait(l, xt);
			if (done) {
				break;
			}
			
			time_t now = time(NULL);
			AppContainerList::iterator it;
			for (it = inactiveApps.begin(); it != inactiveApps.end(); it++) {
				AppContainer &container(*it->get());
				ApplicationPtr app(container.app);
				AppContainerListPtr appList(apps[app->getAppRoot()]);
				
				if (now - container.lastUsed > MAX_IDLE_TIME) {
					P_TRACE("Cleaning idle app " << app->getAppRoot());
					appList->erase(container.iterator);
					inactiveApps.erase(it);
					count--;
				}
				if (appList->empty()) {
					apps.erase(app->getAppRoot());
					data->restartFileTimes.erase(app->getAppRoot());
				}
			}
		}
	}
	
	void detach() {
		detached = true;
	}
	
	void handleConnectException(const string &exceptionMessage,
	              const AppContainerPtr &container,
	              AppContainerList &list) {
		// TODO: merge this back into the algorithm description
		string message("Cannot connect to an existing application instance for '");
		message.append(container->app->getAppRoot());
		message.append("': ");
		message.append(exceptionMessage);
		
		container->sessions--;
		list.erase(container->iterator);
		if (list.empty()) {
			apps.erase(container->app->getAppRoot());
			restartFileTimes.erase(container->app->getAppRoot());
		}
		count--;
		active--;
		activeOrMaxChanged.notify_all();
		throw IOException(message);
	}
	
public:
	/**
	 * Create a new StandardApplicationPool object.
	 *
	 * @param spawnServerCommand The filename of the spawn server to use.
	 * @param logFile Specify a log file that the spawn server should use.
	 *            Messages on its standard output and standard error channels
	 *            will be written to this log file. If an empty string is
	 *            specified, no log file will be used, and the spawn server
	 *            will use the same standard output/error channels as the
	 *            current process.
	 * @param environment The RAILS_ENV environment that all RoR applications
	 *            should use. If an empty string is specified, the current value
	 *            of the RAILS_ENV environment variable will be used.
	 * @param rubyCommand The Ruby interpreter's command.
	 * @throws SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException The specified log file could not be opened.
	 */
	StandardApplicationPool(const string &spawnServerCommand,
	             const string &logFile = "",
	             const string &environment = "production",
	             const string &rubyCommand = "ruby")
	        :
		#ifndef PASSENGER_USE_DUMMY_SPAWN_MANAGER
		spawnManager(spawnServerCommand, logFile, environment, rubyCommand),
		#endif
		data(new SharedData()),
		lock(data->lock),
		activeOrMaxChanged(data->activeOrMaxChanged),
		apps(data->apps),
		max(data->max),
		count(data->count),
		active(data->active),
		inactiveApps(data->inactiveApps),
		restartFileTimes(data->restartFileTimes)
	{
		detached = false;
		done = false;
		max = 100;
		count = 0;
		active = 0;
		cleanerThread = new thread(bind(&StandardApplicationPool::cleanerThreadMainLoop, this));
	}
	
	virtual ~StandardApplicationPool() {
		if (!detached) {
			{
				mutex::scoped_lock l(lock);
				done = true;
				cleanerThreadSleeper.notify_one();
			}
			cleanerThread->join();
		}
		delete cleanerThread;
	}
	
	virtual Application::SessionPtr
	get(const string &appRoot, bool lowerPrivilege = true, const string &lowestUser = "nobody") {
		/*
		 * See "doc/ApplicationPool Algorithm.txt" for a more readable description
		 * of the algorithm.
		 */
		AppContainerPtr container;
		AppContainerList *list;
		mutex::scoped_lock l(lock);
		
		try {
			ApplicationMap::iterator it(apps.find(appRoot));
			if (it != apps.end()) {
				list = it->second.get();
		
				if (list->front()->sessions == 0 || count == max) {
					if (needsRestart(appRoot)) {
						// ...
					} else {
						container = list->front();
						list->pop_front();
						list->push_back(container);
						if (container->sessions == 0) {
							inactiveApps.erase(container->ia_iterator);
						}
						active++;
					}
				}
				
				if (container == NULL) {
					container = ptr(new AppContainer());
					container->app = spawnManager.spawn(appRoot,
						lowerPrivilege, lowestUser);
					container->sessions = 0;
					list->push_back(container);
					container->iterator = list->end();
					container->iterator--;
					count++;
					active++;
					activeOrMaxChanged.notify_all();
				}
			} else {
				while (active >= max) {
					activeOrMaxChanged.wait(l);
				}
				if (count == max) {
					container = inactiveApps.front();
					inactiveApps.pop_front();
					list = apps[container->app->getAppRoot()].get();
					list->erase(container->iterator);
					if (list->empty()) {
						apps.erase(container->app->getAppRoot());
						restartFileTimes.erase(container->app->getAppRoot());
					}
					count--;
				}
				container = ptr(new AppContainer());
				container->app = spawnManager.spawn(appRoot, lowerPrivilege, lowestUser);
				container->sessions = 0;
				list = new AppContainerList();
				list->push_back(container);
				container->iterator = list->end();
				container->iterator--;
				apps[appRoot] = ptr(list);
				count++;
				active++;
				activeOrMaxChanged.notify_all();
			}
		} catch (const SpawnException &e) {
			string message("Cannot spawn application '");
			message.append(appRoot);
			message.append("': ");
			message.append(e.what());
			throw SpawnException(message);
		}
		
		container->lastUsed = time(NULL);
		container->sessions++;
		
		// TODO: This should not just throw an exception.
		// If we fail to connect to one application we should just use another, or
		// spawn a new application. Of course, this must not be done too often
		// because every app might crash at startup. There should be a limit
		// to the number of retries.
		try {
			return container->app->connect(SessionCloseCallback(data, container));
		} catch (const IOException &e) {
			handleConnectException(e.what(), container, *list);
		} catch (const SystemException &e) {
			handleConnectException(e.sys(), container, *list);
		}
		// Never reached; shut up compiler warning
		return Application::SessionPtr();
	}
	
	virtual void setMax(unsigned int max) {
		mutex::scoped_lock l(lock);
		this->max = max;
		activeOrMaxChanged.notify_all();
	}
	
	virtual unsigned int getActive() const {
		return active;
	}
	
	virtual unsigned int getCount() const {
		return count;
	}
	
	virtual pid_t getSpawnServerPid() const {
		return spawnManager.getServerPid();
	}
};

typedef shared_ptr<ApplicationPool> ApplicationPoolPtr;

}; // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_H_ */
