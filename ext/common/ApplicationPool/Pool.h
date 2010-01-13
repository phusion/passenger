/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL_POOL_H_
#define _PASSENGER_APPLICATION_POOL_POOL_H_

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

#include "Interface.h"
#include "../Logging.h"
#include "../FileChangeChecker.h"
#include "../CachedFileStat.hpp"
#include "../SpawnManager.h"
#include "../SystemTime.h"

namespace Passenger {
namespace ApplicationPool {

using namespace std;
using namespace boost;
using namespace oxt;

// Forward declaration for ApplicationPool::Server.
class Server;

/****************************************************************
 *
 *  See "doc/ApplicationPool algorithm.txt" for a more readable
 *  and detailed description of the algorithm implemented here.
 *
 ****************************************************************/

/**
 * A standard implementation of ApplicationPool::Interface for single-process environments.
 *
 * The environment may or may not be multithreaded - ApplicationPool::Pool is completely
 * thread-safe. Apache with the threading MPM is an example of a multithreaded single-process
 * environment.
 *
 * This class is unusable in multi-process environments such as Apache with the prefork MPM.
 * The reasons are as follows:
 *  - ApplicationPool::Pool uses threads internally. Because threads disappear after a fork(),
 *    an ApplicationPool::Pool object will become unusable after a fork().
 *  - ApplicationPool::Pool stores its internal cache on the heap. Different processes
 *    cannot share their heaps, so they will not be able to access each others' pool cache.
 *  - ApplicationPool::Pool has a connection to the spawn server. If there are multiple
 *    processes, and they all use the spawn servers's connection at the same time without
 *    some sort of synchronization, then bad things will happen.
 *
 * (Of course, ApplicationPool::Pool <em>is</em> usable if each process creates its own
 * ApplicationPool::Pool object, but that would defeat the point of having a shared pool.)
 *
 * For multi-process environments, one should use ApplicationPool::Server +
 * ApplicationPool::Client instead.
 *
 * ApplicationPool::Pool is fully thread-safe.
 *
 * @ingroup Support
 */
class Pool: public ApplicationPool::Interface {
public:
	static const int DEFAULT_MAX_IDLE_TIME = 120;
	static const int DEFAULT_MAX_POOL_SIZE = 20;
	static const int DEFAULT_MAX_INSTANCES_PER_APP = 0;
	static const int CLEANER_THREAD_STACK_SIZE = 1024 * 64;
	static const unsigned int MAX_GET_ATTEMPTS = 10;

private:
	struct Group;
	struct ProcessInfo;
	
	typedef shared_ptr<Group> GroupPtr;
	typedef shared_ptr<ProcessInfo> ProcessInfoPtr;
	typedef list<ProcessInfoPtr> ProcessInfoList;
	typedef map<string, GroupPtr> GroupMap;
	
	struct Group {
		ProcessInfoList processes;
		unsigned int size;
		bool detached;
		unsigned long maxRequests;
		unsigned long minProcesses;
		
		Group() {
			size = 0;
			detached = false;
			maxRequests = 0;
			minProcesses = 0;
		}
	};
	
	struct ProcessInfo {
		ProcessPtr process;
		unsigned long long startTime;
		time_t lastUsed;
		unsigned int sessions;
		unsigned int processed;
		ProcessInfoList::iterator iterator;
		ProcessInfoList::iterator ia_iterator;
		bool detached;
		
		ProcessInfo() {
			startTime  = SystemTime::getMsec();
			lastUsed   = 0;
			sessions   = 0;
			processed  = 0;
			detached   = false;
		}
		
		/**
		 * Returns the uptime of this process so far, as a string.
		 */
		string uptime() const {
			unsigned long long seconds = (unsigned long long) time(NULL) - startTime / 1000;
			stringstream result;
			
			if (seconds >= 60) {
				unsigned long long minutes = seconds / 60;
				if (minutes >= 60) {
					unsigned long long hours = minutes / 60;
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
	 * A data structure which contains data that's shared between an
	 * ApplicationPool::Pool and a SessionCloseCallback object.
	 * This is because the ApplicationPool::Pool's life time could be
	 * different from a SessionCloseCallback's.
	 */
	struct SharedData {
		boost::mutex lock;
		condition activeOrMaxChanged;
		
		GroupMap groups;
		unsigned int max;
		unsigned int count;
		unsigned int active;
		unsigned int maxPerApp;
		ProcessInfoList inactiveApps;
	};
	
	typedef shared_ptr<SharedData> SharedDataPtr;
	
	/**
	 * Function object which will be called when a session has been closed.
	 */
	struct SessionCloseCallback {
		SharedDataPtr data;
		weak_ptr<ProcessInfo> processInfo;
		
		SessionCloseCallback(const SharedDataPtr &data,
		                     const weak_ptr<ProcessInfo> &processInfo) {
			this->data = data;
			this->processInfo = processInfo;
		}
		
		void operator()() {
			ProcessInfoPtr processInfo = this->processInfo.lock();
			if (processInfo == NULL || processInfo->detached) {
				return;
			}
			
			boost::mutex::scoped_lock l(data->lock);
			if (processInfo->detached) {
				return;
			}
			
			GroupMap::iterator it;
			it = data->groups.find(processInfo->process->getAppRoot());
			Group *group = it->second.get();
			ProcessInfoList *processes = &group->processes;
			
			processInfo->processed++;
			
			if (group->maxRequests > 0 && processInfo->processed >= group->maxRequests) {
				processInfo->detached = true;
				processes->erase(processInfo->iterator);
				group->size--;
				if (processes->empty()) {
					group->detached = true;
					data->groups.erase(processInfo->process->getAppRoot());
				}
				data->count--;
				if (processInfo->sessions == 0) {
					data->inactiveApps.erase(processInfo->ia_iterator);
				} else {
					data->active--;
					data->activeOrMaxChanged.notify_all();
				}
			} else {
				processInfo->lastUsed = time(NULL);
				processInfo->sessions--;
				if (processInfo->sessions == 0) {
					processes->erase(processInfo->iterator);
					processes->push_front(processInfo);
					processInfo->iterator = processes->begin();
					data->inactiveApps.push_back(processInfo);
					processInfo->ia_iterator = data->inactiveApps.end();
					processInfo->ia_iterator--;
					data->active--;
					data->activeOrMaxChanged.notify_all();
				}
			}
		}
	};
	
	AbstractSpawnManagerPtr spawnManager;
	SharedDataPtr data;
	oxt::thread *cleanerThread;
	bool done;
	unsigned int maxIdleTime;
	unsigned int waitingOnGlobalQueue;
	condition cleanerThreadSleeper;
	CachedFileStat cstat;
	FileChangeChecker fileChangeChecker;
	
	// Shortcuts for instance variables in SharedData. Saves typing in get()
	// and other methods.
	boost::mutex &lock;
	condition &activeOrMaxChanged;
	GroupMap &groups;
	unsigned int &max;
	unsigned int &count;
	unsigned int &active;
	unsigned int &maxPerApp;
	ProcessInfoList &inactiveApps;
	
	/**
	 * Verify that all the invariants are correct.
	 */
	bool inline verifyState() {
	#if PASSENGER_DEBUG
		// Invariants for _groups_.
		GroupMap::const_iterator it;
		unsigned int totalSize = 0;
		for (it = groups.begin(); it != groups.end(); it++) {
			const string &appRoot = it->first;
			Group *group = it->second.get();
			ProcessInfoList *processes = &group->processes;
			
			// Invariants for Group.
			
			P_ASSERT(group->size <= count, false,
				"groups['" << appRoot << "'].size (" << group->size <<
				") <= count (" << count << ")");
			totalSize += group->size;
			P_ASSERT(!processes->empty(), false,
				"groups['" << appRoot << "'].processes is nonempty.");
			P_ASSERT(!group->detached, false,
				"groups['" << appRoot << "'].detached is true");
			
			ProcessInfoList::const_iterator prev_lit;
			ProcessInfoList::const_iterator lit;
			prev_lit = processes->begin();
			lit = prev_lit;
			lit++;
			for (; lit != processes->end(); lit++) {
				const ProcessInfoPtr &processInfo = *lit;
				
				// Invariants for ProcessInfo.
				if ((*prev_lit)->sessions > 0) {
					P_ASSERT(processInfo->sessions > 0, false,
						"groups['" << appRoot << "'].processes "
						"is sorted from nonactive to active");
					P_ASSERT(!processInfo->detached, false,
						"groups['" << appRoot << "'].detached "
						"is false");
				}
			}
		}
		P_ASSERT(totalSize == count, false, "(sum of all d.size in groups) == count");
		
		P_ASSERT(active <= count, false,
			"active (" << active << ") < count (" << count << ")");
		P_ASSERT(inactiveApps.size() == count - active, false,
			"inactive_apps.size() == count - active");
	#endif
		return true;
	}
	
	string inspectWithoutLock() const {
		stringstream result;
		
		result << "----------- General information -----------" << endl;
		result << "max      = " << max << endl;
		result << "count    = " << count << endl;
		result << "active   = " << active << endl;
		result << "inactive = " << inactiveApps.size() << endl;
		result << "Waiting on global queue: " << waitingOnGlobalQueue << endl;
		result << endl;
		
		result << "----------- Groups -----------" << endl;
		GroupMap::const_iterator it;
		for (it = groups.begin(); it != groups.end(); it++) {
			Group *group = it->second.get();
			ProcessInfoList *processes = &group->processes;
			ProcessInfoList::const_iterator lit;
			
			result << it->first << ": " << endl;
			for (lit = processes->begin(); lit != processes->end(); lit++) {
				const ProcessInfo *processInfo = lit->get();
				char buf[128];
				
				snprintf(buf, sizeof(buf),
						"PID: %-5lu   Sessions: %-2u   Processed: %-5u   Uptime: %s",
						(unsigned long) processInfo->process->getPid(),
						processInfo->sessions,
						processInfo->processed,
						processInfo->uptime().c_str());
				result << "  " << buf << endl;
			}
			result << endl;
		}
		return result.str();
	}
	
	/**
	 * Checks whether the given application group needs to be restarted.
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
	
	ProcessInfoPtr selectProcess(ProcessInfoList *processes, const PoolOptions &options,
	                             unique_lock<boost::mutex> &l)
	{
		if (options.useGlobalQueue) {
			TRACE_POINT();
			waitingOnGlobalQueue++;
			activeOrMaxChanged.wait(l);
			waitingOnGlobalQueue--;
			return ProcessInfoPtr();
		} else {
			ProcessInfoList::iterator it = processes->begin();
			ProcessInfoList::iterator end = processes->end();
			ProcessInfoList::iterator smallest = processes->begin();
			ProcessInfoPtr processInfo;
			
			it++;
			for (; it != end; it++) {
				if ((*it)->sessions < (*smallest)->sessions) {
					smallest = it;
				}
			}
			processInfo = *smallest;
			processes->erase(smallest);
			processes->push_back(processInfo);
			processInfo->iterator = processes->end();
			processInfo->iterator--;
			return processInfo;
		}
	}
	
	bool detachWithoutLock(const string &detachKey) {
		GroupMap::iterator group_it;
		GroupMap::iterator group_it_end = groups.end();
		
		for (group_it = groups.begin(); group_it != group_it_end; group_it++) {
			GroupPtr &group = group_it->second;
			ProcessInfoList &processes = group->processes;
			ProcessInfoList::iterator process_info_it = processes.begin();
			ProcessInfoList::iterator process_info_it_end = processes.end();
			
			for (; process_info_it != process_info_it_end; process_info_it++) {
				ProcessInfoPtr processInfo = *process_info_it;
				if (processInfo->process->getDetachKey() == detachKey) {
					// Found a matching process.
					processInfo->detached = true;
					processes.erase(processInfo->iterator);
					group->size--;
					if (processes.empty()) {
						group->detached = true;
						groups.erase(processInfo->process->getAppRoot());
					}
					if (processInfo->sessions == 0) {
						inactiveApps.erase(processInfo->ia_iterator);
					} else {
						active--;
						activeOrMaxChanged.notify_all();
					}
					count--;
					return true;
				}
			}
		}
		return false;
	}
	
	/**
	 * Marks all ProcessInfo objects as detached. Doesn't lock the data structures.
	 */
	void markAllAsDetached() {
		GroupMap::iterator group_it;
		GroupMap::iterator group_it_end = groups.end();
		
		for (group_it = groups.begin(); group_it != group_it_end; group_it++) {
			GroupPtr &group = group_it->second;
			ProcessInfoList &processes = group->processes;
			ProcessInfoList::iterator process_info_it = processes.begin();
			ProcessInfoList::iterator process_info_it_end = processes.end();
			
			for (; process_info_it != process_info_it_end; process_info_it++) {
				ProcessInfoPtr &processInfo = *process_info_it;
				processInfo->detached = true;
			}
		}
	}
	
	void cleanerThreadMainLoop() {
		this_thread::disable_syscall_interruption dsi;
		unique_lock<boost::mutex> l(lock);
		try {
			while (!done && !this_thread::interruption_requested()) {
				if (maxIdleTime == 0) {
					cleanerThreadSleeper.wait(l);
					if (done) {
						// ApplicationPool::Pool is being destroyed.
						break;
					} else {
						// maxIdleTime changed.
						continue;
					}
				} else {
					xtime xt;
					xtime_get(&xt, TIME_UTC);
					xt.sec += maxIdleTime + 1;
					if (cleanerThreadSleeper.timed_wait(l, xt)) {
						// Condition was woken up.
						if (done) {
							// ApplicationPool::Pool is being destroyed.
							break;
						} else {
							// maxIdleTime changed.
							continue;
						}
					}
					// Timeout: maxIdleTime + 1 seconds passed.
				}
				
				time_t now = syscalls::time(NULL);
				ProcessInfoList::iterator it = inactiveApps.begin();
				ProcessInfoList::iterator end_it = inactiveApps.end();
				for (; it != end_it; it++) {
					ProcessInfoPtr processInfo = *it;
					
					if (now - processInfo->lastUsed > (time_t) maxIdleTime) {
						ProcessPtr process = processInfo->process;
						GroupPtr   group   = groups[process->getAppRoot()];
						
						if (group->size > group->minProcesses) {
							ProcessInfoList *processes = &group->processes;
							
							P_DEBUG("Cleaning idle process " << process->getAppRoot() <<
								" (PID " << process->getPid() << ")");
							processes->erase(processInfo->iterator);
							processInfo->detached = true;
							
							ProcessInfoList::iterator prev = it;
							prev--;
							inactiveApps.erase(it);
							it = prev;
							
							group->size--;
							count--;
							
							if (processes->empty()) {
								group->detached = true;
								groups.erase(process->getAppRoot());
							}
						}
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
	 * @throws TimeRetrievalException Something went wrong while retrieving the system time.
	 * @throws Anything thrown by options.environmentVariables->getItems().
	 */
	pair<ProcessInfoPtr, Group *>
	checkoutWithoutLock(unique_lock<boost::mutex> &l, const PoolOptions &options) {
		beginning_of_function:
		
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		const string &appRoot(options.appRoot);
		ProcessInfoPtr processInfo;
		Group *group;
		ProcessInfoList *processes;
		
		try {
			GroupMap::iterator group_it = groups.find(appRoot);
			
			if (needsRestart(appRoot, options)) {
				P_DEBUG("Restarting " << appRoot);
				spawnManager->reload(appRoot);
				if (group_it != groups.end()) {
					ProcessInfoList::iterator list_it;
					group = group_it->second.get();
					processes = &group->processes;
					for (list_it = processes->begin(); list_it != processes->end(); list_it++) {
						processInfo = *list_it;
						if (processInfo->sessions == 0) {
							inactiveApps.erase(processInfo->ia_iterator);
						} else {
							active--;
							activeOrMaxChanged.notify_all();
						}
						list_it--;
						processes->erase(processInfo->iterator);
						processInfo->detached = true;
						count--;
					}
					
					group->detached = true;
					groups.erase(appRoot);
					group_it = groups.end();
				}
			}
			
			if (group_it != groups.end()) {
				group = group_it->second.get();
				processes = &group->processes;
				
				if (processes->front()->sessions == 0) {
					processInfo = processes->front();
					processes->pop_front();
					processes->push_back(processInfo);
					processInfo->iterator = processes->end();
					processInfo->iterator--;
					inactiveApps.erase(processInfo->ia_iterator);
					active++;
					activeOrMaxChanged.notify_all();
				} else if (count >= max || (
					maxPerApp != 0 && group->size >= maxPerApp )
					)
				{
					processInfo = selectProcess(processes, options, l);
					if (processInfo == NULL) {
						goto beginning_of_function;
					}
				} else {
					ProcessPtr process;
					{
						this_thread::restore_interruption ri(di);
						this_thread::restore_syscall_interruption rsi(dsi);
						process = spawnManager->spawn(options);
					}
					processInfo = ptr(new ProcessInfo());
					processInfo->process = process;
					processInfo->sessions = 0;
					processes->push_back(processInfo);
					processInfo->iterator = processes->end();
					processInfo->iterator--;
					group->size++;
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
					processInfo = inactiveApps.front();
					inactiveApps.pop_front();
					processInfo->detached = true;
					group = groups[processInfo->process->getAppRoot()].get();
					processes = &group->processes;
					processes->erase(processInfo->iterator);
					if (processes->empty()) {
						group->detached = true;
						groups.erase(processInfo->process->getAppRoot());
					} else {
						group->size--;
					}
					count--;
				}
				
				UPDATE_TRACE_POINT();
				processInfo = ptr(new ProcessInfo());
				{
					this_thread::restore_interruption ri(di);
					this_thread::restore_syscall_interruption rsi(dsi);
					processInfo->process = spawnManager->spawn(options);
				}
				processInfo->sessions = 0;
				group = new Group();
				group->size = 1;
				group->maxRequests = options.maxRequests;
				group->minProcesses = options.minProcesses;
				groups[appRoot] = ptr(group);
				processes = &group->processes;
				processes->push_back(processInfo);
				processInfo->iterator = processes->end();
				processInfo->iterator--;
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
		
		processInfo->lastUsed = time(NULL);
		processInfo->sessions++;
		return make_pair(processInfo, group);
	}
	
	/** @throws boost::thread_resource_error */
	void initialize() {
		done = false;
		max = DEFAULT_MAX_POOL_SIZE;
		count = 0;
		active = 0;
		waitingOnGlobalQueue = 0;
		maxPerApp = DEFAULT_MAX_INSTANCES_PER_APP;
		maxIdleTime = DEFAULT_MAX_IDLE_TIME;
		cleanerThread = new oxt::thread(
			bind(&Pool::cleanerThreadMainLoop, this),
			"ApplicationPool cleaner",
			CLEANER_THREAD_STACK_SIZE
		);
	}
	
public:
	/**
	 * Create a new ApplicationPool::Pool object, and initialize it with a
	 * SpawnManager. The arguments here are all passed to the SpawnManager
	 * constructor.
	 *
	 * @throws SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException The specified log file could not be opened.
	 * @throws boost::thread_resource_error Cannot spawn a new thread.
	 */
	Pool(const string &spawnServerCommand,
	     const ServerInstanceDir::GenerationPtr &generation,
	     const AccountPtr &poolAccount = AccountPtr(),
	     const string &rubyCommand = "ruby")
	   : data(new SharedData()),
		cstat(DEFAULT_MAX_POOL_SIZE),
		lock(data->lock),
		activeOrMaxChanged(data->activeOrMaxChanged),
		groups(data->groups),
		max(data->max),
		count(data->count),
		active(data->active),
		maxPerApp(data->maxPerApp),
		inactiveApps(data->inactiveApps)
	{
		TRACE_POINT();
		this->spawnManager = ptr(new SpawnManager(spawnServerCommand, generation,
			poolAccount, rubyCommand));
		initialize();
	}
	
	/**
	 * Create a new ApplicationPool::Pool object and initialize it with
	 * the given spawn manager.
	 *
	 * @throws boost::thread_resource_error Cannot spawn a new thread.
	 */
	Pool(AbstractSpawnManagerPtr spawnManager)
	   : data(new SharedData()),
	     cstat(DEFAULT_MAX_POOL_SIZE),
	     lock(data->lock),
	     activeOrMaxChanged(data->activeOrMaxChanged),
	     groups(data->groups),
	     max(data->max),
	     count(data->count),
	     active(data->active),
	     maxPerApp(data->maxPerApp),
	     inactiveApps(data->inactiveApps)
	{
		TRACE_POINT();
		this->spawnManager = spawnManager;
		initialize();
	}
	
	virtual ~Pool() {
		this_thread::disable_interruption di;
		{
			lock_guard<boost::mutex> l(lock);
			done = true;
			cleanerThreadSleeper.notify_one();
			markAllAsDetached();
		}
		cleanerThread->join();
		delete cleanerThread;
	}
	
	virtual SessionPtr get(const string &appRoot) {
		return ApplicationPool::Interface::get(appRoot);
	}
	
	virtual SessionPtr get(const PoolOptions &options) {
		TRACE_POINT();
		unsigned int attempt = 0;
		
		while (true) {
			attempt++;
			
			pair<ProcessInfoPtr, Group *> p;
			{
				unique_lock<boost::mutex> l(lock);
				p = checkoutWithoutLock(l, options);
				P_ASSERT(verifyState(), SessionPtr(),
					"ApplicationPool state is valid:\n" << inspectWithoutLock());
			}
			ProcessInfoPtr &processInfo = p.first;
			
			try {
				UPDATE_TRACE_POINT();
				SessionPtr session = processInfo->process->newSession(
					SessionCloseCallback(data, processInfo),
					options.initiateSession
				);
				return session;
				
			} catch (SystemException &e) {
				{
					unique_lock<boost::mutex> l(lock);
					detachWithoutLock(processInfo->process->getDetachKey());
					processInfo->sessions--;
					P_ASSERT(verifyState(), SessionPtr(),
						"ApplicationPool state is valid:\n" << inspectWithoutLock());
				}
				if (e.code() == EMFILE || attempt == MAX_GET_ATTEMPTS) {
					/* A "too many open files" (EMFILE) error is probably unrecoverable,
					 * so propagate that immediately.
					 */
					e.setBriefMessage("Cannot connect to an existing "
						"application instance for '" +
						options.appRoot +
						"'");
					throw;
				} // else retry
				
			} catch (exception &e) {
				{
					unique_lock<boost::mutex> l(lock);
					detachWithoutLock(processInfo->process->getDetachKey());
					processInfo->sessions--;
					P_ASSERT(verifyState(), SessionPtr(),
						"ApplicationPool state is valid:\n" << inspectWithoutLock());
				}
				if (attempt == MAX_GET_ATTEMPTS) {
					string message("Cannot connect to an existing "
						"application instance for '");
					message.append(options.appRoot);
					message.append("': ");
					message.append(e.what());
					throw IOException(message);
				} // else retry
			}
		}
		// Never reached; shut up compiler warning
		return SessionPtr();
	}
	
	virtual bool detach(const string &detachKey) {
		unique_lock<boost::mutex> l(lock);
		return detachWithoutLock(detachKey);
	}
	
	virtual void clear() {
		lock_guard<boost::mutex> l(lock);
		markAllAsDetached();
		groups.clear();
		inactiveApps.clear();
		count = 0;
		active = 0;
		activeOrMaxChanged.notify_all();
		// TODO: clear cstat and fileChangeChecker, and reload all spawner servers.
	}
	
	virtual void setMaxIdleTime(unsigned int seconds) {
		lock_guard<boost::mutex> l(lock);
		maxIdleTime = seconds;
		cleanerThreadSleeper.notify_one();
	}
	
	virtual void setMax(unsigned int max) {
		lock_guard<boost::mutex> l(lock);
		this->max = max;
		activeOrMaxChanged.notify_all();
	}
	
	virtual unsigned int getActive() const {
		lock_guard<boost::mutex> l(lock);
		return active;
	}
	
	virtual unsigned int getCount() const {
		lock_guard<boost::mutex> l(lock);
		return count;
	}
	
	virtual void setMaxPerApp(unsigned int maxPerApp) {
		lock_guard<boost::mutex> l(lock);
		this->maxPerApp = maxPerApp;
		activeOrMaxChanged.notify_all();
	}
	
	virtual pid_t getSpawnServerPid() const {
		return spawnManager->getServerPid();
	}
	
	virtual string inspect() const {
		lock_guard<boost::mutex> l(lock);
		return inspectWithoutLock();
	}
	
	virtual string toXml(bool includeSensitiveInformation = true) const {
		lock_guard<boost::mutex> l(lock);
		stringstream result;
		GroupMap::const_iterator it;
		
		result << "<?xml version=\"1.0\" encoding=\"iso8859-1\" ?>\n";
		result << "<info>";
		
		result << "<groups>";
		for (it = groups.begin(); it != groups.end(); it++) {
			Group *group = it->second.get();
			ProcessInfoList *processes = &group->processes;
			ProcessInfoList::const_iterator lit;
			
			result << "<group>";
			result << "<name>" << escapeForXml(it->first) << "</name>";
			
			result << "<processes>";
			for (lit = processes->begin(); lit != processes->end(); lit++) {
				ProcessInfo *processInfo = lit->get();
				
				result << "<process>";
				result << "<pid>" << processInfo->process->getPid() << "</pid>";
				result << "<sessions>" << processInfo->sessions << "</sessions>";
				result << "<processed>" << processInfo->processed << "</processed>";
				result << "<uptime>" << processInfo->uptime() << "</uptime>";
				if (includeSensitiveInformation) {
					const ProcessPtr &process = processInfo->process;
					const Process::SocketInfoMap *serverSockets;
					Process::SocketInfoMap::const_iterator sit;
					
					result << "<connect_password>" << process->getConnectPassword() << "</connect_password>";
					result << "<server_sockets>";
					serverSockets = process->getServerSockets();
					for (sit = serverSockets->begin(); sit != serverSockets->end(); sit++) {
						const string &name = sit->first;
						const Process::SocketInfo &info = sit->second;
						result << "<server_socket>";
						result << "<name>" << escapeForXml(name) << "</name>";
						result << "<address>" << escapeForXml(info.address) << "</address>";
						result << "<type>" << escapeForXml(info.type) << "</type>";
						result << "</server_socket>";
					}
					result << "</server_sockets>";
				}
				result << "</process>";
			}
			result << "</processes>";
			
			result << "</group>";
		}
		result << "</groups>";
		
		result << "</info>";
		return result.str();
	}
};

typedef shared_ptr<Pool> PoolPtr;

} // namespace ApplicationPool
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_POOL_H_ */

