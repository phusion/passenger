#ifndef _PASSENGER_APPLICATION_H_
#define _PASSENGER_APPLICATION_H_

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <string>

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include "MessageChannel.h"
#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace boost;

// TODO: write better documentation
class Application {
public:
	class Session;
	typedef function<void (Session &session)> CloseCallback;
	typedef shared_ptr<Session> SessionPtr;
	
	class Session {
	public:
		virtual ~Session() {}
		
		virtual void sendHeaders(const string &headers) {
			int writer = getWriter();
			ssize_t ret;
			unsigned int written = 0;
			const char *data = const_cast<const string &>(headers).c_str();
			do {
				do {
					ret = write(writer, data + written, headers.size() - written);
				} while (ret == -1 && errno == EINTR);
				if (ret == -1) {
					throw SystemException("An error occured while writing headers to the request handler", errno);
				} else {
					written += ret;
				}
			} while (written < headers.size());
			closeWriter();
		}
		
		virtual void sendBody(const string &body) { /* TODO */ }
		
		virtual int getReader() = 0;
		virtual void closeReader() = 0;
		virtual int getWriter() = 0;
		virtual void closeWriter() = 0;
	};

private:
	struct SharedData {
		unsigned int sessions;
	};
	
	typedef shared_ptr<SharedData> SharedDataPtr;

	class StandardSession: public Session {
	protected:
		SharedDataPtr data;
		CloseCallback closeCallback;
		int reader;
		int writer;
		
	public:
		StandardSession(SharedDataPtr data, const CloseCallback &closeCallback, int reader, int writer) {
			this->data = data;
			this->closeCallback = closeCallback;
			data->sessions++;
			this->reader = reader;
			this->writer = writer;
		}
	
		virtual ~StandardSession() {
			data->sessions--;
			closeReader();
			closeWriter();
			closeCallback(*this);
		}
		
		virtual int getReader() {
			return reader;
		}
		
		virtual void closeReader() {
			if (reader != -1) {
				close(reader);
				reader = -1;
			}
		}
		
		virtual int getWriter() {
			return writer;
		}
		
		virtual void closeWriter() {
			if (writer != -1) {
				close(writer);
				writer = -1;
			}
		}
	};

	string appRoot;
	pid_t pid;
	int listenSocket;
	time_t lastUsed;
	SharedDataPtr data;

public:
	Application(const string &theAppRoot, pid_t pid, int listenSocket) {
		appRoot = theAppRoot;
		this->pid = pid;
		this->listenSocket = listenSocket;
		lastUsed = time(NULL);
		this->data = ptr(new SharedData());
		this->data->sessions = 0;
		P_TRACE("Application " << this << ": created.");
	}
	
	virtual ~Application() {
		close(listenSocket);
		P_TRACE("Application " << this << ": destroyed.");
	}
	
	string getAppRoot() const {
		return appRoot;
	}
	
	pid_t getPid() const {
		return pid;
	}
	
	SessionPtr connect(const CloseCallback &closeCallback) const {
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
		return ptr(new StandardSession(data, closeCallback, reader, writer));
	}
	
	int getListenSocket() const {
		return listenSocket;
	}
	
	unsigned int getSessions() const {
		return data->sessions;
	}
	
	time_t getLastUsed() const {
		return lastUsed;
	}
	
	void setLastUsed(time_t time) {
		lastUsed = time;
	}
};

typedef shared_ptr<Application> ApplicationPtr;

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_H_ */
