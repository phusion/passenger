#ifndef _PASSENGER_APPLICATION_H_
#define _PASSENGER_APPLICATION_H_

#include <boost/shared_ptr.hpp>
#include <string>

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "MessageChannel.h"
#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace boost;

// TODO: write better documentation
class Application {
private:
	string appRoot;
	pid_t pid;
	int listenSocket;
public:
	Application(const string &theAppRoot, pid_t pid, int listenSocket) {
		appRoot = theAppRoot;
		this->pid = pid;
		this->listenSocket = listenSocket;
		P_TRACE("Application " << this << ": created.");
	}
	
	~Application() {
		close(listenSocket);
		P_TRACE("Application " << this << ": destroyed.");
	}
	
	string getAppRoot() const {
		return appRoot;
	}
	
	pid_t getPid() const {
		return pid;
	}
	
	pair<int, int> connect() const {
		int ret;
		do {
			ret = write(listenSocket, "", 1);
		} while ((ret == -1 && errno == EINTR) || ret == 0);
		if (ret == -1) {
			throw SystemException("Cannot request a connection from the request handler", errno);
		}
		
		MessageChannel channel(listenSocket);
		int reader = channel.readFileDescriptor();
		int writer = channel.readFileDescriptor();
		return make_pair(reader, writer);
	}
	
	int getListenSocket() const {
		return listenSocket;
	}
};

typedef shared_ptr<Application> ApplicationPtr;

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_H_ */
