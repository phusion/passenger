/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _PASSENGER_STANDARD_APPLICATION_POOL_H_
#define _PASSENGER_STANDARD_APPLICATION_POOL_H_

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/microsec_time_clock.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <string>
#include <sstream>
#include <map>
#include <list>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <ctime>
#include <cerrno>
#ifdef TESTING_APPLICATION_POOL
	#include <cstdlib>
#endif

#include "ApplicationPool.h"
#include "Logging.h"
#include "System.h"
#ifdef PASSENGER_USE_DUMMY_SPAWN_MANAGER
	#include "DummySpawnManager.h"
#else
	#include "SpawnManager.h"
#endif

namespace Passenger {

using namespace std;
using namespace boost;

class ApplicationPoolServer;

/****************************************************************
 *
 *  See "doc/ApplicationPool algorithm.txt" for a more readable
 *  and detailed description of the algorithm implemented here.
 *
 ****************************************************************/

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
	static const int DEFAULT_MAX_IDLE_TIME = 120;
	static const int DEFAULT_MAX_POOL_SIZE = 20;
	static const int DEFAULT_MAX_INSTANCES_PER_APP = 0;
	static const int CLEANER_THREAD_STACK_SIZE = 1024 * 128;
	static const unsigned int MAX_GET_ATTEMPTS = 10;
	static const unsigned int GET_TIMEOUT = 5000; // In milliseconds.

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
		mutex waitingLock;
		condition activeOrMaxChanged;
		
		ApplicationMap apps;
		unsigned int max;
		unsigned int count;
		unsigned int active;
		unsigned int waiting;
		unsigned int maxPerApp;
		AppContainerList inactiveApps;
		map<string, time_t> restartFileTimes;
		map<string, unsigned int> appInstanceCount;
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
	unsigned int maxIdleTime;
	condition cleanerThreadSleeper;
	
	// Shortcuts for instance variables in SharedData. Saves typing in get().
	mutex &lock;
	mutex &waitingLock;
	condition &activeOrMaxChanged;
	ApplicationMap &apps;
	unsigned int &max;
	unsigned int &count;
	unsigned int &active;
	unsigned int &waiting;
	unsigned int &maxPerApp;
	AppContainerList &inactiveApps;
	map<string, time_t> &restartFileTimes;
	map<string, unsigned int> &appInstanceCount;
	
	/**
	 * Verify that all the invariants are correct.
	 */
	bool inline verifyState() {
	#if PASSENGER_DEBUG
		// Invariant for _apps_.
		ApplicationMap::const_iterator it;
		for (it = apps.begin(); it != apps.end(); it++) {
			AppContainerList *list = it->second.get();
			P_ASSERT(!list->empty(), false, "List for '" << it->first << "' is nonempty.");
			
			AppContainerList::const_iterator prev_lit;
			AppContainerList::const_iterator lit;
			prev_lit = list->begin();
			lit = prev_lit;
			lit++;
			for (; lit != list->end(); lit++) {
				if ((*prev_lit)->sessions > 0) {
					P_ASSERT((*lit)->sessions > 0, false,
						"List for '" << it->first <<
						"' is sorted from nonactive to active");
				}
			}
		}
		
		P_ASSERT(active <= count, false,
			"active (" << active << ") < count (" << count << ")");
	#endif
		return true;
	}
	
	template<typename LockActionType>
	string toString(LockActionType lockAction) const {
		unique_lock<mutex> l(lock, lockAction);
		stringstream result;
		
		result << "----------- General information -----------" << endl;
		result << "max    = " << max << endl;
		result << "count  = " << count << endl;
		result << "active = " << active << endl;
		result << "waiting = " << waiting << endl;
		result << endl;
		
		result << "----------- Applications -----------" << endl;
		ApplicationMap::const_iterator it;
		for (it = apps.begin(); it != apps.end(); it++) {
			AppContainerList *list = it->second.get();
			AppContainerList::const_iterator lit;
			
			result << it->first << ": " << endl;
			for (lit = list->begin(); lit != list->end(); lit++) {
				AppContainer *container = lit->get();
				char buf[128];
				
				snprintf(buf, sizeof(buf), "PID: %-8d  Sessions: %d",
					container->app->getPid(), container->sessions);
				result << "  " << buf << endl;
			}
			result << endl;
		}
		return result.str();
	}
	
	bool needsRestart(const string &appRoot) {
		string restartFile(appRoot);
		restartFile.append("/tmp/restart.txt");
		
		struct stat buf;
		bool result;
		int ret;
		
		do {
			ret = stat(restartFile.c_str(), &buf);
		} while (ret == -1 && errno == EINTR);
		if (ret == 0) {
			do {
				ret = unlink(restartFile.c_str());
			} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
			if (ret == 0 || errno == ENOENT) {
				restartFileTimes.erase(appRoot);
				result = true;
			} else {
				map<string, time_t>::const_iterator it;
				
				it = restartFileTimes.find(appRoot);
				if (it == restartFileTimes.end()) {
					result = true;
				} else {
					result = buf.st_mtime != restartFileTimes[appRoot];
				}
				restartFileTimes[appRoot] = buf.st_mtime;
			}
		} else {
			restartFileTimes.erase(appRoot);
			result = false;
		}
		return result;
	}
	
	void cleanerThreadMainLoop() {
		this_thread::disable_syscall_interruption dsi;
		unique_lock<mutex> l(lock);
		try {
			while (!done && !this_thread::interruption_requested()) {
				xtime xt;
				xtime_get(&xt, TIME_UTC);
				xt.sec += maxIdleTime + 1;
				if (cleanerThreadSleeper.timed_wait(l, xt)) {
					// Condition was woken up.
					if (done) {
						// StandardApplicationPool is being destroyed.
						break;
					} else {
						// maxIdleTime changed.
						continue;
					}
				}
				
				time_t now = InterruptableCalls::time(NULL);
				AppContainerList::iterator it;
				for (it = inactiveApps.begin(); it != inactiveApps.end(); it++) {
					AppContainer &container(*it->get());
					ApplicationPtr app(container.app);
					AppContainerListPtr appList(apps[app->getAppRoot()]);
					
					if (now - container.lastUsed > (time_t) maxIdleTime) {
						P_DEBUG("Cleaning idle app " << app->getAppRoot() <<
							" (PID " << app->getPid() << ")");
						appList->erase(container.iterator);
						
						AppContainerList::iterator prev = it;
						prev--;
						inactiveApps.erase(it);
						it = prev;
						
						appInstanceCount[app->getAppRoot()]--;
						
						count--;
					}
					if (appList->empty()) {
						apps.erase(app->getAppRoot());
						appInstanceCount.erase(app->getAppRoot());
						data->restartFileTimes.erase(app->getAppRoot());
					}
				}
			}
		} catch (const exception &e) {
			P_ERROR("Uncaught exception: " << e.what());
		}
	}
	
	/**
	 * @throws boost::thread_interrupted
	 * @throws SpawnException
	 * @throws SystemException
	 */
	pair<AppContainerPtr, AppContainerList *>
	spawnOrUseExisting(
		mutex::scoped_lock &l,
		const string &appRoot,
		bool lowerPrivilege,
		const string &lowestUser,
		const string &environment,
		const string &spawnMethod
	) {
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		AppContainerPtr container;
		AppContainerList *list;
		
		try {
			ApplicationMap::iterator it(apps.find(appRoot));
			
			if (it != apps.end() && needsRestart(appRoot)) {
				AppContainerList::iterator it2;
				list = it->second.get();
				for (it2 = list->begin(); it2 != list->end(); it2++) {
					container = *it2;
					if (container->sessions == 0) {
						inactiveApps.erase(container->ia_iterator);
					} else {
						active--;
					}
					it2--;
					list->erase(container->iterator);
					count--;
				}
				apps.erase(appRoot);
				appInstanceCount.erase(appRoot);
				spawnManager.reload(appRoot);
				it = apps.end();
			}
			
			if (it != apps.end()) {
				list = it->second.get();
				
				if (list->front()->sessions == 0) {
					container = list->front();
					list->pop_front();
					list->push_back(container);
					container->iterator = list->end();
					container->iterator--;
					if (container->sessions == 0) {
						inactiveApps.erase(container->ia_iterator);
					}
					active++;
				} else if (count >= max || (
					maxPerApp != 0 && appInstanceCount[appRoot] >= maxPerApp )
					) {
					return make_pair(AppContainerPtr(), (AppContainerList *) 0);
				} else {
					container = ptr(new AppContainer());
					{
						this_thread::restore_interruption ri(di);
						this_thread::restore_syscall_interruption rsi(dsi);
						container->app = spawnManager.spawn(appRoot,
							lowerPrivilege, lowestUser, environment,
							spawnMethod);
					}
					container->sessions = 0;
					list->push_back(container);
					container->iterator = list->end();
					container->iterator--;
					appInstanceCount[appRoot]++;
					count++;
					active++;
					activeOrMaxChanged.notify_all();
				}
			} else {
				while (!(
					active < max &&
					(maxPerApp == 0 || appInstanceCount[appRoot] < maxPerApp)
				)) {
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
						appInstanceCount.erase(container->app->getAppRoot());
					} else {
						appInstanceCount[container->app->getAppRoot()]--;
					}
					count--;
				}
				container = ptr(new AppContainer());
				{
					this_thread::restore_interruption ri(di);
					this_thread::restore_syscall_interruption rsi(dsi);
					container->app = spawnManager.spawn(appRoot, lowerPrivilege, lowestUser,
						environment, spawnMethod);
				}
				container->sessions = 0;
				it = apps.find(appRoot);
				if (it == apps.end()) {
					list = new AppContainerList();
					apps[appRoot] = ptr(list);
					appInstanceCount[appRoot] = 1;
				} else {
					list = it->second.get();
					appInstanceCount[appRoot]++;
				}
				list->push_back(container);
				container->iterator = list->end();
				container->iterator--;
				count++;
				active++;
				activeOrMaxChanged.notify_all();
			}
		} catch (const SpawnException &e) {
			string message("Cannot spawn application '");
			message.append(appRoot);
			message.append("': ");
			message.append(e.what());
			if (e.hasErrorPage()) {
				throw SpawnException(message, e.getErrorPage());
			} else {
				throw SpawnException(message);
			}
		} catch (const exception &e) {
			string message("Cannot spawn application '");
			message.append(appRoot);
			message.append("': ");
			message.append(e.what());
			throw SpawnException(message);
		}
		
		return make_pair(container, list);
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
	 * @param rubyCommand The Ruby interpreter's command.
	 * @param user The user that the spawn manager should run as. This
	 *             parameter only has effect if the current process is
	 *             running as root. If the empty string is given, or if
	 *             the <tt>user</tt> is not a valid username, then
	 *             the spawn manager will be run as the current user.
	 * @param rubyCommand The Ruby interpreter's command.
	 * @throws SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException The specified log file could not be opened.
	 */
	StandardApplicationPool(const string &spawnServerCommand,
	             const string &logFile = "",
	             const string &rubyCommand = "ruby",
	             const string &user = "")
	        :
		#ifndef PASSENGER_USE_DUMMY_SPAWN_MANAGER
		spawnManager(spawnServerCommand, logFile, rubyCommand, user),
		#endif
		data(new SharedData()),
		lock(data->lock),
		waitingLock(data->waitingLock),
		activeOrMaxChanged(data->activeOrMaxChanged),
		apps(data->apps),
		max(data->max),
		count(data->count),
		active(data->active),
		waiting(data->waiting),
		maxPerApp(data->maxPerApp),
		inactiveApps(data->inactiveApps),
		restartFileTimes(data->restartFileTimes),
		appInstanceCount(data->appInstanceCount)
	{
		detached = false;
		done = false;
		max = DEFAULT_MAX_POOL_SIZE;
		count = 0;
		active = 0;
		waiting = 0;
		maxPerApp = DEFAULT_MAX_INSTANCES_PER_APP;
		maxIdleTime = DEFAULT_MAX_IDLE_TIME;
		cleanerThread = new thread(
			bind(&StandardApplicationPool::cleanerThreadMainLoop, this),
			CLEANER_THREAD_STACK_SIZE
		);
	}
	
	virtual ~StandardApplicationPool() {
		if (!detached) {
			this_thread::disable_interruption di;
			{
				mutex::scoped_lock l(lock);
				done = true;
				cleanerThreadSleeper.notify_one();
			}
			cleanerThread->join();
		}
		delete cleanerThread;
	}
	
	virtual Application::SessionPtr get(
		const string &appRoot,
		bool lowerPrivilege = true,
		const string &lowestUser = "nobody",
		const string &environment = "production",
		const string &spawnMethod = "smart"
	) {
		using namespace boost::posix_time;
		unsigned int attempt = 0;
		ptime timeLimit(get_system_time() + millisec(GET_TIMEOUT));
		unique_lock<mutex> l(lock);
		
		while (true) {
			attempt++;
			
			pair<AppContainerPtr, AppContainerList *> p(
				spawnOrUseExisting(l, appRoot, lowerPrivilege, lowestUser,
					environment, spawnMethod)
			);
			AppContainerPtr &container(p.first);
			AppContainerList &list(*p.second);
			
			if (container != NULL) {
				container->lastUsed = time(NULL);
				container->sessions++;
				
				//P_ASSERT(verifyState(), Application::SessionPtr(),
				//	"State is valid:\n" << toString(false));
				try {
					return container->app->connect(SessionCloseCallback(data, container));
				} catch (const exception &e) {
					container->sessions--;
					if (attempt == MAX_GET_ATTEMPTS) {
						string message("Cannot connect to an existing "
							"application instance for '");
						message.append(appRoot);
						message.append("': ");
						try {
							const SystemException &syse =
								dynamic_cast<const SystemException &>(e);
							message.append(syse.sys());
						} catch (const bad_cast &) {
							message.append(e.what());
						}
						throw IOException(message);
					} else {
						list.erase(container->iterator);
						if (list.empty()) {
							apps.erase(appRoot);
						}
						appInstanceCount.erase(appRoot);
						count--;
						active--;
						// P_ASSERT(verifyState(), Application::SessionPtr(),
						//	"State is valid.");
					}
				}
			} else {
				{
					lock_guard<mutex> wl(waitingLock);
					waiting++;
				}
				while (!(
					active < max &&
					(maxPerApp == 0 || appInstanceCount[appRoot] < maxPerApp)
				)) {
					if (!activeOrMaxChanged.timed_wait(l, timeLimit)) {
						lock_guard<mutex> wl(waitingLock);
						waiting--;
						throw BusyException("Unable to fulfill get() request in time.");
					}
				}
				{
					lock_guard<mutex> wl(waitingLock);
					waiting--;
				}
			}
		}
		// Never reached; shut up compiler warning
		return Application::SessionPtr();
	}
	
	virtual void clear() {
		mutex::scoped_lock l(lock);
		apps.clear();
		inactiveApps.clear();
		restartFileTimes.clear();
		appInstanceCount.clear();
		count = 0;
		active = 0;
	}
	
	virtual void setMaxIdleTime(unsigned int seconds) {
		mutex::scoped_lock l(lock);
		maxIdleTime = seconds;
		cleanerThreadSleeper.notify_one();
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
	
	virtual void setMaxPerApp(unsigned int maxPerApp) {
		mutex::scoped_lock l(lock);
		this->maxPerApp = maxPerApp;
		activeOrMaxChanged.notify_all();
	}
	
	virtual pid_t getSpawnServerPid() const {
		return spawnManager.getServerPid();
	}
	
	/**
	 * Returns a textual description of the internal state of
	 * the application pool.
	 */
	virtual string toString(bool lockMutex = true) const {
		if (lockMutex) {
			return toString(boost::adopt_lock);
		} else {
			return toString(boost::defer_lock);
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_STANDARD_APPLICATION_POOL_H_ */

