#ifndef _PASSENGER_APPLICATION_POOL_SESSION_H_
#define _PASSENGER_APPLICATION_POOL_SESSION_H_

#include <sys/types.h>
#include <boost/shared_ptr.hpp>
#include <oxt/macros.hpp>
#include <oxt/system_calls.hpp>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/Socket.h>
#include <FileDescriptor.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace oxt;


/**
 * Not thread-safe, but Pool's and Process's API encourage that
 * a Session is only used by 1 thread and then thrown away.
 */
class Session {
public:
	typedef void (*Callback)(Session *session);

private:
	/** For keeping the OS process alive until all of a Process's sessions are closed. */
	ProcessPtr process;
	/** Socket to use for this session. Guaranteed to be alive thanks to the 'process' reference. */
	Socket *socket;
	
	Connection connection;
	bool closed;
	
	void reallyClose(bool success) {
		connection.fail = !success;
		closed = true;
		socket->checkinConnection(connection);
		if (OXT_LIKELY(onClose != NULL)) {
			onClose(this);
		}
	}

public:
	Callback onClose;
	
	Session(const ProcessPtr &process, Socket *socket)
	{
		this->process   = process;
		this->socket    = socket;
		closed          = false;
		onClose         = NULL;
	}
	
	~Session() {
		// If user doesn't close() explicitly, we penalize performance.
		if (OXT_UNLIKELY(initiated() && !closed)) {
			reallyClose(false);
		}
	}
	
	pid_t getPid() const;
	
	ProcessPtr getProcess() const {
		return process;
	}
	
	Socket *getSocket() const {
		return socket;
	}
	
	void initiate() {
		assert(!closed);
		connection = socket->checkoutConnection();
		connection.fail = true;
	}
	
	bool initiated() {
		return connection.fd != -1;
	}
	
	int fd() const {
		if (OXT_LIKELY(!closed)) {
			return connection.fd;
		} else {
			return -1;
		}
	}
	
	void close(bool success) {
		if (OXT_LIKELY(initiated() && !closed)) {
			reallyClose(success);
		}
	}
};

typedef shared_ptr<Session> SessionPtr;


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SESSION_H_ */
