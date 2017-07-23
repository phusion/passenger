/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_BLOCKING_QUEUE_H_
#define _PASSENGER_BLOCKING_QUEUE_H_

#include <boost/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <queue>

namespace Passenger {

using namespace std;
using namespace boost;

template<typename T>
class BlockingQueue {
private:
	mutable boost::timed_mutex lock;
	boost::condition_variable_any added;
	boost::condition_variable_any removed;
	unsigned int max;
	std::queue<T> queue;

	bool atMaxCapacity() const {
		return max > 0 && queue.size() >= max;
	}

public:
	BlockingQueue(unsigned int max = 0) {
		this->max = max;
	}

	unsigned int size() const {
		boost::lock_guard<boost::timed_mutex> l(lock);
		return queue.size();
	}

	void add(const T &item) {
		boost::unique_lock<boost::timed_mutex> l(lock);
		while (atMaxCapacity()) {
			removed.wait(l);
		}
		queue.push(item);
		added.notify_one();
		if (!atMaxCapacity()) {
			removed.notify_one();
		}
	}

	bool tryAdd(const T &item) {
		boost::lock_guard<boost::timed_mutex> l(lock);
		if (!atMaxCapacity()) {
			queue.push(item);
			added.notify_one();
			if (!atMaxCapacity()) {
				removed.notify_one();
			}
			return true;
		} else {
			return false;
		}
	}

	T get() {
		boost::unique_lock<boost::timed_mutex> l(lock);
		while (queue.empty()) {
			added.wait(l);
		}
		T item = queue.front();
		queue.pop();
		removed.notify_one();
		if (!queue.empty()) {
			added.notify_one();
		}
		return item;
	}

	bool timedGet(T &output, unsigned int timeout) {
		boost::unique_lock<boost::timed_mutex> l(lock);
		posix_time::ptime deadline = posix_time::microsec_clock::local_time() +
			posix_time::milliseconds(timeout);
		bool timedOut = false;

		while (queue.empty() && !timedOut) {
			posix_time::time_duration diff = deadline -
				posix_time::microsec_clock::local_time();
			if (diff.is_negative()) {
				timedOut = true;
			} else {
				timedOut = !added.timed_wait(l,
					posix_time::milliseconds(diff.total_milliseconds()));
			}
		}
		if (!queue.empty()) {
			output = queue.front();
			queue.pop();
			removed.notify_one();
			if (!queue.empty()) {
				added.notify_one();
			}
			return true;
		} else {
			return false;
		}
	}

	bool tryGet(T &output) {
		boost::unique_lock<boost::timed_mutex> l(lock);
		if (queue.empty()) {
			return false;
		} else {
			output = queue.front();
			queue.pop();
			removed.notify_one();
			if (!queue.empty()) {
				added.notify_one();
			}
			return true;
		}
	}

	T peek() {
		boost::unique_lock<boost::timed_mutex> l(lock);
		while (queue.empty()) {
			added.wait(l);
		}
		return queue.front();
	}
};

} // namespace Passenger

#endif /* _PASSENGER_BLOCKING_QUEUE_H_ */
