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
#include <PriorityQueue.h>
#include <FileDescriptor.h>
#include <Logging.h>
#include <Utils/SystemTime.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


class ProcessList: public list<ProcessPtr> {
public:
	iterator last_iterator() {
		iterator last = end();
		last--;
		return last;
	}
};

/**
 * Except for the otherwise documented parts, this class is not thread-safe,
 * so only use within the ApplicationPool lock.
 */
class Process: public enable_shared_from_this<Process> {
private:
	friend class Group;
	
	mutable boost::mutex backrefSyncher;
	weak_ptr<Group> group;
	
	PriorityQueue<Socket> sessionSockets;
	
	ProcessList::iterator it;
	PriorityQueue<Process>::Handle pqHandle;
	
	void indexSessionSockets() {
		SocketList::iterator it;
		for (it = sockets->begin(); it != sockets->end(); it++) {
			Socket *socket = &(*it);
			if (socket->protocol == "session") {
				socket->pqHandle = sessionSockets.push(socket, socket->usage());
			}
		}
	}
	
public:
	/* Read-only fields, set once during initialization and never written to again.
	 * Reading is thread-safe.
	 */
	pid_t pid;
	string gupid;
	FileDescriptor adminSocket;
	SocketListPtr sockets;
	unsigned long long spawnStartTime;
	
	/* Information used by Pool. Do not write to these from outside the Pool.
	 * If you read these make sure the Pool isn't concurrently modifying.
	 */
	unsigned long long spawnTime;
	unsigned long long lastUsed;
	int concurrency;
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
		concurrency = 0;
		sessions    = 0;
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
		return (int) (((long long) sessions * INT_MAX) / (double) concurrency);
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
