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
#include <cassert>
#ifdef TESTING_APPLICATION_POOL
	#include <cstdlib>
#endif

#include "Interface.h"
#include "../Logging.h"
#include "../SpawnManager.h"
#include "../Constants.h"
#include "../Utils/SystemTime.h"
#include "../Utils/FileChangeChecker.h"
#include "../Utils/CachedFileStat.hpp"
#include "../Utils/ProcessMetricsCollector.h"

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
	static const int CLEANER_THREAD_STACK_SIZE = 1024 * 64;
	static const int SPAWNER_THREAD_STACK_SIZE = 1024 * 64;
	static const int ANALYTICS_COLLECTION_THREAD_STACK_SIZE = 1024 * 64;
	static const unsigned int MAX_GET_ATTEMPTS = 10;

private:
	struct Group;
	struct ProcessInfo;
	
	typedef shared_ptr<Group> GroupPtr;
	typedef shared_ptr<ProcessInfo> ProcessInfoPtr;
	typedef list<ProcessInfoPtr> ProcessInfoList;
	typedef map<string, GroupPtr> GroupMap;
	
	struct Group {
		string name;
		string appRoot;
		ProcessInfoList processes;
		unsigned int size;
		bool detached;
		unsigned long maxRequests;
		unsigned long minProcesses;
		bool spawning;
		shared_ptr<oxt::thread> spawnerThread;
		string environment;
		bool analytics;
		string unionStationKey;
		
		/*****************/
		/*****************/
		
		Group() {
			size         = 0;
			detached     = false;
			maxRequests  = 0;
			minProcesses = 0;
			spawning     = false;
			analytics    = false;
			/*****************/
		}
	};
	
	struct ProcessInfo {
		ProcessPtr process;
		string groupName;
		unsigned long long startTime;
		time_t lastUsed;
		unsigned int sessions;
		unsigned int processed;
		ProcessInfoList::iterator iterator;
		ProcessInfoList::iterator ia_iterator;
		bool detached;
		ProcessMetrics metrics;
		
		/****************/
		/****************/
		
		ProcessInfo() {
			startTime  = SystemTime::getMsec();
			lastUsed   = 0;
			sessions   = 0;
			processed  = 0;
			detached   = false;
			/*****************/
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
		boost::timed_mutex lock;
		condition_variable_any newAppGroupCreatable;
		condition_variable_any globalQueuePositionBecameAvailable;
		
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
		
		void operator()(const StandardSession *session) {
			ProcessInfoPtr processInfo = this->processInfo.lock();
			if (processInfo == NULL || processInfo->detached) {
				return;
			}
			
			boost::timed_mutex::scoped_lock l(data->lock);
			if (processInfo->detached) {
				return;
			}
			
			GroupMap::iterator it;
			it = data->groups.find(processInfo->groupName);
			GroupPtr group = it->second;
			ProcessInfoList *processes = &group->processes;
			
			processInfo->processed++;
			
			if (group->maxRequests > 0 && processInfo->processed >= group->maxRequests) {
				P_DEBUG("MaxRequests for process " << processInfo->process->getPid() << " reached");
				processInfo->detached = true;
				processes->erase(processInfo->iterator);
				group->size--;
				if (processes->empty()) {
					Pool::detachGroupWithoutLock(data, group);
				}
				mutateCount(data, data->count - 1);
				if (processInfo->sessions == 0) {
					data->inactiveApps.erase(processInfo->ia_iterator);
				} else {
					mutateActive(data, data->active - 1);
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
					mutateActive(data, data->active - 1);
				}
			}
		}
	};
	
	AbstractSpawnManagerPtr spawnManager;
	AnalyticsLoggerPtr analyticsLogger;
	SharedDataPtr data;
	oxt::thread *cleanerThread;
	oxt::thread *analyticsCollectionThread;
	bool destroying;
	unsigned int maxIdleTime;
	unsigned int waitingOnGlobalQueue;
	condition_variable_any cleanerThreadSleeper;
	CachedFileStat cstat;
	FileChangeChecker fileChangeChecker;
	ProcessMetricsCollector processMetricsCollector;
	
	// Shortcuts for instance variables in SharedData. Saves typing in get()
	// and other methods.
	boost::timed_mutex &lock;
	condition_variable_any &newAppGroupCreatable;
	condition_variable_any &globalQueuePositionBecameAvailable;
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
		unsigned int expectedActive = 0;
		
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
			
			
			ProcessInfoList::const_iterator lit = processes->begin();
			for (; lit != processes->end(); lit++) {
				const ProcessInfoPtr &processInfo = *lit;
				
				// Invariants for ProcessInfo.
				P_ASSERT(processInfo->groupName == group->name, false,
					"groups['" << appRoot << "'].processes[x].groupName "
					"equals groups['" << appRoot << "'].name");
				P_ASSERT(!processInfo->detached, false,
					"groups['" << appRoot << "'].processes[x].detached is false");
				if (processInfo->sessions > 0) {
					expectedActive++;
				}
			}
			
			ProcessInfoList::const_iterator prev_lit;
			prev_lit = processes->begin();
			lit = prev_lit;
			lit++;
			for (; lit != processes->end(); lit++) {
				const ProcessInfoPtr &processInfo = *lit;
				
				// Invariants for ProcessInfo that depend on the previous item.
				if ((*prev_lit)->sessions > 0) {
					P_ASSERT(processInfo->sessions > 0, false,
						"groups['" << appRoot << "'].processes "
						"is sorted from nonactive to active");
				}
			}
		}
		P_ASSERT(totalSize == count, false, "(sum of all d.size in groups) == count");
		
		P_ASSERT(active == expectedActive, false,
			"active (" << active << ") == " << expectedActive);
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
	
	static void mutateActive(const SharedDataPtr &data, unsigned int value) {
		if (value < data->active) {
			data->newAppGroupCreatable.notify_all();
			data->globalQueuePositionBecameAvailable.notify_all();
		}
		data->active = value;
	}
	
	static void mutateCount(const SharedDataPtr &data, unsigned int value) {
		data->globalQueuePositionBecameAvailable.notify_all();
		data->count = value;
	}
	
	static void mutateMax(const SharedDataPtr &data, unsigned int value) {
		if (value > data->max) {
			data->newAppGroupCreatable.notify_all();
			data->globalQueuePositionBecameAvailable.notify_all();
		}
		data->max = value;
	}
	
	void mutateActive(unsigned int value) {
		Pool::mutateActive(data, value);
	}
	
	void mutateCount(unsigned int value) {
		Pool::mutateCount(data, value);
	}
	
	void mutateMax(unsigned int value) {
		Pool::mutateMax(data, value);
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
	
	bool spawningAllowed(const GroupPtr &group, const PoolOptions &options) const {
		return ( count < max ) &&
			( (maxPerApp == 0) || (group->size < maxPerApp) );
	}
	
	void dumpProcessInfoAsXml(const ProcessInfo *processInfo, bool includeSensitiveInformation,
	                          stringstream &result) const
	{
		result << "<process>";
		result << "<pid>" << processInfo->process->getPid() << "</pid>";
		result << "<gupid>" << processInfo->process->getGupid() << "</gupid>";
		result << "<sessions>" << processInfo->sessions << "</sessions>";
		result << "<processed>" << processInfo->processed << "</processed>";
		result << "<uptime>" << processInfo->uptime() << "</uptime>";
		if (processInfo->metrics.isValid()) {
			const ProcessMetrics &metrics = processInfo->metrics;
			result << "<has_metrics>true</has_metrics>";
			result << "<cpu>" << (int) metrics.cpu << "</cpu>";
			result << "<rss>" << metrics.rss << "</rss>";
			if (metrics.pss != -1) {
				result << "<pss>" << metrics.pss << "</pss>";
			}
			if (metrics.privateDirty != -1) {
				result << "<private_dirty>" << metrics.privateDirty << "</private_dirty>";
			}
			if (metrics.swap != -1) {
				result << "<swap>" << metrics.swap << "</swap>";
			}
			result << "<real_memory>" << metrics.realMemory() << "</real_memory>";
			result << "<vmsize>" << metrics.vmsize << "</vmsize>";
			result << "<process_group_id>" << metrics.processGroupId << "</process_group_id>";
			result << "<command>" << escapeForXml(metrics.command) << "</command>";
		}
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
	
	static void detachGroupWithoutLock(const SharedDataPtr &data, const GroupPtr &group) {
		TRACE_POINT();
		assert(!group->detached);
		
		ProcessInfoList *processes = &group->processes;
		ProcessInfoList::iterator list_it;
		
		for (list_it = processes->begin(); list_it != processes->end(); list_it++) {
			ProcessInfoPtr processInfo = *list_it;
			
			if (processInfo->sessions == 0) {
				data->inactiveApps.erase(processInfo->ia_iterator);
			} else {
				mutateActive(data, data->active - 1);
			}
			list_it--;
			processes->erase(processInfo->iterator);
			processInfo->detached = true;
			mutateCount(data, data->count - 1);
		}
		
		if (group->spawning) {
			UPDATE_TRACE_POINT();
			group->spawnerThread->interrupt_and_join();
			group->spawnerThread.reset();
			group->spawning = false;
		}
		
		group->detached = true;
		data->groups.erase(group->name);
	}
	
	void detachGroupWithoutLock(const GroupPtr &group) {
		Pool::detachGroupWithoutLock(data, group);
	}
	
	ProcessInfoPtr selectProcess(ProcessInfoList *processes, const PoolOptions &options,
		unique_lock<boost::timed_mutex> &l,
		this_thread::disable_interruption &di,
		this_thread::disable_syscall_interruption &dsi)
	{
		if (options.useGlobalQueue) {
			TRACE_POINT();
			waitingOnGlobalQueue++;
			this_thread::restore_interruption ri(di);
			this_thread::restore_syscall_interruption rsi(dsi);
			globalQueuePositionBecameAvailable.wait(l);
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
	
	void spawnInBackground(const GroupPtr &group, const PoolOptions &options) {
		assert(!group->detached);
		assert(!group->spawning);
		group->spawning = true;
		group->spawnerThread = ptr(new oxt::thread(
			boost::bind(&Pool::spawnerThreadCallback, this, group, options.own()),
			"ApplicationPool background spawner",
			SPAWNER_THREAD_STACK_SIZE
		));
	}
	
	void spawnerThreadCallback(GroupPtr group, PoolOptions options) {
		TRACE_POINT();
		
		while (true) {
			ProcessPtr process;
			
			try {
				UPDATE_TRACE_POINT();
				P_DEBUG("Background spawning a process for " << options.appRoot);
				process = spawnManager->spawn(options);
			} catch (const thread_interrupted &) {
				UPDATE_TRACE_POINT();
				interruptable_lock_guard<boost::timed_mutex> l(lock);
				this_thread::disable_interruption di;
				this_thread::disable_syscall_interruption dsi;
				group->spawning = false;
				group->spawnerThread.reset();
				return;
			} catch (const std::exception &e) {
				UPDATE_TRACE_POINT();
				P_DEBUG("Background spawning of " << options.appRoot <<
					" failed; removing entire group." <<
					" Error: " << e.what());
				interruptable_lock_guard<boost::timed_mutex> l(lock);
				this_thread::disable_interruption di;
				this_thread::disable_syscall_interruption dsi;
				if (!group->detached) {
					group->spawning = false;
					group->spawnerThread.reset();
					detachGroupWithoutLock(group);
				}
				return;
			}
			
			UPDATE_TRACE_POINT();
			interruptable_lock_guard<boost::timed_mutex> l(lock);
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			ProcessInfoPtr processInfo;
			
			processInfo = ptr(new ProcessInfo());
			processInfo->process = process;
			processInfo->groupName = options.getAppGroupName();
			
			group->processes.push_front(processInfo);
			processInfo->iterator = group->processes.begin();
			
			inactiveApps.push_back(processInfo);
			processInfo->ia_iterator = inactiveApps.end();
			processInfo->ia_iterator--;
			
			group->size++;
			mutateCount(count + 1);
			
			P_ASSERT_WITH_VOID_RETURN(verifyState(),
				"Background spawning: ApplicationPool state is valid:\n" <<
				inspectWithoutLock());
			
			if (group->size >= options.minProcesses
			 || !spawningAllowed(group, options)) {
				group->spawning = false;
				group->spawnerThread.reset();
				return;
			}
		}
	}
	
	bool detachWithoutLock(const string &detachKey) {
		TRACE_POINT();
		GroupMap::iterator group_it;
		GroupMap::iterator group_it_end = groups.end();
		
		for (group_it = groups.begin(); group_it != group_it_end; group_it++) {
			GroupPtr group = group_it->second;
			ProcessInfoList &processes = group->processes;
			ProcessInfoList::iterator process_info_it = processes.begin();
			ProcessInfoList::iterator process_info_it_end = processes.end();
			
			for (; process_info_it != process_info_it_end; process_info_it++) {
				ProcessInfoPtr processInfo = *process_info_it;
				if (processInfo->process->getDetachKey() == detachKey) {
					// Found a matching process.
					P_DEBUG("Detaching process " << processInfo->process->getPid());
					processInfo->detached = true;
					processes.erase(processInfo->iterator);
					group->size--;
					if (processes.empty()) {
						detachGroupWithoutLock(group);
					}
					if (processInfo->sessions == 0) {
						inactiveApps.erase(processInfo->ia_iterator);
					} else {
						mutateActive(active - 1);
					}
					mutateCount(count - 1);
					return true;
				}
			}
		}
		return false;
	}
	
	void cleanerThreadMainLoop() {
		this_thread::disable_syscall_interruption dsi;
		unique_lock<boost::timed_mutex> l(lock);
		try {
			while (!destroying && !this_thread::interruption_requested()) {
				if (maxIdleTime == 0) {
					cleanerThreadSleeper.wait(l);
					if (destroying) {
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
						if (destroying) {
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
						GroupPtr   group   = groups[processInfo->groupName];
						
						if (group->size > group->minProcesses) {
							ProcessInfoList *processes = &group->processes;
							
							P_DEBUG("Cleaning idle process " << process->getAppRoot() <<
								" (PID " << process->getPid() << ")");
							P_TRACE(2, "Group size = " << group->size << ", "
								"min processes = " << group->minProcesses);
							processes->erase(processInfo->iterator);
							processInfo->detached = true;
							
							ProcessInfoList::iterator prev = it;
							prev--;
							inactiveApps.erase(it);
							it = prev;
							
							group->size--;
							mutateCount(count - 1);
							
							if (processes->empty()) {
								detachGroupWithoutLock(group);
							}
						}
					}
				}
			}
		} catch (const std::exception &e) {
			P_ERROR("Uncaught exception: " << e.what());
		}
	}
	
	void analyticsCollectionThreadMainLoop() {
		/* Invariant inside this thread:
		 * analyticsLogger != NULL
		 */
		TRACE_POINT();
		try {
			syscalls::sleep(3);
			while (!this_thread::interruption_requested()) {
				this_thread::disable_interruption di;
				this_thread::disable_syscall_interruption dsi;
				vector<pid_t> pids;
				
				// Collect all the PIDs.
				{
					UPDATE_TRACE_POINT();
					lock_guard<boost::timed_mutex> l(lock);
					GroupMap::const_iterator group_it;
					GroupMap::const_iterator group_it_end = groups.end();
					
					UPDATE_TRACE_POINT();
					pids.reserve(count);
					for (group_it = groups.begin(); group_it != group_it_end; group_it++) {
						const GroupPtr &group = group_it->second;
						const ProcessInfoList &processes = group->processes;
						ProcessInfoList::const_iterator process_info_it = processes.begin();
						ProcessInfoList::const_iterator process_info_it_end = processes.end();
						
						for (; process_info_it != process_info_it_end; process_info_it++) {
							const ProcessInfoPtr &processInfo = *process_info_it;
							pids.push_back(processInfo->process->getPid());
						}
					}
				}
				
				try {
					// Now collect the process metrics and store them in the
					// data structures, and log the state into the analytics logs.
					UPDATE_TRACE_POINT();
					ProcessMetricMap allMetrics =
						processMetricsCollector.collect(pids);
					
					UPDATE_TRACE_POINT();
					lock_guard<boost::timed_mutex> l(lock);
					GroupMap::iterator group_it;
					GroupMap::iterator group_it_end = groups.end();
					
					UPDATE_TRACE_POINT();
					for (group_it = groups.begin(); group_it != group_it_end; group_it++) {
						GroupPtr &group = group_it->second;
						ProcessInfoList &processes = group->processes;
						ProcessInfoList::iterator process_info_it = processes.begin();
						ProcessInfoList::iterator process_info_it_end = processes.end();
						AnalyticsLogPtr log;
						stringstream xml;
						
						if (group->analytics && analyticsLogger != NULL) {
							ssize_t shared;
							log = analyticsLogger->newTransaction(group->name,
								"processes", group->unionStationKey);
							xml << "Processes: <processes>";
							xml << "<total_memory>" << allMetrics.totalMemory(shared) << "</total_memory>";
							if (shared != -1) {
								xml << "<total_shared_memory>" << shared << "</total_shared_memory>";
							}
						}
						for (; process_info_it != process_info_it_end; process_info_it++) {
							ProcessInfoPtr &processInfo = *process_info_it;
							ProcessMetricMap::const_iterator metrics_it =
								allMetrics.find(processInfo->process->getPid());
							if (metrics_it != allMetrics.end()) {
								processInfo->metrics = metrics_it->second;
							}
							if (log != NULL) {
								dumpProcessInfoAsXml(processInfo.get(),
									false, xml);
							}
						}
						if (log != NULL) {
							xml << "</processes>";
							log->message(xml.str());
						}
					}
				} catch (const ProcessMetricsCollector::ParseException &) {
					P_WARN("Unable to collect process metrics: cannot parse 'ps' output.");
				} catch (const thread_interrupted &) {
					throw;
				} catch (const std::exception &e) {
					P_WARN("Error while collecting process metrics: " << e.what());
				}
				
				pids.resize(0);
				
				// Sleep for about 4 seconds, aligned to seconds boundary
				// for saving power on laptops.
				UPDATE_TRACE_POINT();
				unsigned long long currentTime = SystemTime::getUsec();
				unsigned long long deadline =
					roundUp<unsigned long long>(currentTime, 1000000) + 4000000;
				syscalls::usleep(deadline - currentTime);
			}
		} catch (const thread_interrupted &) {
		} catch (const std::exception &e) {
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
	pair<ProcessInfoPtr, GroupPtr>
	checkoutWithoutLock(unique_lock<boost::timed_mutex> &l, const PoolOptions &options) {
		beginning_of_function:
		
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		const string &appRoot = options.appRoot;
		const string appGroupName = options.getAppGroupName();
		ProcessInfoPtr processInfo;
		GroupPtr group;
		ProcessInfoList *processes;
		
		try {
			GroupMap::iterator group_it = groups.find(appGroupName);
			
			if (needsRestart(appRoot, options)) {
				P_DEBUG("Restarting " << appGroupName);
				spawnManager->reload(appGroupName);
				if (group_it != groups.end()) {
					detachGroupWithoutLock(group_it->second);
					group_it = groups.end();
				}
			}
			
			if (group_it != groups.end()) {
				group = group_it->second;
				processes = &group->processes;
				
				if (processes->front()->sessions == 0) {
					processInfo = processes->front();
					processes->pop_front();
					processes->push_back(processInfo);
					processInfo->iterator = processes->end();
					processInfo->iterator--;
					inactiveApps.erase(processInfo->ia_iterator);
					mutateActive(active + 1);
				} else {
					if (!group->spawning && spawningAllowed(group, options)) {
						P_DEBUG("Spawning another process for " << appRoot <<
							" in the background in order to handle the load");
						spawnInBackground(group, options);
					}
					processInfo = selectProcess(processes, options, l, di, dsi);
					if (processInfo == NULL) {
						goto beginning_of_function;
					}
				}
			} else {
				P_DEBUG("Spawning a process for " << appRoot <<
					" because there are none for this app group");
				if (active >= max) {
					UPDATE_TRACE_POINT();
					this_thread::restore_interruption ri(di);
					this_thread::restore_syscall_interruption rsi(dsi);
					newAppGroupCreatable.wait(l);
					goto beginning_of_function;
				} else if (count == max) {
					processInfo = inactiveApps.front();
					P_DEBUG("Killing process " << processInfo->process->getPid() <<
						" because an extra slot is necessary for spawning");
					inactiveApps.pop_front();
					processInfo->detached = true;
					group = groups[processInfo->groupName];
					processes = &group->processes;
					processes->erase(processInfo->iterator);
					if (processes->empty()) {
						detachGroupWithoutLock(group);
					} else {
						group->size--;
					}
					mutateCount(count - 1);
				}
				
				UPDATE_TRACE_POINT();
				processInfo = ptr(new ProcessInfo());
				{
					this_thread::restore_interruption ri(di);
					this_thread::restore_syscall_interruption rsi(dsi);
					processInfo->process = spawnManager->spawn(options);
				}
				processInfo->groupName = appGroupName;
				processInfo->sessions = 0;
				group = ptr(new Group());
				group->name = appGroupName;
				group->appRoot = appRoot;
				group->size = 1;
				groups[appGroupName] = group;
				processes = &group->processes;
				processes->push_back(processInfo);
				processInfo->iterator = processes->end();
				processInfo->iterator--;
				mutateCount(count + 1);
				mutateActive(active + 1);
				if (options.minProcesses > 1 && spawningAllowed(group, options)) {
					spawnInBackground(group, options);
				}
			}
		} catch (const SpawnException &e) {
			string message("Cannot spawn application '");
			message.append(appGroupName);
			message.append("': ");
			message.append(e.what());
			if (e.hasErrorPage()) {
				throw SpawnException(message, e.getErrorPage());
			} else {
				throw SpawnException(message);
			}
		} catch (const thread_interrupted &) {
			throw;
		} catch (const std::exception &e) {
			string message("Cannot spawn application '");
			message.append(appGroupName);
			message.append("': ");
			message.append(e.what());
			throw SpawnException(message);
		}
		
		group->maxRequests  = options.maxRequests;
		group->minProcesses = options.minProcesses;
		group->environment  = options.environment;
		group->analytics    = options.log != NULL;
		if (group->analytics) {
			group->unionStationKey = options.log->getUnionStationKey();
		}
		
		processInfo->lastUsed = time(NULL);
		processInfo->sessions++;
		
		return make_pair(processInfo, group);
	}
	
	/** @throws boost::thread_resource_error */
	void initialize(const AnalyticsLoggerPtr &analyticsLogger)
	{
		destroying = false;
		max = DEFAULT_MAX_POOL_SIZE;
		count = 0;
		active = 0;
		waitingOnGlobalQueue = 0;
		maxPerApp = DEFAULT_MAX_INSTANCES_PER_APP;
		maxIdleTime = DEFAULT_POOL_IDLE_TIME;
		cleanerThread = new oxt::thread(
			bind(&Pool::cleanerThreadMainLoop, this),
			"ApplicationPool cleaner",
			CLEANER_THREAD_STACK_SIZE
		);
		
		if (analyticsLogger != NULL) {
			this->analyticsLogger = analyticsLogger;
			analyticsCollectionThread = new oxt::thread(
				bind(&Pool::analyticsCollectionThreadMainLoop, this),
				"ApplicationPool analytics collector",
				ANALYTICS_COLLECTION_THREAD_STACK_SIZE
			);
		} else {
			analyticsCollectionThread = NULL;
		}
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
	     const AccountsDatabasePtr &accountsDatabase = AccountsDatabasePtr(),
	     const string &rubyCommand = "ruby",
	     const AnalyticsLoggerPtr &analyticsLogger = AnalyticsLoggerPtr(),
	     int logLevel = 0,
	     const string &debugLogFile = ""
	) : data(new SharedData()),
		cstat(DEFAULT_MAX_POOL_SIZE),
		lock(data->lock),
		newAppGroupCreatable(data->newAppGroupCreatable),
		globalQueuePositionBecameAvailable(data->globalQueuePositionBecameAvailable),
		groups(data->groups),
		max(data->max),
		count(data->count),
		active(data->active),
		maxPerApp(data->maxPerApp),
		inactiveApps(data->inactiveApps)
	{
		TRACE_POINT();
		
		this->spawnManager = ptr(new SpawnManager(spawnServerCommand, generation,
			accountsDatabase, rubyCommand, analyticsLogger, logLevel,
			debugLogFile));
		initialize(analyticsLogger);
	}
	
	/**
	 * Create a new ApplicationPool::Pool object and initialize it with
	 * the given spawn manager.
	 *
	 * @throws boost::thread_resource_error Cannot spawn a new thread.
	 */
	Pool(AbstractSpawnManagerPtr spawnManager,
	     const AnalyticsLoggerPtr &analyticsLogger = AnalyticsLoggerPtr()
	) : data(new SharedData()),
	     cstat(DEFAULT_MAX_POOL_SIZE),
	     lock(data->lock),
	     newAppGroupCreatable(data->newAppGroupCreatable),
	     globalQueuePositionBecameAvailable(data->globalQueuePositionBecameAvailable),
	     groups(data->groups),
	     max(data->max),
	     count(data->count),
	     active(data->active),
	     maxPerApp(data->maxPerApp),
	     inactiveApps(data->inactiveApps)
	{
		TRACE_POINT();
		this->spawnManager = spawnManager;
		initialize(analyticsLogger);
	}
	
	virtual ~Pool() {
		TRACE_POINT();
		this_thread::disable_interruption di;
		{
			lock_guard<boost::timed_mutex> l(lock);
			destroying = true;
			cleanerThreadSleeper.notify_one();
			while (!groups.empty()) {
				detachGroupWithoutLock(groups.begin()->second);
			}
		}
		cleanerThread->join();
		delete cleanerThread;
		
		if (analyticsCollectionThread != NULL) {
			analyticsCollectionThread->interrupt_and_join();
			delete analyticsCollectionThread;
		}
	}
	
	virtual SessionPtr get(const string &appRoot) {
		return ApplicationPool::Interface::get(appRoot);
	}
	
	virtual SessionPtr get(const PoolOptions &options) {
		TRACE_POINT();
		unsigned int attempt = 0;
		
		while (true) {
			attempt++;
			
			pair<ProcessInfoPtr, GroupPtr> p;
			{
				unique_lock<boost::timed_mutex> l(lock);
				p = checkoutWithoutLock(l, options);
				P_ASSERT(verifyState(), SessionPtr(),
					"get(): ApplicationPool state is valid:\n" << inspectWithoutLock());
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
				P_TRACE(2, "Exception occurred while connecting to checked out "
					"process " << processInfo->process->getPid() << ": " <<
					e.what());
				{
					unique_lock<boost::timed_mutex> l(lock);
					detachWithoutLock(processInfo->process->getDetachKey());
					processInfo->sessions--;
					P_ASSERT(verifyState(), SessionPtr(),
						"get(): ApplicationPool state is valid:\n" << inspectWithoutLock());
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
			
			} catch (const thread_interrupted &) {
				throw;
			
			} catch (std::exception &e) {
				P_TRACE(2, "Exception occurred while connecting to checked out "
					"process " << processInfo->process->getPid() << ": " <<
					e.what());
				{
					unique_lock<boost::timed_mutex> l(lock);
					detachWithoutLock(processInfo->process->getDetachKey());
					processInfo->sessions--;
					P_ASSERT(verifyState(), SessionPtr(),
						"get(): ApplicationPool state is valid:\n" << inspectWithoutLock());
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
		TRACE_POINT();
		unique_lock<boost::timed_mutex> l(lock);
		return detachWithoutLock(detachKey);
	}
	
	virtual void clear() {
		lock_guard<boost::timed_mutex> l(lock);
		P_DEBUG("Clearing pool");
		
		while (!groups.empty()) {
			detachGroupWithoutLock(groups.begin()->second);
		}
		newAppGroupCreatable.notify_all();
		globalQueuePositionBecameAvailable.notify_all();
		
		P_ASSERT_WITH_VOID_RETURN(groups.size() == 0,
			"groups.size() == 0\n" << inspectWithoutLock());
		P_ASSERT_WITH_VOID_RETURN(inactiveApps.size() == 0,
			"inactiveApps.size() == 0\n" << inspectWithoutLock());
		P_ASSERT_WITH_VOID_RETURN(count == 0,
			"count == 0\n" << inspectWithoutLock());
		P_ASSERT_WITH_VOID_RETURN(active == 0,
			"active == 0\n" << inspectWithoutLock());
		P_ASSERT_WITH_VOID_RETURN(verifyState(),
			"ApplicationPool state is valid:\n" << inspectWithoutLock());
		
		// TODO: clear cstat and fileChangeChecker, and reload all spawner servers.
	}
	
	virtual void setMaxIdleTime(unsigned int seconds) {
		lock_guard<boost::timed_mutex> l(lock);
		maxIdleTime = seconds;
		cleanerThreadSleeper.notify_one();
	}
	
	virtual void setMax(unsigned int max) {
		lock_guard<boost::timed_mutex> l(lock);
		mutateMax(max);
	}
	
	virtual unsigned int getActive() const {
		lock_guard<boost::timed_mutex> l(lock);
		return active;
	}
	
	virtual unsigned int getCount() const {
		lock_guard<boost::timed_mutex> l(lock);
		return count;
	}
	
	virtual unsigned int getGlobalQueueSize() const {
		lock_guard<boost::timed_mutex> l(lock);
		return waitingOnGlobalQueue;
	}
	
	virtual void setMaxPerApp(unsigned int maxPerApp) {
		lock_guard<boost::timed_mutex> l(lock);
		this->maxPerApp = maxPerApp;
		newAppGroupCreatable.notify_all();
		globalQueuePositionBecameAvailable.notify_all();
	}
	
	virtual pid_t getSpawnServerPid() const {
		return spawnManager->getServerPid();
	}
	
	virtual string inspect() const {
		lock_guard<boost::timed_mutex> l(lock);
		return inspectWithoutLock();
	}
	
	virtual string toXml(bool includeSensitiveInformation = true) const {
		lock_guard<boost::timed_mutex> l(lock);
		stringstream result;
		GroupMap::const_iterator it;
		
		result << "<?xml version=\"1.0\" encoding=\"iso8859-1\" ?>\n";
		result << "<info>";
		
		result << "<active>" << toString(active) << "</active>";
		result << "<count>" << toString(count) << "</count>";
		result << "<max>" << toString(max) << "</max>";
		result << "<global_queue_size>" << toString(waitingOnGlobalQueue) << "</global_queue_size>";
		
		result << "<groups>";
		for (it = groups.begin(); it != groups.end(); it++) {
			Group *group = it->second.get();
			ProcessInfoList *processes = &group->processes;
			ProcessInfoList::const_iterator lit;
			
			result << "<group>";
			result << "<app_root>" << escapeForXml(group->appRoot) << "</app_root>";
			result << "<name>" << escapeForXml(group->name) << "</name>";
			result << "<environment>" << escapeForXml(group->environment) << "</environment>";
			
			result << "<processes>";
			for (lit = processes->begin(); lit != processes->end(); lit++) {
				ProcessInfo *processInfo = lit->get();
				dumpProcessInfoAsXml(processInfo, includeSensitiveInformation, result);
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

