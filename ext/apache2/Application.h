#ifndef _PASSENGER_APPLICATION_H_
#define _PASSENGER_APPLICATION_H_

#include <sys/types.h>
#include <boost/shared_ptr.hpp>
#include <string>
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace boost;

/**
 * Represents a single instance of a Ruby on Rails application, as spawned by
 * SpawnManager. An Application can handle one CGI request at a time, through its
 * two communication channels: reader and writer.
 *
 * Application is supposed to be used as follows:
 *  1. The web server first opens the Application, thereby telling the Application
 *     that the web server wants to establish a new CGI session. Application
 *     will return a lock, which the web server must hold onto.
 *  2. The web server sends the CGI request data through the Application's
 *     writer channel.
 *  3. The web server reads the HTTP response from the Application's reader channel,
 *     and forwarding that to the web browser.
 *  4. The web server destroys the previously obtained lock.
 *  5. The web server closes the Application, thereby ending the CGI session.
 *
 * An Application can be reopened after it has been closed.
 *
 * Example:
 * @code
 *   // Create a new Application.
 *   ApplicationPtr app(some_function_which_returns_an_application());
 *   // Open a new CGI session, and save the lock.
 *   Application::LockPtr lock(app->openSession());
 *   
 *   // Process request and response.
 *   send_cgi_headers_to(app->getWriter());
 *   response = read_response_from(app->getReader());
 *   process_response(response);
 *   
 *   // Now we're done. *First* we destroy the lock!
 *   lock = Application::LockPtr();
 *   // And *then* we close the session.
 *   app->closeSession();
 * @endcode
 *
 * <h2>About open/close
 */
class Application {
private:
	struct LockData {
		bool locked;
	};
	
	string appRoot;
	pid_t pid;
	int reader, writer;
	bool opened;
	shared_ptr<LockData> lockData;

public:
	class Lock {
	private:
		friend class Application;
		shared_ptr<LockData> data;
		
		Lock(shared_ptr<LockData> data) {
			this->data = data;
		}
	
	public:
		Lock() {}
		
		~Lock() {
			if (data != NULL) {
				data->locked = false;
			}
			P_TRACE("Unlocked!");
		}
	};
	
	typedef shared_ptr<Lock> LockPtr;

	Application(const string &theAppRoot, pid_t pid, int reader, int writer) {
		appRoot = theAppRoot;
		this->pid = pid;
		this->reader = reader;
		this->writer = writer;
		opened = false;
		lockData = ptr(new LockData());
		lockData->locked = false;
		P_TRACE("Application " << this << ": created.");
	}
	
	virtual ~Application() {
		close(reader);
		close(writer);
		P_TRACE("Application " << this << ": destroyed.");
	}
	
	LockPtr openSession() {
		opened = true;
		lockData->locked = true;
		return ptr(new Lock(lockData));
	}
	
	void closeSession() {
		opened = false;
		P_TRACE("Closed!");
	}
	
	bool isOpen() const {
		return opened;
	}
	
	bool hasError() const {
		return opened && !lockData->locked;
	}
	
	string getAppRoot() const {
		return appRoot;
	}
	
	pid_t getPid() const {
		return pid;
	}
	
	int getReader() const {
		return reader;
	}
	
	int getWriter() const {
		return writer;
	}
};

typedef shared_ptr<Application> ApplicationPtr;

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_H_ */
