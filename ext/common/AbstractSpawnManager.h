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
#ifndef _PASSENGER_ABSTRACT_SPAWN_MANAGER_H_
#define _PASSENGER_ABSTRACT_SPAWN_MANAGER_H_

#include <string>

#include <boost/shared_ptr.hpp>

#include <unistd.h>

#include "Process.h"
#include "PoolOptions.h"

namespace Passenger {

using namespace std;
using namespace boost;

/**
 * @brief Spawning of application processes.
 *
 * An AbstractSpawnManager is responsible for spawning new application processes.
 * Use the spawn() method to do so. AbstractSpawnManager is guaranteed to be thread-safe.
 *
 * AbstractSpawnManager is just an interface. There are two concrete implementations,
 * namely SpawnManager and StubSpawnManager. The former is the one that's usually used,
 * while the latter exists for unit testing purposes.
 *
 * @ingroup Support
 */
class AbstractSpawnManager {
public:
	virtual ~AbstractSpawnManager() { }
	
	/**
	 * Spawn a new application process. Spawning details are to be passed
	 * via the <tt>options</tt> argument.
	 *
	 * If the spawn server died during the spawning process, then the server
	 * will be automatically restarted, and another spawn attempt will be made.
	 * If restarting the server fails, or if the second spawn attempt fails,
	 * then an exception will be thrown.
	 *
	 * @param options An object containing the details for this spawn operation,
	 *                such as which application to spawn. See PoolOptions for details.
	 * @return A smart pointer to a Process object, which represents the application
	 *         process that has been spawned. Use this object to communicate with the
	 *         spawned process.
	 * @throws SpawnException Something went wrong.
	 * @throws boost::thread_interrupted
	 * @throws Anything thrown by options.environmentVariables->getItems().
	 */
	virtual ProcessPtr spawn(const PoolOptions &options) = 0;
	
	/**
	 * Shutdown the ApplicationSpawner server that's running at the given
	 * application root. This method should be called when it's time to reload
	 * an application.
	 *
	 * @throws SystemException Unable to communicate with the spawn server,
	 *         even after a restart.
	 * @throws SpawnException The spawn server died unexpectedly, and a
	 *         restart was attempted, but it failed.
	 */
	virtual void reload(const string &appRoot) = 0;
	
	/**
	 * Forcefully kill the spawn server. This AbstractSpawnManager's state will
	 * not be modified, so that it won't know that the spawn server is killed
	 * until next time it sends a command to it.
	 *
	 * Used within unit tests.
	 */
	virtual void killSpawnServer() const = 0;
	
	/**
	 * Returns the spawn server's PID. Used within unit tests.
	 */
	virtual pid_t getServerPid() const = 0;
};

/** Convenient alias for AbstractSpawnManager smart pointer. */
typedef shared_ptr<AbstractSpawnManager> AbstractSpawnManagerPtr;

} // namespace Passenger

#endif /* _PASSENGER_ABSTRACT_SPAWN_MANAGER_H_ */
