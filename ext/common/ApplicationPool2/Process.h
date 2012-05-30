/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2011, 2012 Phusion
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
#include <oxt/macros.hpp>
#include <sys/types.h>
#include <cstdio>
#include <climits>
#include <cassert>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/Socket.h>
#include <ApplicationPool2/Session.h>
#include <ApplicationPool2/PipeWatcher.h>
#include <FileDescriptor.h>
#include <SafeLibev.h>
#include <Logging.h>
#include <Utils/PriorityQueue.h>
#include <Utils/SystemTime.h>
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
 * 1. It acts as the main communication channel with the process. Commands are
 *    sent to and responses are received from it.
 * 2. It's used for garbage collection: closing it causes the Process to gracefully
 *    terminate itself.
 *
 * Except for the otherwise documented parts, this class is not thread-safe,
 * so only use within the Pool lock.
 *
 * == Normal usage
 *
 * 1. Create a session with newSession().
 * 2. Initiate the session by calling initiate() on it.
 * 3. Perform I/O through session->fd().
 * 4. When done, close the session by calling close() on it.
 * 5. Call process.sessionClosed().
 */
class Process: public enable_shared_from_this<Process> {
private:
	friend class Group;
	
	/** A mutex to protect access to 'group'. */
	mutable boost::mutex backrefSyncher;
	/** Group inside the Pool that this Process belongs to. */
	weak_ptr<Group> group;
	
	/** A subset of 'sockets': all sockets that speak the
	 * "session" protocol, sorted by socket.usage(). */
	PriorityQueue<Socket> sessionSockets;
	
	/** The iterator inside the associated Group's process list. */
	ProcessList::iterator it;
	/** The handle inside the associated Group's process priority queue. */
	PriorityQueue<Process>::Handle pqHandle;
	
	void indexSessionSockets() {
		SocketList::iterator it;
		concurrency = 0;
		for (it = sockets->begin(); it != sockets->end(); it++) {
			Socket *socket = &(*it);
			if (socket->protocol == "session") {
				socket->pqHandle = sessionSockets.push(socket, socket->usage());
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
	/** UUID for this process, randomly generated and will never appear again. */
	string gupid;
	string connectPassword;
	/** Admin socket, see class description. */
	FileDescriptor adminSocket;
	PipeWatcherPtr adminSocketWatcher;
	PipeWatcherPtr errorPipeWatcher;
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
	enum {
		ENABLED,
		DISABLING,
		DISABLED
	} enabled;
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
		/** Whether to automatically forward data from errorPipe to our STDERR. */
		bool _forwardStderr = false)
		: libev(_libev.get()),
		  pid(_pid),
		  gupid(_gupid),
		  connectPassword(_connectPassword),
		  adminSocket(_adminSocket),
		  sockets(_sockets),
		  spawnerCreationTime(_spawnerCreationTime),
		  spawnStartTime(_spawnStartTime),
		  sessions(0),
		  processed(0),
		  enabled(ENABLED)
	{
		if (_libev != NULL) {
			adminSocketWatcher = make_shared<PipeWatcher>(_libev, _adminSocket,
				_forwardStderr ? STDOUT_FILENO : -1);
			adminSocketWatcher->start();
		}
		if (_libev != NULL && _errorPipe != -1) {
			errorPipeWatcher = make_shared<PipeWatcher>(_libev, _errorPipe,
				_forwardStderr ? STDERR_FILENO : -1);
			errorPipeWatcher->start();
		}
		
		if (OXT_LIKELY(sockets != NULL)) {
			indexSessionSockets();
		}
		
		lastUsed     = SystemTime::getUsec();
		spawnEndTime = lastUsed;
	}
	
	~Process() {
		if (OXT_LIKELY(sockets != NULL)) {
			SocketList::const_iterator it, end = sockets->end();
			for (it = sockets->begin(); it != end; it++) {
				if (getSocketAddressType(it->address) == SAT_UNIX) {
					string filename = parseUnixSocketAddress(it->address);
					syscalls::unlink(filename.c_str());
				}
			}
		}
		// The admin socket stays alive for a while thanks to adminSocketWatcher,
		// so we shutdown the writable part to close the child's stdin.
		syscalls::shutdown(adminSocket, SHUT_WR);
	}
	
	// Thread-safe.
	GroupPtr getGroup() const {
		lock_guard<boost::mutex> lock(backrefSyncher);
		return group.lock();
	}
	
	// Thread-safe.
	void setGroup(const GroupPtr &group) {
		lock_guard<boost::mutex> lock(backrefSyncher);
		this->group = group;
	}
	
	// Thread-safe.
	bool detached() const {
		return getGroup() == NULL;
	}
	
	int usage() const {
		/* Different processes within a Group may have different
		 * 'concurrency' values. We want:
		 * - Group.pqueue to sort the processes from least used to most used.
		 * - to give processes with concurrency == 0 more priority over processes
		 *   with concurrency > 0.
		 * Therefore, we describe our usage as a percentage of 'concurrency', with
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
	
	bool atFullCapacity() const {
		return concurrency != 0 && sessions >= concurrency;
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
		if (socket->atFullCapacity()) {
			return SessionPtr();
		} else {
			socket->sessions++;
			this->sessions++;
			processed++;
			socket->pqHandle = sessionSockets.push(socket, socket->usage());
			lastUsed = SystemTime::getUsec();
			return make_shared<Session>(shared_from_this(), socket);
		}
	}
	
	void sessionClosed(Session *session) {
		Socket *socket = session->getSocket();
		
		assert(socket->sessions > 0);
		assert(sessions > 0);
		
		socket->sessions--;
		this->sessions--;
		sessionSockets.decrease(socket->pqHandle, socket->usage());
		assert(!atFullCapacity());
	}

	/**
	 * Returns the uptime of this process so far, as a string.
	 */
	string uptime() const {
		unsigned long long seconds = (SystemTime::getUsec() - spawnEndTime) / 1000000;
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

	string inspect() const;

	template<typename Stream>
	void inspectXml(Stream &stream, bool includeSockets = true) const {
		stream << "<pid>" << pid << "</pid>";
		stream << "<gupid>" << gupid << "</gupid>";
		stream << "<connect_password>" << connectPassword << "</connect_password>";
		stream << "<concurrency>" << concurrency << "</concurrency>";
		stream << "<sessions>" << sessions << "</sessions>";
		stream << "<usage>" << usage() << "</usage>";
		stream << "<processed>" << processed << "</processed>";
		stream << "<spawner_creation_time>" << spawnerCreationTime << "</spawner_creation_time>";
		stream << "<spawn_start_time>" << spawnStartTime << "</spawn_start_time>";
		stream << "<spawn_end_time>" << spawnEndTime << "</spawn_end_time>";
		stream << "<last_used>" << lastUsed << "</last_used>";
		stream << "<uptime>" << uptime() << "</uptime>";
		switch (enabled) {
		case ENABLED:
			stream << "<enabled>enabled</enabled>";
			break;
		case DISABLING:
			stream << "<enabled>disabling</enabled>";
			break;
		case DISABLED:
			stream << "<enabled>disabled</enabled>";
			break;
		default:
			stream << "<enabled>unknown</enabled>";
			break;
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
