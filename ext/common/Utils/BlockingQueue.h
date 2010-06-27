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
	timed_mutex lock;
	condition_variable_any added;
	condition_variable_any removed;
	unsigned int max;
	queue<T> queue;
	
	bool atMaxCapacity() const {
		return max > 0 && queue.size() >= max;
	}

public:
	BlockingQueue(unsigned int max = 0) {
		this->max = max;
	}
	
	void add(const T &item) {
		unique_lock<timed_mutex> l(lock);
		while (atMaxCapacity()) {
			removed.wait(l);
		}
		queue.push(item);
		added.notify_one();
		if (!atMaxCapacity()) {
			removed.notify_one();
		}
	}
	
	T get() {
		unique_lock<timed_mutex> l(lock);
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
		unique_lock<timed_mutex> l(lock);
		posix_time::ptime deadline = posix_time::microsec_clock::local_time() +
			posix_time::milliseconds(timeout);
		bool timedOut = false;
		
		while (queue.empty() && !timedOut) {
			posix_time::time_duration diff = deadline -
				posix_time::microsec_clock::local_time();
			if (diff.is_negative() < 0) {
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
		unique_lock<timed_mutex> l(lock);
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
		unique_lock<timed_mutex> l(lock);
		while (queue.empty()) {
			added.wait(l);
		}
		return queue.front();
	}
};

} // namespace Passenger

#endif _PASSENGER_BLOCKING_QUEUE_H_
