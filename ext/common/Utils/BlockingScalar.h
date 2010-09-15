#ifndef _PASSENGER_BLOCKING_SCALAR_H_
#define _PASSENGER_BLOCKING_SCALAR_H_

#include <boost/thread.hpp>
#include <queue>

namespace Passenger {

using namespace std;
using namespace boost;

template<typename T>
class BlockingScalar {
private:
	boost::mutex lock;
	condition_variable added, removed;
	T item;
	bool empty;

public:
	BlockingScalar() {
		empty = true;
	}
	
	void set(const T &item) {
		unique_lock<boost::mutex> l(lock);
		while (!empty) {
			removed.wait(l);
		}
		this->item = item;
		empty = false;
		added.notify_one();
	}
	
	T get() {
		unique_lock<boost::mutex> l(lock);
		while (empty) {
			added.wait(l);
		}
		T item = this->item;
		this->item = T();
		empty = true;
		removed.notify_one();
		return item;
	}
};

} // namespace Passenger

#endif _PASSENGER_BLOCKING_SCALAR_H_
