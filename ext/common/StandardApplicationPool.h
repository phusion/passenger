/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_STANDARD_APPLICATION_POOL_H_
#define _PASSENGER_STANDARD_APPLICATION_POOL_H_

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/microsec_time_clock.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/thread.hpp>

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
#include "FileChangeChecker.h"
#include "CachedFileStat.hpp"
#ifdef PASSENGER_USE_DUMMY_SPAWN_MANAGER
	#include "DummySpawnManager.h"
#else
	#include "SpawnManager.h"
#endif

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;

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
	static const int CLEANER_THREAD_STACK_SIZE = 1024 * 64;
	static const unsigned int MAX_GET_ATTEMPTS = 10;
	static const unsigned int GET_TIMEOUT = 5000; // In milliseconds.

	friend class ApplicationPoolServer;
	struct Domain;
	struct AppContainer;
	
	typedef shared_ptr<Domain> DomainPtr;
	typedef shared_ptr<AppContainer> AppContainerPtr;
	typedef list<AppContainerPtr> AppContainerList;
	typedef map<string, DomainPtr> DomainMap;
	
	struct Domain {
		AppContainerList instances;
		unsigned int size;
		unsigned long maxRequests;
	};
	
	struct AppContainer {
		ApplicationPtr app;
		time_t startTime;
		time_t lastUsed;
		unsigned int sessions;
		unsigned int processed;
		AppContainerList::iterator iterator;
		AppContainerList::iterator ia_iterator;
		
		AppContainer() {
			startTime = time(NULL);
			processed = 0;
		}
		
		/**
		 * Returns the uptime of this AppContainer so far, as a string.
		 */
		string uptime() const {
			time_t seconds = time(NULL) - startTime;
			stringstream result;
			
			if (seconds >= 60) {
				time_t minutes = seconds / 60;
				if (minutes >= 60) {
					time_t hours = minutes / 60;
					minutes = minutes % 60;
					result << hours << "h ";
				}
				
				seconds = seconds % 60;
				result << minutes << "m ";
			}
			result << seconds << "s";
			return result.str();
		}
	};
	
	/**
	 * A data structure which contains data that's shared between a
	 * StandardApplicationPool and a SessionCloseCallback object.
	 * This is because the StandardApplicationPool's life time could be
	 * different from a SessionCloseCallback's.
	 */
	struct SharedData {
		boost::mutex lock;
		condition activeOrMaxChanged;
		
		DomainMap domains;
		unsigned int max;
		unsigned int count;
		unsigned int active;
		unsigned int maxPerApp;
		AppContainerList inactiveApps;
		map<string, unsigned int> appInstanceCount;
	};
	
	typedef shared_ptr<SharedData> SharedDataPtr;
	
	/**
	 * Function object which will be called when a session has been closed.
	 */
	struct SessionCloseCallback {
		SharedDataPtr data;
		weak_ptr<AppContainer> container;
		
		SessionCloseCallback(SharedDataPtr data,
		                     const weak_ptr<AppContainer> &container) {
			this->data = data;
			this->container = container;
		}
		
		void operator()() {
			boost::mutex::scoped_lock l(data->lock);
			AppContainerPtr container(this->container.lock());
			
			if (container == NULL) {
				return;
			}
			
			DomainMap::iterator it;
			it = data->domains.find(container->app->getAppRoot());
			if (it != data->domains.end()) {
				Domain *domain = it->second.get();
				AppContainerList *instances = &domain->instances;
				
				container->processed++;
				if (domain->maxRequests > 0 && container->processed >= domain->maxRequests) {
					instances->erase(container->iterator);
					domain->size--;
					if (instances->empty()) {
						data->domains.erase(container->app->getAppRoot());
					}
					data->count--;
					data->active--;
					data->activeOrMaxChanged.notify_all();
				} else {
					container->lastUsed = time(NULL);
					container->sessions--;
					if (container->sessions == 0) {
						instances->erase(container->iterator);
						instances->push_front(container);
						container->iterator = instances->begin();
						data->inactiveApps.push_back(container);
						container->ia_iterator = data->inactiveApps.end();
						container->ia_iterator--;
						data->active--;
						data->activeOrMaxChanged.notify_all();
					}
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
	oxt::thread *cleanerThread;
	bool detached;
	bool done;
	unsigned int maxIdleTime;
	unsigned int waitingOnGlobalQueue;
	condition cleanerThreadSleeper;
	CachedFileStat cstat;
	FileChangeChecker fileChangeChecker;
	
	// Shortcuts for instance variables in SharedData. Saves typing in get().
	boost::mutex &lock;
	condition &activeOrMaxChanged;
	DomainMap &domains;
	unsigned int &max;
	unsigned int &count;
	unsigned int &active;
	unsigned int &maxPerApp;
	AppContainerList &inactiveApps;
	map<string, unsigned int> &appInstanceCount;
	
	/**
	 * Verify that all the invariants are correct.
	 */
	bool inline verifyState() {
	#if PASSENGER_DEBUG
		// Invariants for _domains_.
		DomainMap::const_iterator it;
		unsigned int totalSize = 0;
		for (it = domains.begin(); it != domains.end(); it++) {
			const string &appRoot = it->first;
			Domain *domain = it->second.get();
			AppContainerList *instances = &domain->instances;
			
			P_ASSERT(domain->size <= count, false,
				"domains['" << appRoot << "'].size (" << domain->size <<
				") <= count (" << count << ")");
			totalSize += domain->size;
			
			// Invariants for Domain.
			
			P_ASSERT(!instances->empty(), false,
				"domains['" << appRoot << "'].instances is nonempty.");
			
			AppContainerList::const_iterator prev_lit;
			AppContainerList::const_iterator lit;
			prev_lit = instances->begin();
			lit = prev_lit;
			lit++;
			for (; lit != instances->end(); lit++) {
				if ((*prev_lit)->sessions > 0) {
					P_ASSERT((*lit)->sessions > 0, false,
						"domains['" << appRoot << "'].instances "
						"is sorted from nonactive to active");
				}
			}
		}
		P_ASSERT(totalSize == count, false, "(sum of all d.size in domains) == count");
		
		P_ASSERT(active <= count, false,
			"active (" << active << ") < count (" << count << ")");
		P_ASSERT(inactiveApps.size() == count - active, false,
			"inactive_apps.size() == count - active");
	#endif
		return true;
	}
	
	string toStringWithoutLock() const {
		stringstream result;
		
		result << "----------- General information -----------" << endl;
		result << "max      = " << max << endl;
		result << "count    = " << count << endl;
		result << "active   = " << active << endl;
		result << "inactive = " << inactiveApps.size() << endl;
		result << "Waiting on global queue: " << waitingOnGlobalQueue << endl;
		result << endl;
		
		result << "----------- Domains -----------" << endl;
		DomainMap::const_iterator it;
		for (it = domains.begin(); it != domains.end(); it++) {
			Domain *domain = it->second.get();
			AppContainerList *instances = &domain->instances;
			AppContainerList::const_iterator lit;
			
			result << it->first << ": " << endl;
			for (lit = instances->begin(); lit != instances->end(); lit++) {
				AppContainer *container = lit->get();
				char buf[128];
				
				snprintf(buf, sizeof(buf),
						"PID: %-5lu   Sessions: %-2u   Processed: %-5u   Uptime: %s",
						(unsigned long) container->app->getPid(),
						container->sessions,
						container->processed,
						container->uptime().c_str());
				result << "  " << buf << endl;
			}
			result << endl;
		}
		return result.str();
	}
	
	/**
	 * Checks whether the given application domain needs to be restarted.
	 *
	 * @throws TimeRetrievalException Something went wrong while retrieving the system time.
	 * @throws boost::thread_interrupted
	 */
	bool needsRestart(const string &appRoot, const PoolOptions &options) {
		string restartDir;
		if (options.restartDir.empty()) {
			restartDir = appRoot + "/tmp";
		} else if (options.restartDir[0] == '/') {
			restartDir = options.restartDir;
		} else {
			restartDir = appRoot + "/" + options.restartDir;
		}
		
		string alwaysRestartFile = restartDir + "/always_restart.txt";
		string restartFile = restartDir + "/restart.txt";
		struct stat buf;
		return cstat.stat(alwaysRestartFile, &buf, options.statThrottleRate) == 0 ||
		       fileChangeChecker.changed(restartFile, options.statThrottleRate);
	}
	
	void cleanerThreadMainLoop() {
		this_thread::disable_syscall_interruption dsi;
		unique_lock<boost::mutex> l(lock);
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
				
				time_t now = syscalls::time(NULL);
				AppContainerList::iterator it;
				for (it = inactiveApps.begin(); it != inactiveApps.end(); it++) {
					AppContainer &container(*it->get());
					ApplicationPtr app(container.app);
					Domain *domain = domains[app->getAppRoot()].get();
					AppContainerList *instances = &domain->instances;
					
					if (maxIdleTime > 0 &&  
					   (now - container.lastUsed > (time_t) maxIdleTime)) {
						P_DEBUG("Cleaning idle app " << app->getAppRoot() <<
							" (PID " << app->getPid() << ")");
						instances->erase(container.iterator);
						
						AppContainerList::iterator prev = it;
						prev--;
						inactiveApps.erase(it);
						it = prev;
						
						domain->size--;
						
						count--;
					}
					if (instances->empty()) {
						domains.erase(app->getAppRoot());
					}
				}
			}
		} catch (const exception &e) {
			P_ERROR("Uncaught exception: " << e.what());
		}
	}
	
	/**
	 * Spawn a new application instance, or use an existing one that's in the pool.
	 *
	 * @throws boost::thread_interrupted
	 * @throws SpawnException
	 * @throws SystemException
	 * @throws TimeRetrievalException Something went wrong while retrieving the system time.
	 */
	pair<AppContainerPtr, Domain *>
	spawnOrUseExisting(boost::mutex::scoped_lock &l, const PoolOptions &options) {
		beginning_of_function:
		
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		const string &appRoot(options.appRoot);
		AppContainerPtr container;
		Domain *domain;
		AppContainerList *instances;
		
		try {
			DomainMap::iterator it(domains.find(appRoot));
			
			if (needsRestart(appRoot, options)) {
				if (it != domains.end()) {
					AppContainerList::iterator it2;
					instances = &it->second->instances;
					for (it2 = instances->begin(); it2 != instances->end(); it2++) {
						container = *it2;
						if (container->sessions == 0) {
							inactiveApps.erase(container->ia_iterator);
						} else {
							active--;
						}
						it2--;
						instances->erase(container->iterator);
						count--;
					}
					domains.erase(appRoot);
				}
				P_DEBUG("Restarting " << appRoot);
				spawnManager.reload(appRoot);
				it = domains.end();
				activeOrMaxChanged.notify_all();
			}
			
			if (it != domains.end()) {
				domain = it->second.get();
				instances = &domain->instances;
				
				if (instances->front()->sessions == 0) {
					container = instances->front();
					instances->pop_front();
					instances->push_back(container);
					container->iterator = instances->end();
					container->iterator--;
					inactiveApps.erase(container->ia_iterator);
					active++;
					activeOrMaxChanged.notify_all();
				} else if (count >= max || (
					maxPerApp != 0 && domain->size >= maxPerApp )
					) {
					if (options.useGlobalQueue) {
						UPDATE_TRACE_POINT();
						waitingOnGlobalQueue++;
						activeOrMaxChanged.wait(l);
						waitingOnGlobalQueue--;
						goto beginning_of_function;
					} else {
						AppContainerList::iterator it(instances->begin());
						AppContainerList::iterator smallest(instances->begin());
						it++;
						for (; it != instances->end(); it++) {
							if ((*it)->sessions < (*smallest)->sessions) {
								smallest = it;
							}
						}
						container = *smallest;
						instances->erase(smallest);
						instances->push_back(container);
						container->iterator = instances->end();
						container->iterator--;
					}
				} else {
					container = ptr(new AppContainer());
					{
						this_thread::restore_interruption ri(di);
						this_thread::restore_syscall_interruption rsi(dsi);
						container->app = spawnManager.spawn(options);
					}
					container->sessions = 0;
					instances->push_back(container);
					container->iterator = instances->end();
					container->iterator--;
					domain->size++;
					count++;
					active++;
					activeOrMaxChanged.notify_all();
				}
			} else {
				if (active >= max) {
					UPDATE_TRACE_POINT();
					activeOrMaxChanged.wait(l);
					goto beginning_of_function;
				} else if (count == max) {
					container = inactiveApps.front();
					inactiveApps.pop_front();
					domain = domains[container->app->getAppRoot()].get();
					instances = &domain->instances;
					instances->erase(container->iterator);
					if (instances->empty()) {
						domains.erase(container->app->getAppRoot());
					} else {
						domain->size--;
					}
					count--;
				}
				
				UPDATE_TRACE_POINT();
				container = ptr(new AppContainer());
				{
					this_thread::restore_interruption ri(di);
					this_thread::restore_syscall_interruption rsi(dsi);
					container->app = spawnManager.spawn(options);
				}
				container->sessions = 0;
				it = domains.find(appRoot);
				if (it == domains.end()) {
					domain = new Domain();
					domain->size = 1;
					domain->maxRequests = options.maxRequests;
					domains[appRoot] = ptr(domain);
				} else {
					domain = it->second.get();
					domain->size++;
				}
				instances = &domain->instances;
				instances->push_back(container);
				container->iterator = instances->end();
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
		
		return make_pair(container, domain);
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
		activeOrMaxChanged(data->activeOrMaxChanged),
		domains(data->domains),
		max(data->max),
		count(data->count),
		active(data->active),
		maxPerApp(data->maxPerApp),
		inactiveApps(data->inactiveApps),
		appInstanceCount(data->appInstanceCount)
	{
		TRACE_POINT();
		detached = false;
		done = false;
		max = DEFAULT_MAX_POOL_SIZE;
		count = 0;
		active = 0;
		waitingOnGlobalQueue = 0;
		maxPerApp = DEFAULT_MAX_INSTANCES_PER_APP;
		maxIdleTime = DEFAULT_MAX_IDLE_TIME;
		cleanerThread = new oxt::thread(
			bind(&StandardApplicationPool::cleanerThreadMainLoop, this),
			"ApplicationPool cleaner",
			CLEANER_THREAD_STACK_SIZE
		);
	}
	
	virtual ~StandardApplicationPool() {
		if (!detached) {
			this_thread::disable_interruption di;
			{
				boost::mutex::scoped_lock l(lock);
				done = true;
				cleanerThreadSleeper.notify_one();
			}
			cleanerThread->join();
		}
		delete cleanerThread;
	}
	
	virtual Application::SessionPtr get(const string &appRoot) {
		return ApplicationPool::get(appRoot);
	}
	
	virtual Application::SessionPtr get(const PoolOptions &options) {
		TRACE_POINT();
		using namespace boost::posix_time;
		unsigned int attempt = 0;
		// TODO: We should probably add a timeout to the following
		// lock. This way we can fail gracefully if the server's under
		// rediculous load. Though I'm not sure how much it really helps.
		unique_lock<boost::mutex> l(lock);
		
		while (true) {
			attempt++;
			
			pair<AppContainerPtr, Domain *> p(
				spawnOrUseExisting(l, options)
			);
			AppContainerPtr &container = p.first;
			Domain *domain = p.second;

			container->lastUsed = time(NULL);
			container->sessions++;
			
			P_ASSERT(verifyState(), Application::SessionPtr(),
				"State is valid:\n" << toString(false));
			try {
				UPDATE_TRACE_POINT();
				return container->app->connect(SessionCloseCallback(data, container));
			} catch (const exception &e) {
				container->sessions--;
				
				AppContainerList &instances(domain->instances);
				instances.erase(container->iterator);
				domain->size--;
				if (instances.empty()) {
					domains.erase(options.appRoot);
				}
				count--;
				active--;
				activeOrMaxChanged.notify_all();
				P_ASSERT(verifyState(), Application::SessionPtr(),
					"State is valid: " << toString(false));
				if (attempt == MAX_GET_ATTEMPTS) {
					string message("Cannot connect to an existing "
						"application instance for '");
					message.append(options.appRoot);
					message.append("': ");
					try {
						const SystemException &syse =
							dynamic_cast<const SystemException &>(e);
						message.append(syse.sys());
					} catch (const bad_cast &) {
						message.append(e.what());
					}
					throw IOException(message);
				}
			}
		}
		// Never reached; shut up compiler warning
		return Application::SessionPtr();
	}
	
	virtual void clear() {
		boost::mutex::scoped_lock l(lock);
		domains.clear();
		inactiveApps.clear();
		appInstanceCount.clear();
		count = 0;
		active = 0;
		activeOrMaxChanged.notify_all();
	}
	
	virtual void setMaxIdleTime(unsigned int seconds) {
		boost::mutex::scoped_lock l(lock);
		maxIdleTime = seconds;
		cleanerThreadSleeper.notify_one();
	}
	
	virtual void setMax(unsigned int max) {
		boost::mutex::scoped_lock l(lock);
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
		boost::mutex::scoped_lock l(lock);
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
			unique_lock<boost::mutex> l(lock);
			return toStringWithoutLock();
		} else {
			return toStringWithoutLock();
		}
	}
	
	/**
	 * Returns an XML description of the internal state of the
	 * application pool.
	 */
	virtual string toXml() const {
		unique_lock<boost::mutex> l(lock);
		stringstream result;
		DomainMap::const_iterator it;
		
		result << "<?xml version=\"1.0\" encoding=\"iso8859-1\" ?>\n";
		result << "<info>";
		
		result << "<domains>";
		for (it = domains.begin(); it != domains.end(); it++) {
			Domain *domain = it->second.get();
			AppContainerList *instances = &domain->instances;
			AppContainerList::const_iterator lit;
			
			result << "<domain>";
			result << "<name>" << escapeForXml(it->first) << "</name>";
			
			result << "<instances>";
			for (lit = instances->begin(); lit != instances->end(); lit++) {
				AppContainer *container = lit->get();
				
				result << "<instance>";
				result << "<pid>" << container->app->getPid() << "</pid>";
				result << "<sessions>" << container->sessions << "</sessions>";
				result << "<processed>" << container->processed << "</processed>";
				result << "<uptime>" << container->uptime() << "</uptime>";
				result << "</instance>";
			}
			result << "</instances>";
			
			result << "</domain>";
		}
		result << "</domains>";
		
		result << "</info>";
		return result.str();
	}
};

typedef shared_ptr<StandardApplicationPool> StandardApplicationPoolPtr;

} // namespace Passenger

#endif /* _PASSENGER_STANDARD_APPLICATION_POOL_H_ */

