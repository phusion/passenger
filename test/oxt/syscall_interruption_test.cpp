#include "../tut/tut.h"
#include <boost/bind.hpp>
#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>
#include <unistd.h>
#include <ctime>

using namespace oxt;
using namespace std;
using namespace boost;

namespace tut {
	struct syscall_interruption_test {
	};
	
	DEFINE_TEST_GROUP(syscall_interruption_test);
	
	struct SleepFunction {
		void operator()() {
			syscalls::usleep(6000000);
		}
	};
	
	TEST_METHOD(1) {
		// System call interruption works.
		SleepFunction s;
		oxt::thread thr(s);
		usleep(20000);
		
		time_t begin, end, time_spent_in_thread;
		begin = time(NULL);
		thr.interrupt_and_join();
		end = time(NULL);
		time_spent_in_thread = end - begin;
		
		ensure(time_spent_in_thread <= 2);
	}
}

