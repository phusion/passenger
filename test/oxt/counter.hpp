#ifndef _COUNTER_HPP_
#define _COUNTER_HPP_

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/thread/thread_time.hpp>

struct Counter;
typedef boost::shared_ptr<Counter> CounterPtr;

/**
 * A synchronization mechanism with counter-like properties.
 *
 * To avoid memory corruption when unit tests fail, one should
 * never store Counter objects on the stack. Instead, one should
 * create them on the heap and use CounterPtr smart pointers.
 */
struct Counter {
	struct timeout_expired { };
	
	unsigned int value;
	boost::mutex mutex;
	boost::condition_variable cond;
	
	static CounterPtr create_ptr() {
		return CounterPtr(new Counter());
	}
	
	Counter() {
		value = 0;
	}
	
	/**
	 * Wait until other threads have increment this counter to at least wanted_value.
	 * If this doesn't happen within <tt>timeout</tt> miliseconds, then a timeout_expired
	 * exception will be thrown.
	 */
	void wait_until(unsigned int wanted_value, unsigned int timeout = 1000) {
		boost::unique_lock<boost::mutex> l(mutex);
		while (value < wanted_value) {
			if (!cond.timed_wait(l, boost::get_system_time() + boost::posix_time::milliseconds(timeout))) {
				throw timeout_expired();
			}
		}
	}
	
	/** Increment the counter by one. */
	void increment() {
		boost::unique_lock<boost::mutex> l(mutex);
		value++;
		cond.notify_all();
	}
};

#endif /* _COUNTER_HPP_ */
