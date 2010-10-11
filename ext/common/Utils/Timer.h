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
#ifndef _PASSENGER_TIMER_H_
#define _PASSENGER_TIMER_H_

#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>
#include <sys/time.h>
#include <cerrno>

namespace Passenger {

using namespace boost;
using namespace oxt;

/**
 * A Timer which one can use to check how much time has elapsed since the
 * timer started. This timer support miliseconds-resolution, but the exact
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
class Timer {
private:
	struct timeval startTime;
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
		// TODO: We really use should clock_gettime() and the monotonic
		// clock whenever possible, instead of gettimeofday()...
		// On OS X we can use mach_absolute_time()
		lock_guard<boost::mutex> l(lock);
		int ret;
		do {
			ret = gettimeofday(&startTime, NULL);
		} while (ret == -1 && errno == EINTR);
	}
	
	/**
	 * Stop the timer. If there's currently another thread waiting on the wait()
	 * call, then that wait() call will block indefinitely until you call start()
	 * and sufficient amount of time has elapsed.
	 */
	void stop() {
		lock_guard<boost::mutex> l(lock);
		startTime.tv_sec = 0;
		startTime.tv_usec = 0;
	}
	
	/**
	 * Returns the amount of time that has elapsed since the timer was last started,
	 * in miliseconds. If the timer is currently stopped, then 0 is returned.
	 */
	unsigned long long elapsed() const {
		lock_guard<boost::mutex> l(lock);
		if (startTime.tv_sec == 0 && startTime.tv_usec == 0) {
			return 0;
		} else {
			struct timeval t;
			unsigned long long now, beginning;
			int ret;
			
			do {
				ret = gettimeofday(&t, NULL);
			} while (ret == -1 && errno == EINTR);
			now = (unsigned long long) t.tv_sec * 1000 + t.tv_usec / 1000;
			beginning = (unsigned long long) startTime.tv_sec * 1000 + startTime.tv_usec / 1000;
			return now - beginning;
		}
	}
	
	/**
	 * Returns the amount of time that has elapsed since the timer was last started,
	 * in microseconds. If the timer is currently stopped, then 0 is returned.
	 */
	unsigned long long usecElapsed() const {
		lock_guard<boost::mutex> l(lock);
		if (startTime.tv_sec == 0 && startTime.tv_usec == 0) {
			return 0;
		} else {
			struct timeval t;
			unsigned long long now, beginning;
			int ret;
			
			do {
				ret = gettimeofday(&t, NULL);
			} while (ret == -1 && errno == EINTR);
			now = (unsigned long long) t.tv_sec * 1000000 + t.tv_usec;
			beginning = (unsigned long long) startTime.tv_sec * 1000000 + startTime.tv_usec;
			return now - beginning;
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
