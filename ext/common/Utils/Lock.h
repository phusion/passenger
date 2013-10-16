#ifndef _PASSENGER_LOCK_H_
#define _PASSENGER_LOCK_H_

#include <boost/thread.hpp>

namespace Passenger {

using namespace boost;

/** Shortcut typedefs. */
typedef boost::lock_guard<boost::mutex> LockGuard;
typedef boost::unique_lock<boost::mutex> ScopedLock;

/** Nicer syntax for conditionally locking the mutex during construction. */
class DynamicScopedLock: public boost::unique_lock<boost::mutex> {
public:
	DynamicScopedLock(boost::mutex &m, bool lockNow = true)
		: boost::unique_lock<boost::mutex>(m, boost::defer_lock)
	{
		if (lockNow) {
			lock();
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_LOCK_H_ */
