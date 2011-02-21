#ifndef _PASSENGER_APPLICATION_POOL_PROCESS_H_
#define _PASSENGER_APPLICATION_POOL_PROCESS_H_

#include <string>
#include <list>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <oxt/macros.hpp>
#include <sys/types.h>
#include <climits>
#include <cassert>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/Socket.h>
#include <ApplicationPool2/Session.h>
#include <FileDescriptor.h>
#include <Logging.h>
#include <Utils/PriorityQueue.h>
#include <Utils/SystemTime.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


class ProcessList: public list<ProcessPtr> {
public:
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
	
	/** Process PID. */
	pid_t pid;
	/** UUID for this process, randomly generated and will never appear again. */
	string gupid;
	/** Admin socket, see class description. */
	FileDescriptor adminSocket;
	/** The sockets that this Process listens on for connections. */
	SocketListPtr sockets;
	/** Time at which we started spawning this process. */
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
	 * process was finished initializing. */
	unsigned long long spawnTime;
	/** Last time when a session was opened for this Process. */
	unsigned long long lastUsed;
	/** Number of sessions currently open. */
	int sessions;
	
	
	Process(pid_t pid, const string &gupid, const FileDescriptor &adminSocket,
		const SocketListPtr &sockets, unsigned long long spawnStartTime)
	{
		this->pid         = pid;
		this->gupid       = gupid;
		this->adminSocket = adminSocket;
		this->sockets     = sockets;
		this->spawnStartTime = spawnStartTime;
		
		indexSessionSockets();
		
		lastUsed    = SystemTime::getUsec();
		spawnTime   = lastUsed;
		sessions    = 0;
	}
	
	~Process() {
		SocketList::const_iterator it, end = sockets->end();
		for (it = sockets->begin(); it != end; it++) {
			if (getSocketAddressType(it->address) == SAT_UNIX) {
				string filename = parseUnixSocketAddress(it->address);
				syscalls::unlink(filename.c_str());
			}
		}
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
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_PROCESS_H_ */
