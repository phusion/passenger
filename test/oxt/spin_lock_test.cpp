#include "../tut/tut.h"
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <oxt/spin_lock.hpp>

using namespace boost;
using namespace oxt;

namespace tut {
	struct spin_lock_test {
		boost::mutex continue_mutex;
		boost::condition_variable continue_cond;
		bool continue_ok;
		spin_lock lock;
		volatile unsigned int counter;
		
		spin_lock_test() {
			counter = 0;
			continue_ok = false;
		}
		
		void loop_increment(unsigned int inc) {
			{
				boost::mutex::scoped_lock l1(continue_mutex);
				while (!continue_ok) {
					continue_cond.wait(l1);
				}
			}
			
			for (unsigned int i = 0; i < inc; i++) {
				spin_lock::scoped_lock l2(lock);
				counter++;
			}
		}
	};
	
	DEFINE_TEST_GROUP(spin_lock_test);

	TEST_METHOD(1) {
		boost::thread thr1(boost::bind(&spin_lock_test::loop_increment, this, 100000));
		boost::thread thr2(boost::bind(&spin_lock_test::loop_increment, this, 100000));
		boost::thread thr3(boost::bind(&spin_lock_test::loop_increment, this, 100000));
		boost::thread thr4(boost::bind(&spin_lock_test::loop_increment, this, 100000));
		
		{
			boost::mutex::scoped_lock l(continue_mutex);
			continue_ok = true;
			continue_cond.notify_all();
		}
		
		thr1.join();
		thr2.join();
		thr3.join();
		thr4.join();
		
		ensure_equals(counter, 400000u);
	}
}

