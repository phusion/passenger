#ifndef _PASSENGER_APPLICATION_POOL_SOCKET_H_
#define _PASSENGER_APPLICATION_POOL_SOCKET_H_

#include <string>
#include <vector>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <climits>
#include <cassert>
#include <PriorityQueue.h>
#include <Utils/IOUtils.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


class Process;

struct Connection {
	int fd;
	bool persistent: 1;
	bool fail: 1;
	
	Connection() {
		fd = -1;
		persistent = false;
		fail = false;
	}
	
	void close() {
		if (fd != -1) {
			int fd2 = fd;
			fd = -1;
			persistent = false;
			safelyClose(fd2);
		}
	}
};

/**
 * Not thread-safe except for the connection pooling methods, so only use
 * within the ApplicationPool lock.
 */
class Socket {
private:
	boost::mutex connectionPoolLock;
	int totalConnections;
	vector<Connection> idleConnections;
	
	int connectionPoolLimit() const {
		return concurrency;
	}
	
	Connection connect() const {
		Connection connection;
		connection.fd = connectToServer(address);
		return connection;
	}
	
public:
	// Read-only.
	string name;
	string address;
	string protocol;
	int concurrency;
	
	/** The handle inside the associated Process's 'sessionSockets' priority queue.
	 * Guaranteed to be valid as long as the Process is alive.
	 */
	PriorityQueue<Socket>::Handle pqHandle;
	
	/** Invariant: sessions >= 0 */
	int sessions;
	
	Socket()
		: concurrency(0)
		{ }
	
	Socket(const string &_name, const string &_address, const string &_protocol, int _concurrency)
		: name(_name),
		  address(_address),
		  protocol(_protocol),
		  concurrency(_concurrency),
		  sessions(0)
		{ }
	
	Socket(const Socket &other)
		: totalConnections(other.totalConnections),
		  idleConnections(other.idleConnections),
		  name(other.name),
		  address(other.address),
		  protocol(other.protocol),
		  concurrency(other.concurrency),
		  pqHandle(other.pqHandle),
		  sessions(other.sessions)
		{ }
	
	Socket &operator=(const Socket &other) {
		totalConnections = other.totalConnections;
		idleConnections = other.idleConnections;
		name = other.name;
		address = other.address;
		protocol = other.protocol;
		concurrency = other.concurrency;
		pqHandle = other.pqHandle;
		sessions = other.sessions;
		return *this;
	}
	
	/** One MUST call checkinConnection() when one's done using the Connection.
	 * Failure to do so will result in a resource leak.
	 */
	Connection checkoutConnection() {
		lock_guard<boost::mutex> l(connectionPoolLock);
		
		if (idleConnections.empty()) {
			Connection connection = idleConnections.back();
			idleConnections.pop_back();
			return connection;
		} else if (totalConnections < connectionPoolLimit()) {
			Connection connection = connect();
			connection.persistent = true;
			totalConnections++;
			return connection;
		} else {
			return connect();
		}
	}
	
	void checkinConnection(Connection connection) {
		unique_lock<boost::mutex> l(connectionPoolLock);
		
		if (connection.persistent) {
			if (connection.fail) {
				totalConnections--;
				l.unlock();
				connection.close();
			} else {
				idleConnections.push_back(connection);
			}
		} else {
			l.unlock();
			connection.close();
		}
	}
	
	
	bool idle() const {
		return sessions == 0;
	}
	
	int usage() const {
		if (concurrency == 0) {
			return 0;
		} else {
			return (int) (((long long) sessions * INT_MAX) / (double) concurrency);
		}
	}
	
	bool atFullCapacity() const {
		return concurrency != 0 && sessions >= concurrency;
	}
};

class SocketList: public vector<Socket> {
public:
	void add(const string &name, const string &address, const string &protocol, int concurrency) {
		push_back(Socket(name, address, protocol, concurrency));
	}
};

typedef shared_ptr<SocketList> SocketListPtr;


} // namespace ApplicationPool2
} // namespace Pasenger

#endif /* _PASSENGER_APPLICATION_POOL2_SOCKET_H_ */
