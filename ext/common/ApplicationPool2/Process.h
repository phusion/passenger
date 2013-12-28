/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2013 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL_PROCESS_H_
#define _PASSENGER_APPLICATION_POOL_PROCESS_H_

#include <string>
#include <list>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/macros.hpp>
#include <sys/types.h>
#include <cstdio>
#include <climits>
#include <cassert>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/Socket.h>
#include <ApplicationPool2/Session.h>
#include <ApplicationPool2/PipeWatcher.h>
#include <Constants.h>
#include <FileDescriptor.h>
#include <SafeLibev.h>
#include <Logging.h>
#include <Utils/PriorityQueue.h>
#include <Utils/SystemTime.h>
#include <Utils/StrIntUtils.h>
#include <Utils/ProcessMetricsCollector.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


class ProcessList: public list<ProcessPtr> {
public:
	ProcessPtr &get(unsigned int index) {
		iterator it = begin(), end = this->end();
		unsigned int i = 0;
		while (i != index && it != end) {
			i++;
			it++;
		}
		if (it == end) {
			throw RuntimeException("Index out of bounds");
		} else {
			return *it;
		}
	}
	
	ProcessPtr &operator[](unsigned int index) {
		return get(index);
	}
	
	iterator last_iterator() {
		if (empty()) {
			return end();
		} else {
			iterator last = end();
			last--;
			return last;
		}
	}
};

/**
 * Represents an application process, as spawned by a Spawner. Every Process has
 * a PID, an admin socket and a list of sockets on which it listens for
 * connections. A Process is usually contained inside a Group.
 *
 * The admin socket, an anonymous Unix domain socket, is mapped to the process's
 * STDIN and STDOUT and has two functions.
 *
 *  1. It acts as the main communication channel with the process. Commands are
 *     sent to and responses are received from it.
 *  2. It's used for garbage collection: closing the STDIN part causes the process
 *     to gracefully terminate itself.
 *
 * Except for the otherwise documented parts, this class is not thread-safe,
 * so only use within the Pool lock.
 *
 * ## Normal usage
 *
 *  1. Create a session with newSession().
 *  2. Initiate the session by calling initiate() on it.
 *  3. Perform I/O through session->fd().
 *  4. When done, close the session by calling close() on it.
 *  5. Call process.sessionClosed().
 *
 * ## Life time
 *
 * A Process object lives until the containing Group calls `detach(process)`,
 * which indicates that it wants this Process to should down. This causes
 * the Process to enter the `detached() == true` state. Processes in this
 * state are stored in the `detachedProcesses` collection in the Group and
 * are no longer eligible for receiving requests. They will be removed from
 * the Group and destroyed when all of the following applies:
 * 
 *  1. the OS process is gone.
 *  2. `sessions == 0`
 *
 * This means that a Group outlives all its Processes, a Process outlives all
 * its Sessions, and a Process also outlives the OS process.
 */
class Process: public boost::enable_shared_from_this<Process> {
private:
	friend class Group;
	
	/** A mutex to protect access to `lifeStatus`. */
	mutable boost::mutex lifetimeSyncher;

	/** Group inside the Pool that this Process belongs to.
	 * Should never be NULL because a Group should outlive all of its Processes.
	 * Read-only; only set once during initialization.
	 */
	boost::weak_ptr<Group> group;
	
	/** A subset of 'sockets': all sockets that speak the
	 * "session" protocol, sorted by socket.busyness(). */
	PriorityQueue<Socket> sessionSockets;
	
	/** The iterator inside the associated Group's process list. */
	ProcessList::iterator it;
	/** The handle inside the associated Group's process priority queue. */
	PriorityQueue<Process>::Handle pqHandle;

	static bool
	isZombie(pid_t pid) {
		string filename = "/proc/" + toString(pid) + "/status";
		FILE *f = fopen(filename.c_str(), "r");
		if (f == NULL) {
			// Don't know.
			return false;
		}

		bool result = false;
		while (!feof(f)) {
			char buf[512];
			const char *line;

			line = fgets(buf, sizeof(buf), f);
			if (line == NULL) {
				break;
			}
			if (strcmp(line, "State:	Z (zombie)\n") == 0) {
				// Is a zombie.
				result = true;
				break;
			}
		}
		fclose(f);
		return result;
	}

	void indexSessionSockets() {
		SocketList::iterator it;
		concurrency = 0;
		for (it = sockets->begin(); it != sockets->end(); it++) {
			Socket *socket = &(*it);
			if (socket->protocol == "session" || socket->protocol == "http_session") {
				socket->pqHandle = sessionSockets.push(socket, socket->busyness());
				if (concurrency != -1) {
					if (socket->concurrency == 0) {
						// If one of the sockets has a concurrency of
						// 0 (unlimited) then we mark this entire Process
						// as having a concurrency of 0.
						concurrency = -1;
					} else {
						concurrency += socket->concurrency;
					}
				}
			}
		}
		if (concurrency == -1) {
			concurrency = 0;
		}
	}
	
public:
	/*************************************************************
	 * Read-only fields, set once during initialization and never
	 * written to again. Reading is thread-safe.
	 *************************************************************/
	
	/** The libev event loop to use. */
	SafeLibev * const libev;
	/** Process PID. */
	pid_t pid;
	/** An ID that uniquely identifies this Process in the Group, for
	 * use in implementing sticky sessions. Set by Group::attach(). */
	unsigned int stickySessionId;
	/** UUID for this process, randomly generated and extremely unlikely to ever
	 * appear again in this universe. */
	string gupid;
	string connectPassword;
	/** Admin socket, see class description. */
	FileDescriptor adminSocket;
	/** The sockets that this Process listens on for connections. */
	SocketListPtr sockets;
	/** Time at which the Spawner that created this process was created.
	 * Microseconds resolution. */
	unsigned long long spawnerCreationTime;
	/** Time at which we started spawning this process. Microseconds resolution. */
	unsigned long long spawnStartTime;
	/** The maximum amount of concurrent sessions this process can handle.
	 * 0 means unlimited. */
	int concurrency;
	/** If true, then indicates that this Process does not refer to a real OS
	 * process. The sockets in the socket list are fake and need not be deleted,
	 * the admin socket need not be closed, etc.
	 */
	bool dummy;
	/** Whether it is required that triggerShutdown() and cleanup() must be called
	 * before destroying this Process. Normally true, except for dummy Process
	 * objects created by Pool::asyncGet() with options.noop == true, because those
	 * processes are never added to Group.enabledProcesses.
	 */
	bool requiresShutdown;
	
	/*************************************************************
	 * Information used by Pool. Do not write to these from
	 * outside the Pool. If you read these make sure the Pool
	 * isn't concurrently modifying.
	 *************************************************************/
	
	/** Time at which we finished spawning this process, i.e. when this
	 * process was finished initializing. Microseconds resolution.
	 */
	unsigned long long spawnEndTime;
	/** Last time when a session was opened for this Process. */
	unsigned long long lastUsed;
	/** Number of sessions currently open.
	 * @invariant session >= 0
	 */
	int sessions;
	/** Number of sessions opened so far. */
	unsigned int processed;
	/** Do not access directly, always use `isAlive()`/`isDead()`/`getLifeStatus()` or
	 * through `lifetimeSyncher`. */
	enum LifeStatus {
		/** Up and operational. */
		ALIVE,
		/** This process has been detached, and the detached processes checker has
		 * verified that there are no active sessions left and has told the process
		 * to shut down. In this state we're supposed to wait until the process
		 * has actually shutdown, after which cleanup() must be called. */
		SHUTDOWN_TRIGGERED,
		/**
		 * The process has exited and cleanup() has been called. In this state,
		 * this object is no longer usable.
		 */
		DEAD
	} lifeStatus;
	enum EnabledStatus {
		/** Up and operational. */
		ENABLED,
		/** Process is being disabled. The containing Group is waiting for
		 * all sessions on this Process to finish. It may in some corner
		 * cases still be selected for processing requests.
		 */
		DISABLING,
		/** Process is fully disabled and should not be handling any
		 * requests. It *may* still handle some requests, e.g. by
		 * the Out-of-Band-Work trigger.
		 */
		DISABLED,
		/**
		 * Process has been detached. It will be removed from the Group
		 * as soon we have detected that the OS process has exited. Detached
		 * processes are allowed to finish their requests, but are not
		 * eligible for new requests.
		 */
		DETACHED
	} enabled;
	enum OobwStatus {
		/** Process is not using out-of-band work. */
		OOBW_NOT_ACTIVE,
		/** The process has requested out-of-band work. At some point, the code
		 * will see this and set the status to OOBW_IN_PROGRESS. */
		OOBW_REQUESTED,
		/** An out-of-band work is in progress. We need to wait until all
		 * sessions have ended and the process has been disabled before the
		 * out-of-band work can be performed. */
		OOBW_IN_PROGRESS,
	} oobwStatus;
	/** Caches whether or not the OS process still exists. */
	mutable bool m_osProcessExists;
	/** Time at which shutdown began. */
	time_t shutdownStartTime;
	/** Collected by Pool::collectAnalytics(). */
	ProcessMetrics metrics;
	
	Process(const SafeLibevPtr _libev,
		pid_t _pid,
		const string &_gupid,
		const string &_connectPassword,
		const FileDescriptor &_adminSocket,
		/** Pipe on which this process outputs errors. Mapped to the process's STDERR.
		 * Only Processes spawned by DirectSpawner have this set.
		 * SmartSpawner-spawned Processes use the same STDERR as their parent preloader processes.
		 */
		const FileDescriptor &_errorPipe,
		const SocketListPtr &_sockets,
		unsigned long long _spawnerCreationTime,
		unsigned long long _spawnStartTime,
		const SpawnerConfigPtr &_config = SpawnerConfigPtr())
		: pqHandle(NULL),
		  libev(_libev.get()),
		  pid(_pid),
		  stickySessionId(0),
		  gupid(_gupid),
		  connectPassword(_connectPassword),
		  adminSocket(_adminSocket),
		  sockets(_sockets),
		  spawnerCreationTime(_spawnerCreationTime),
		  spawnStartTime(_spawnStartTime),
		  concurrency(0),
		  dummy(false),
		  requiresShutdown(true),
		  sessions(0),
		  processed(0),
		  lifeStatus(ALIVE),
		  enabled(ENABLED),
		  oobwStatus(OOBW_NOT_ACTIVE),
		  m_osProcessExists(true),
		  shutdownStartTime(0)
	{
		SpawnerConfigPtr config;
		if (_config == NULL) {
			config = boost::make_shared<SpawnerConfig>();
		} else {
			config = _config;
		}

		if (_adminSocket != -1) {
			PipeWatcherPtr watcher = boost::make_shared<PipeWatcher>(_adminSocket,
				"stdout", pid);
			watcher->initialize();
			watcher->start();
		}
		if (_errorPipe != -1) {
			PipeWatcherPtr watcher = boost::make_shared<PipeWatcher>(_errorPipe,
				"stderr", pid);
			watcher->initialize();
			watcher->start();
		}
		
		if (OXT_LIKELY(sockets != NULL)) {
			indexSessionSockets();
		}
		
		lastUsed      = SystemTime::getUsec();
		spawnEndTime  = lastUsed;
	}
	
	~Process() {
		if (OXT_UNLIKELY(!isDead() && requiresShutdown)) {
			P_BUG("You must call Process::triggerShutdown() and Process::cleanup() before actually "
				"destroying the Process object.");
		}
	}

	static void forceTriggerShutdownAndCleanup(ProcessPtr process) {
		if (process != NULL) {
			process->triggerShutdown();
			// Pretend like the OS process has exited so
			// that the canCleanup() precondition is true.
			process->m_osProcessExists = false;
			process->cleanup();
		}
	}

	/**
	 * Thread-safe.
	 * @pre getLifeState() != SHUT_DOWN
	 * @post result != NULL
	 */
	const GroupPtr getGroup() const {
		assert(!isDead());
		return group.lock();
	}
	
	void setGroup(const GroupPtr &group) {
		assert(this->group.lock() == NULL || this->group.lock() == group);
		this->group = group;
	}

	/**
	 * Thread-safe.
	 * @pre getLifeState() != DEAD
	 * @post result != NULL
	 */
	SuperGroupPtr getSuperGroup() const;

	// Thread-safe.
	bool isAlive() const {
		boost::lock_guard<boost::mutex> lock(lifetimeSyncher);
		return lifeStatus == ALIVE;
	}

	// Thread-safe.
	bool hasTriggeredShutdown() const {
		boost::lock_guard<boost::mutex> lock(lifetimeSyncher);
		return lifeStatus == SHUTDOWN_TRIGGERED;
	}

	// Thread-safe.
	bool isDead() const {
		boost::lock_guard<boost::mutex> lock(lifetimeSyncher);
		return lifeStatus == DEAD;
	}

	// Thread-safe.
	LifeStatus getLifeStatus() const {
		boost::lock_guard<boost::mutex> lock(lifetimeSyncher);
		return lifeStatus;
	}

	bool canTriggerShutdown() const {
		return getLifeStatus() == ALIVE && sessions == 0;
	}

	void triggerShutdown() {
		assert(canTriggerShutdown());
		{
			boost::lock_guard<boost::mutex> lock(lifetimeSyncher);
			assert(lifeStatus == ALIVE);
			lifeStatus = SHUTDOWN_TRIGGERED;
			shutdownStartTime = SystemTime::get();
		}
		if (!dummy) {
			syscalls::shutdown(adminSocket, SHUT_WR);
		}
	}

	bool shutdownTimeoutExpired() const {
		return SystemTime::get() >= shutdownStartTime + PROCESS_SHUTDOWN_TIMEOUT;
	}

	bool canCleanup() const {
		return getLifeStatus() == SHUTDOWN_TRIGGERED && !osProcessExists();
	}

	void cleanup() {
		assert(canCleanup());

		P_TRACE(2, "Cleaning up process " << inspect());
		if (!dummy) {
			if (OXT_LIKELY(sockets != NULL)) {
				SocketList::const_iterator it, end = sockets->end();
				for (it = sockets->begin(); it != end; it++) {
					if (getSocketAddressType(it->address) == SAT_UNIX) {
						string filename = parseUnixSocketAddress(it->address);
						syscalls::unlink(filename.c_str());
					}
				}
			}
		}

		boost::lock_guard<boost::mutex> lock(lifetimeSyncher);
		lifeStatus = DEAD;
	}

	/** Checks whether the OS process exists.
	 * Once it has been detected that it doesn't, that event is remembered
	 * so that we don't accidentally ping any new processes that have the
	 * same PID.
	 */
	bool osProcessExists() const {
		if (!dummy && m_osProcessExists) {
			if (syscalls::kill(pid, 0) == 0) {
				/* On some environments, e.g. Heroku, the init process does
				 * not properly reap adopted zombie processes, which can interfere
				 * with our process existance check. To work around this, we
				 * explicitly check whether or not the process has become a zombie.
				 */
				m_osProcessExists = !isZombie(pid);
			} else {
				m_osProcessExists = errno != ESRCH;
			}
			return m_osProcessExists;
		} else {
			return false;
		}
	}

	/** Kill the OS process with the given signal. */
	int kill(int signo) {
		if (osProcessExists()) {
			return syscalls::kill(pid, signo);
		} else {
			return 0;
		}
	}
	
	int busyness() const {
		/* Different processes within a Group may have different
		 * 'concurrency' values. We want:
		 * - Group.pqueue to sort the processes from least used to most used.
		 * - to give processes with concurrency == 0 more priority over processes
		 *   with concurrency > 0.
		 * Therefore, we describe our busyness as a percentage of 'concurrency', with
		 * the percentage value in [0..INT_MAX] instead of [0..1].
		 */
		if (concurrency == 0) {
			// Allows Group.pqueue to give idle sockets more priority.
			if (sessions == 0) {
				return 0;
			} else {
				return 1;
			}
		} else {
			return (int) (((long long) sessions * INT_MAX) / (double) concurrency);
		}
	}
	
	/**
	 * Whether we've reached the maximum number of concurrent sessions for this
	 * process.
	 */
	bool isTotallyBusy() const {
		return concurrency != 0 && sessions >= concurrency;
	}

	/**
	 * Whether a get() request can be routed to this process, assuming that
	 * the sticky session ID (if any) matches. This is only not the case
	 * if this process is totally busy.
	 */
	bool canBeRoutedTo() const {
		return !isTotallyBusy();
	}
	
	/**
	 * Create a new communication session with this process. This will connect to one
	 * of the session sockets or reuse an existing connection. See Session for
	 * more information about sessions.
	 *
	 * One SHOULD call sessionClosed() when one's done with the session.
	 * Failure to do so will mess up internal statistics but will otherwise
	 * not result in any harmful behavior.
	 */
	SessionPtr newSession() {
		Socket *socket = sessionSockets.pop();
		if (socket->isTotallyBusy()) {
			return SessionPtr();
		} else {
			socket->sessions++;
			this->sessions++;
			socket->pqHandle = sessionSockets.push(socket, socket->busyness());
			lastUsed = SystemTime::getUsec();
			return boost::make_shared<Session>(shared_from_this(), socket);
		}
	}
	
	void sessionClosed(Session *session) {
		Socket *socket = session->getSocket();
		
		assert(socket->sessions > 0);
		assert(sessions > 0);
		
		socket->sessions--;
		this->sessions--;
		processed++;
		sessionSockets.decrease(socket->pqHandle, socket->busyness());
		assert(!isTotallyBusy());
	}

	/**
	 * Returns the uptime of this process so far, as a string.
	 */
	string uptime() const {
		return distanceOfTimeInWords(spawnEndTime / 1000000);
	}

	string inspect() const;

	template<typename Stream>
	void inspectXml(Stream &stream, bool includeSockets = true) const {
		stream << "<pid>" << pid << "</pid>";
		stream << "<sticky_session_id>" << stickySessionId << "</sticky_session_id>";
		stream << "<gupid>" << gupid << "</gupid>";
		stream << "<connect_password>" << connectPassword << "</connect_password>";
		stream << "<concurrency>" << concurrency << "</concurrency>";
		stream << "<sessions>" << sessions << "</sessions>";
		stream << "<busyness>" << busyness() << "</busyness>";
		stream << "<processed>" << processed << "</processed>";
		stream << "<spawner_creation_time>" << spawnerCreationTime << "</spawner_creation_time>";
		stream << "<spawn_start_time>" << spawnStartTime << "</spawn_start_time>";
		stream << "<spawn_end_time>" << spawnEndTime << "</spawn_end_time>";
		stream << "<last_used>" << lastUsed << "</last_used>";
		stream << "<uptime>" << uptime() << "</uptime>";
		switch (lifeStatus) {
		case ALIVE:
			stream << "<life_status>ALIVE</life_status>";
			break;
		case SHUTDOWN_TRIGGERED:
			stream << "<life_status>SHUTDOWN_TRIGGERED</life_status>";
			break;
		case DEAD:
			stream << "<life_status>DEAD</life_status>";
			break;
		default:
			P_BUG("Unknown 'lifeStatus' state " << (int) lifeStatus);
		}
		switch (enabled) {
		case ENABLED:
			stream << "<enabled>ENABLED</enabled>";
			break;
		case DISABLING:
			stream << "<enabled>DISABLING</enabled>";
			break;
		case DISABLED:
			stream << "<enabled>DISABLED</enabled>";
			break;
		case DETACHED:
			stream << "<enabled>DETACHED</enabled>";
			break;
		default:
			P_BUG("Unknown 'enabled' state " << (int) enabled);
		}
		if (metrics.isValid()) {
			stream << "<has_metrics>true</has_metrics>";
			stream << "<cpu>" << (int) metrics.cpu << "</cpu>";
			stream << "<rss>" << metrics.rss << "</rss>";
			stream << "<pss>" << metrics.pss << "</pss>";
			stream << "<private_dirty>" << metrics.privateDirty << "</private_dirty>";
			stream << "<swap>" << metrics.swap << "</swap>";
			stream << "<real_memory>" << metrics.realMemory() << "</real_memory>";
			stream << "<vmsize>" << metrics.vmsize << "</vmsize>";
			stream << "<process_group_id>" << metrics.processGroupId << "</process_group_id>";
			stream << "<command>" << escapeForXml(metrics.command) << "</command>";
		}
		if (includeSockets) {
			SocketList::const_iterator it;

			stream << "<sockets>";
			for (it = sockets->begin(); it != sockets->end(); it++) {
				const Socket &socket = *it;
				stream << "<socket>";
				stream << "<name>" << escapeForXml(socket.name) << "</name>";
				stream << "<address>" << escapeForXml(socket.address) << "</address>";
				stream << "<protocol>" << escapeForXml(socket.protocol) << "</protocol>";
				stream << "<concurrency>" << socket.concurrency << "</concurrency>";
				stream << "<sessions>" << socket.sessions << "</sessions>";
				stream << "</socket>";
			}
			stream << "</sockets>";
		}
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_PROCESS_H_ */
