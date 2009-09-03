/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
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

#include <sys/time.h>

namespace Passenger {

/**
 * A Timer which one can use to check how much time has elapsed since the timer started.
 * This timer support microseconds-resolution, but the exact resolution depends on the OS
 * and the hardware.
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
			startTime.tv_sec = 0;
			startTime.tv_usec = 0;
		}
	}
	
	/**
	 * Start the timer. If the timer was already started, then this will
	 * restart the timer.
	 */
	void start() {
		// TODO: We really use should clock_gettime() and the monotonic
		// clock whenever possible, instead of gettimeofday()...
		gettimeofday(&startTime, NULL);
	}
	
	/**
	 * Checks how much time has elapsed since the timer was last started.
	 *
	 * @pre The timer must have been started.
	 * @return The elapsed time, in miliseconds.
	 */
	unsigned long long elapsed() const {
		struct timeval t;
		unsigned long long now, beginning;
		
		gettimeofday(&t, NULL);
		now = (unsigned long long) t.tv_sec * 1000 + t.tv_usec / 1000;
		beginning = (unsigned long long) startTime.tv_sec * 1000 + startTime.tv_usec / 1000;
		return now - beginning;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_TIMER_H_ */
