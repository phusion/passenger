#ifndef _PASSENGER_APPLICATION_H_
#define _PASSENGER_APPLICATION_H_

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <string>

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <ctime>

#include "MessageChannel.h"
#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace boost;

/**
 * Represents a single Ruby on Rails application instance.
 *
 * @ingroup Support
 */
class Application {
public:
	class Session;
	/** A type for callback functions that are called when a session is closed.
	 * @see Application::connect()
	 */
	typedef function<void (Session &session)> CloseCallback;
	/** Convenient alias for Session smart pointer. */
	typedef shared_ptr<Session> SessionPtr;
	
	/**
	 * Represents the life time of a single request/response pair.
	 *
	 * A Session object can be used to send request data and to receive response data.
	 * A usage example is shown in Application::connect().
	 *
	 * A session is said to be closed when the Session object is destroyed.
	 */
	class Session {
	public:
		// TODO: Finalize API. sendBody() doesn't do anything yet.
		// Also document when exactly the writer socket is closed.
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
	/**
	 * A structure containing data that both StandardSession and Application
	 * may access. Since Application and StandardSession may have different
	 * life times (i.e. one can be destroyed before the other), they both
	 * have a smart pointer referencing a SharedData structure. Only
	 * when both the StandardSession and the Application object have been
	 * destroyed, will the SharedData object be destroyed as well.
	 */
	struct SharedData {
		unsigned int sessions;
	};
	
	typedef shared_ptr<SharedData> SharedDataPtr;

	/**
	 * A "standard" implementation of Session.
	 */
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
	/**
	 * Construct a new Application object.
	 *
	 * @param theAppRoot The application root of a RoR application, i.e. the folder that
	 *             contains 'app/', 'public/', 'config/', etc. This must be a valid directory,
	 *             but the path does not have to be absolute.
	 * @param pid The process ID of this application instance.
	 * @param listenSocket The listener socket of this application instance.
	 * @post getAppRoot() == theAppRoot && getPid() == pid
	 */
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
	
	/**
	 * Returns the application root for this RoR application. See the constructor
	 * for information about the application root.
	 */
	string getAppRoot() const {
		return appRoot;
	}
	
	/**
	 * Returns the process ID of this application instance.
	 */
	pid_t getPid() const {
		return pid;
	}
	
	/**
	 * Connect to this application instance with the purpose of sending
	 * a request to the application. Once connected, a new session will
	 * be opened. This session represents the life time of a single
	 * request/response pair, and can be used to send the request
	 * data to the application instance, as well as receiving the response
	 * data.
	 *
	 * The use of connect() is demonstrated in the following example.
	 * @code
	 *   // Connect to the application and get the newly opened session.
	 *   Application::SessionPtr session(app->connect("/home/webapps/foo"));
	 *   
	 *   // Send the request headers and request body data.
	 *   session->sendHeaders(...);
	 *   session->sendBody(...);
	 *
	 *   // Now read the HTTP response.
	 *   string responseData = readAllDataFromSocket(session->getReader());
	 *
	 *   // This session has now finished, so we close the session by resetting
	 *   // the smart pointer to NULL (thereby destroying the Session object).
	 *   session.reset();
	 *
	 *   // We can connect to an Application multiple times. Just make sure
	 *   // the previous session is closed.
	 *   session = app->connect("/home/webapps/bar")
	 * @endcode
	 *
	 * Note that a RoR application instance can only process one
	 * request at the same time, and thus only one session at the same time.
	 * You <b>must</b> close a session when you no longer need if. You you
	 * call connect() without having properly closed a previous session,
	 * you might cause a deadlock because the application instance may be
	 * waiting for you to close the previous session.
	 *
	 * @return A smart pointer to a Session object, which represents the created session.
	 * @param closeCallback A function which will be called when the session has been closed.
	 * @post this->getSessions() == old->getSessions() + 1
	 * @throws SystemException Something went wrong during the connection process.
	 * @throws IOException Something went wrong during the connection process.
	 */
	SessionPtr connect(const CloseCallback &closeCallback) const {
		int ret;
		do {
			ret = write(listenSocket, "", 1);
		} while ((ret == -1 && errno == EINTR) || ret == 0);
		if (ret == -1) {
			throw SystemException("Cannot request a new session from the request handler", errno);
		}
		
		try {
			MessageChannel channel(listenSocket);
			int reader = channel.readFileDescriptor();
			int writer = channel.readFileDescriptor();
			return ptr(new StandardSession(data, closeCallback, reader, writer));
		} catch (const SystemException &e) {
			throw SystemException("Cannot receive one of the session file descriptors from the request handler", e.code());
		} catch (const IOException &e) {
			string message("Cannot receive one of the session file descriptors from the request handler");
			message.append(e.what());
			throw IOException(message);
		}
	}
	
	/**
	 * Get the number of currently opened sessions.
	 */
	unsigned int getSessions() const {
		return data->sessions;
	}
	
	/**
	 * Returns the last value set by setLastUsed(). This represents the time
	 * at which this application object was last used.
	 *
	 * This is used by StandardApplicationPool's cleaner thread to determine which
	 * Application objects have been idle for too long and need to be cleaned
	 * up. Thus, outside StandardApplicationPool, one should never have to call this
	 * method directly.
	 */
	time_t getLastUsed() const {
		return lastUsed;
	}
	
	/**
	 * Set the time at which this Application object was last used. See getLastUsed()
	 * for information.
	 *
	 * @param time The time.
	 * @post getLastUsed() == time
	 */
	void setLastUsed(time_t time) {
		lastUsed = time;
	}
};

/** Convenient alias for Application smart pointer. */
typedef shared_ptr<Application> ApplicationPtr;

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_H_ */
