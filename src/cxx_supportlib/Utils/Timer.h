/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#ifndef _PASSENGER_TIMER_H_
#define _PASSENGER_TIMER_H_

#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>
#include <SystemTools/SystemTime.h>

namespace Passenger {

using namespace boost;
using namespace oxt;

/**
 * A Timer which one can use to check how much time has elapsed since the
 * timer started. This timer supports miliseconds-resolution, but the exact
 * resolution depends on the OS and the hardware.
 *
 * This class is thread-safe.
 *
 * @code
 * Timer timer;
 * sleep(10);
 * timer.elapsed();   // => about 10000 (msec)
 * @endcode
 */
template<SystemTime::Granularity granularity = SystemTime::GRAN_1USEC>
class Timer {
private:
	MonotonicTimeUsec startTime;
	mutable boost::mutex lock;

public:
	/**
	 * Creates a new Timer object.
	 *
	 * @param startNow Whether the timer should be started immediately.
	 */
	Timer(bool startNow = true) {
		if (startNow) {
			start();
		} else {
			stop();
		}
	}

	/**
	 * Start the timer. If the timer was already started, then this will
	 * restart the timer.
	 */
	void start() {
		boost::lock_guard<boost::mutex> l(lock);
		startTime = SystemTime::getMonotonicUsecWithGranularity<granularity>();
	}

	/**
	 * Stop the timer. If there's currently another thread waiting on the wait()
	 * call, then that wait() call will block indefinitely until you call start()
	 * and sufficient amount of time has elapsed.
	 */
	void stop() {
		boost::lock_guard<boost::mutex> l(lock);
		startTime = 0;
	}

	/**
	 * Resets the timer. If the timer was already started then it is still started;
	 * if it was stopped then it is still stopped.
	 */
	void reset() {
		boost::lock_guard<boost::mutex> l(lock);
		if (startTime != 0) {
			startTime = SystemTime::getMonotonicUsecWithGranularity<granularity>();
		}
	}

	/**
	 * Returns the amount of time that has elapsed since the timer was last started,
	 * in miliseconds. If the timer is currently stopped, then 0 is returned.
	 */
	unsigned long long elapsed() const {
		boost::lock_guard<boost::mutex> l(lock);
		if (startTime == 0) {
			return 0;
		} else {
			return (SystemTime::getMonotonicUsecWithGranularity<granularity>() - startTime) / 1000;
		}
	}

	/**
	 * Returns the amount of time that has elapsed since the timer was last started,
	 * in microseconds. If the timer is currently stopped, then 0 is returned.
	 */
	MonotonicTimeUsec usecElapsed() const {
		boost::lock_guard<boost::mutex> l(lock);
		if (startTime == 0) {
			return 0;
		} else {
			return SystemTime::getMonotonicUsecWithGranularity<granularity>() - startTime;
		}
	}

	/**
	 * Wait until <em>time</em> miliseconds have elapsed since the timer
	 * was last started.
	 */
	void wait(unsigned long long time) const {
		while (elapsed() < time) {
			syscalls::usleep(25000);
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_TIMER_H_ */
