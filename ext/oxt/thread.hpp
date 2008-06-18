#include <boost/thread.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

/**
 * Thread class with system call interruption support.
 */
class thread: public thread {
public:
	template <class F>
	explicit Thread(F f, unsigned int stackSize = 0)
		: thread(f, stackSize) {}
	
	/**
	 * Interrupt the thread. This method behaves just like
	 * boost::thread::interrupt(), but will also respect the interruption
	 * points defined in Passenger::InterruptableCalls.
	 *
	 * Note that an interruption request may get lost, depending on the
	 * current execution point of the thread. Thus, one should call this
	 * method in a loop, until a certain goal condition has been fulfilled.
	 * interruptAndJoin() is a convenience method that implements this
	 * pattern.
	 */
	void interrupt() {
		int ret;
		
		thread::interrupt();
		do {
			ret = pthread_kill(native_handle(),
				INTERRUPTION_SIGNAL);
		} while (ret == EINTR);
	}
	
	/**
	 * Keep interrupting the thread until it's done, then join it.
	 *
	 * @throws boost::thread_interrupted
	 */
	void interruptAndJoin() {
		bool done = false;
		while (!done) {
			interrupt();
			done = timed_join(posix_time::millisec(10));
		}
	}
};
