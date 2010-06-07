/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL_INTERFACE_H_
#define _PASSENGER_APPLICATION_POOL_INTERFACE_H_

#include <boost/shared_ptr.hpp>
#include <sys/types.h>

#include "../Session.h"
#include "../PoolOptions.h"

namespace Passenger {
namespace ApplicationPool {

using namespace std;
using namespace boost;

/**
 * A persistent pool of Applications.
 *
 * Spawning application instances, especially Ruby on Rails ones, is a very expensive operation.
 * Despite best efforts to make the operation less expensive (see SpawnManager),
 * it remains expensive compared to the cost of processing an HTTP request/response.
 * So, in order to solve this, some sort of caching/pooling mechanism will be required.
 * ApplicationPool provides this.
 *
 * Normally, one would use SpawnManager to spawn a new RoR/Rack application instance,
 * then use Application::connect() to create a new session with that application
 * instance, and then use the returned Session object to send the request and
 * to read the HTTP response. ApplicationPool replaces the first step with
 * a call to Application::get(). For example:
 * @code
 *   ApplicationPool pool = some_function_which_creates_an_application_pool();
 *   
 *   // Connect to the application and get the newly opened session.
 *   Application::SessionPtr session(pool->get("/home/webapps/foo"));
 *   
 *   // Send the request headers and request body data.
 *   session->sendHeaders(...);
 *   session->sendBodyBlock(...);
 *   // Done sending data, so we shutdown the writer stream.
 *   session->shutdownWriter();
 *
 *   // Now read the HTTP response.
 *   string responseData = readAllDataFromSocket(session->getStream());
 *   // Done reading data, so we shutdown the reader stream.
 *   session->shutdownReader();
 *
 *   // This session has now finished, so we close the session by resetting
 *   // the smart pointer to NULL (thereby destroying the Session object).
 *   session.reset();
 *
 *   // We can connect to an Application multiple times. Just make sure
 *   // the previous session is closed.
 *   session = app->connect("/home/webapps/bar");
 * @endcode
 *
 * Internally, ApplicationPool::get() will keep spawned applications instances in
 * memory, and reuse them if possible. It will try to keep spawning to a minimum.
 * Furthermore, if an application instance hasn't been used for a while, it
 * will be automatically shutdown in order to save memory. Restart requests are
 * honored: if an application has the file 'restart.txt' in its 'tmp' folder,
 * then get() will shutdown existing instances of that application and spawn
 * a new instance (this is useful when a new version of an application has been
 * deployed). And finally, one can set a hard limit on the maximum number of
 * applications instances that may be spawned (see ApplicationPool::setMax()).
 *
 * Note that ApplicationPool is just an interface (i.e. a pure virtual class).
 * For concrete classes, see StandardApplicationPool and ApplicationPoolServer.
 * The exact pooling algorithm depends on the implementation class.
 *
 * ApplicationPool is *not* guaranteed to be thread-safe. See the documentation
 * for concrete implementations to find out whether that particular implementation
 * is thread-safe.
 *
 * @ingroup Support
 */
class Interface {
public:
	virtual ~Interface() {};
	
	/**
	 * Checks whether this ApplicationPool object is still connected to the
	 * ApplicationPool server.
	 *
	 * If that's not the case, then one should reconnect to the ApplicationPool server.
	 *
	 * This method is only meaningful for instances of type ApplicationPoolServer::Client.
	 * The default implementation always returns true.
	 */
	virtual bool connected() const {
		return true;
	}
	
	/**
	 * Open a new session with the application specified by <tt>PoolOptions.appRoot</tt>.
	 * See the class description for ApplicationPool, as well as Application::connect(),
	 * on how to use the returned session object.
	 *
	 * Internally, this method may either spawn a new application instance, or use
	 * an existing one.
	 *
	 * @param options An object containing information on which application to open
	 *             a session with, as well as spawning details. Spawning details will be used
	 *             if the pool decides that spawning a new application instance is necessary.
	 *             See SpawnManager and PoolOptions for details.
	 * @return A session object.
	 * @throws SpawnException An attempt was made to spawn a new application instance, but that attempt failed.
	 * @throws BusyException The application pool is too busy right now, and cannot
	 *       satisfy the request. One should either abort, or try again later.
	 * @throws SystemException Something else went wrong.
	 * @throws IOException Something else went wrong.
	 * @throws boost::thread_interrupted
	 * @throws Anything thrown by options.environmentVariables->getItems().
	 * @note Applications are uniquely identified with the application root
	 *       string. So although <tt>appRoot</tt> does not have to be absolute, it
	 *       should be. If one calls <tt>get("/home/foo")</tt> and
	 *       <tt>get("/home/../home/foo")</tt>, then ApplicationPool will think
	 *       they're 2 different applications, and thus will spawn 2 application instances.
	 */
	virtual SessionPtr get(const PoolOptions &options) = 0;
	
	/**
	 * Convenience shortcut for calling get() with default spawn options.
	 *
	 * @throws SpawnException
	 * @throws IOException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 * @throws Anything thrown by options.environmentVariables->getItems().
	 */
	virtual SessionPtr get(const string &appRoot) {
		return get(PoolOptions(appRoot));
	}
	
	/**
	 * Detach the process with the given identifier from this pool.
	 *
	 * The identifier can be obtained from a session through the
	 * getPoolIdentifier() method.
	 *
	 * @param identifier The identifier.
	 * @returns Whether there was a process in the pool with the given identifier.
	 * @throws IOException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	virtual bool detach(const string &identifier) = 0;
	
	/**
	 * Clear all application instances that are currently in the pool.
	 *
	 * This method is used by unit tests to verify that the implementation is correct,
	 * and thus should not be called directly.
	 *
	 * @throws IOException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	virtual void clear() = 0;
	
	/**
	 * Set the maximum idle time for application instances. Application instances
	 * that haven't received any requests in <tt>seconds</tt> seconds will be shut
	 * down.
	 *
	 * A value of 0 means that the application instances will not idle timeout.
	 *
	 * @throws IOException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	virtual void setMaxIdleTime(unsigned int seconds) = 0;
	
	/**
	 * Set a hard limit on the number of application instances that this ApplicationPool
	 * may spawn. The exact behavior depends on the used algorithm, and is not specified by
	 * these API docs.
	 *
	 * It is allowed to set a limit lower than the current number of spawned applications.
	 *
	 * @throws IOException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	virtual void setMax(unsigned int max) = 0;
	
	/**
	 * Get the number of active applications in the pool.
	 *
	 * This method exposes an implementation detail of the underlying pooling algorithm.
	 * It is used by unit tests to verify that the implementation is correct,
	 * and thus should not be called directly.
	 *
	 * @throws IOException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	virtual unsigned int getActive() const = 0;
	
	/**
	 * Get the number of active applications in the pool.
	 *
	 * This method exposes an implementation detail of the underlying pooling algorithm.
	 * It is used by unit tests to verify that the implementation is correct,
	 * and thus should not be called directly.
	 *
	 * @throws IOException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	virtual unsigned int getCount() const = 0;
	
	/**
	 * Returns the number of clients waiting on the global queue.
	 *
	 * This method exposes an implementation detail of the underlying pooling algorithm.
	 * It is used by unit tests to verify that the implementation is correct,
	 * and thus should not be called directly.
	 *
	 * @throws IOException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	virtual unsigned int getGlobalQueueSize() const = 0;
	
	/**
	 * Set a hard limit on the number of application instances that a single application
	 * may spawn in this ApplicationPool. The exact behavior depends on the used algorithm, 
	 * and is not specified by these API docs.
	 *
	 * It is allowed to set a limit lower than the current number of spawned applications.
	 *
	 * @throws IOException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	virtual void setMaxPerApp(unsigned int max) = 0;
	
	/**
	 * Get the process ID of the spawn server that is used.
	 *
	 * This method exposes an implementation detail. It is used by unit tests to verify
	 * that the implementation is correct, and thus should not be used directly.
	 *
	 * @throws IOException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	virtual pid_t getSpawnServerPid() const = 0;
	
	/**
	 * Returns a human-readable description of the internal state
	 * of the application pool.
	 */
	virtual string inspect() const = 0;
	
	/**
	 * Returns an XML description of the internal state of the
	 * application pool.
	 *
	 * @param includeSensitiveInformation Whether potentially sensitive
	 *     information may be included in the result.
	 */
	virtual string toXml(bool includeSensitiveInformation = true) const = 0;
};

typedef shared_ptr<Interface> Ptr;

} // namespace ApplicationPool
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_INTERFACE_H_ */
