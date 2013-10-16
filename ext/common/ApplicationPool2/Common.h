/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2013 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL2_COMMON_H_
#define _PASSENGER_APPLICATION_POOL2_COMMON_H_

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <oxt/tracable_exception.hpp>
#include <ApplicationPool2/Options.h>
#include <Utils/StringMap.h>

namespace tut {
	struct ApplicationPool2_PoolTest;
}

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;

class Pool;
class SuperGroup;
class Group;
class Process;
class Session;

/**
 * The result of a Pool::disableProcess/Group::disable() call. Some values are only
 * returned by the functions, some values are only passed to the Group::disable()
 * callback, some values appear in all cases.
 */
enum DisableResult {
	// The process has been successfully disabled.
	// Returned by functions and passed to the callback.
	DR_SUCCESS,
	
	// The disabling of the process was canceled before completion.
	// The process still exists.
	// Only passed to the callback.
	DR_CANCELED,
	
	// Nothing happened: the requested process does not exist (anymore)
	// or was already disabled.
	// Returned by functions and passed to the callback.
	DR_NOOP,
	
	// The disabling of the process failed: an error occurred.
	// Returned by functions and passed to the callback.
	DR_ERROR,

	// Indicates that the process cannot be disabled immediately
	// and that the callback will be called later.
	// Only returned by functions.
	DR_DEFERRED
};

typedef boost::shared_ptr<Pool> PoolPtr;
typedef boost::shared_ptr<SuperGroup> SuperGroupPtr;
typedef boost::shared_ptr<Group> GroupPtr;
typedef boost::shared_ptr<Process> ProcessPtr;
typedef boost::shared_ptr<Session> SessionPtr;
typedef boost::shared_ptr<tracable_exception> ExceptionPtr;
typedef StringMap<SuperGroupPtr> SuperGroupMap;
typedef boost::function<void (const SessionPtr &session, const ExceptionPtr &e)> GetCallback;
typedef boost::function<void (const ProcessPtr &process, DisableResult result)> DisableCallback;
typedef boost::function<void ()> Callback;

struct GetWaiter {
	Options options;
	GetCallback callback;
	
	GetWaiter(const Options &o, const GetCallback &cb)
		: options(o),
		  callback(cb)
	{
		options.persist(o);
	}
};

struct Ticket {
	boost::mutex syncher;
	boost::condition_variable cond;
	SessionPtr session;
	ExceptionPtr exception;
};

struct SpawnerConfig {
	// Used by SmartSpawner and DirectSpawner.
	/** Whether to print the preloader's and application's stdout. */
	bool forwardStdout;
	/** Whether to print the preloader's and application's stderr. */
	bool forwardStderr;
	/** A random generator to use. */
	RandomGeneratorPtr randomGenerator;

	// Used by DummySpawner and SpawnerFactory.
	unsigned int concurrency;
	unsigned int spawnerCreationSleepTime;
	unsigned int spawnTime;

	SpawnerConfig(const RandomGeneratorPtr &randomGenerator = RandomGeneratorPtr())
		: forwardStdout(true),
		  forwardStderr(true),
		  concurrency(1),
		  spawnerCreationSleepTime(0),
		  spawnTime(0)
	{
		if (randomGenerator != NULL) {
			this->randomGenerator = randomGenerator;
		} else {
			this->randomGenerator = boost::make_shared<RandomGenerator>();
		}
	}
};

typedef boost::shared_ptr<SpawnerConfig> SpawnerConfigPtr;

ExceptionPtr copyException(const tracable_exception &e);
void rethrowException(const ExceptionPtr &e);

} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_COMMON_H_ */
