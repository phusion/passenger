#include "tut.h"
#include <oxt/backtrace.hpp>
#include <oxt/tracable_exception.hpp>
#include <oxt/thread.hpp>

using namespace oxt;
using namespace std;

namespace tut {
	struct backtrace_test {
	};
	
	DEFINE_TEST_GROUP(backtrace_test);

	TEST_METHOD(1) {
		// Test TRACE_POINT() and tracable_exception.
		struct {
			void foo() {
				TRACE_POINT();
				bar();
			}
			
			void bar() {
				TRACE_POINT();
				baz();
			}
			
			void baz() {
				TRACE_POINT();
				throw tracable_exception();
			}
		} object;
		
		try {
			object.foo();
			fail("tracable_exception expected.");
		} catch (const tracable_exception &e) {
			ensure("Backtrace contains foo()",
				e.backtrace().find("foo()") != string::npos);
			ensure("Backtrace contains bar()",
				e.backtrace().find("bar()") != string::npos);
			ensure("Backtrace contains baz()",
				e.backtrace().find("baz()") != string::npos);
		}
	}
	
	struct QuitEvent {
		bool is_done;
		boost::mutex mutex;
		boost::condition_variable cond;
		
		QuitEvent() {
			is_done = false;
		}
		
		void wait() {
			boost::unique_lock<boost::mutex> l(mutex);
			while (!is_done) {
				cond.wait(l);
			}
		}
		
		void done() {
			is_done = true;
			cond.notify_all();
		}
	};
	
	struct FooCaller {
		QuitEvent *quit_event;
		
		static void foo(QuitEvent *quit_event) {
			TRACE_POINT();
			quit_event->wait();
		}
		
		void operator()() {
			foo(quit_event);
		}
	};
	
	struct BarCaller {
		QuitEvent *quit_event;
		
		static void bar(QuitEvent *quit_event) {
			TRACE_POINT();
			quit_event->wait();
		}
		
		void operator()() {
			bar(quit_event);
		}
	};
	
	TEST_METHOD(2) {
		// Test whether oxt::thread's backtrace support works.
		FooCaller foo;
		QuitEvent foo_quit;
		foo.quit_event = &foo_quit;
		
		BarCaller bar;
		QuitEvent bar_quit;
		bar.quit_event = &bar_quit;
		
		oxt::thread foo_thread(foo);
		oxt::thread bar_thread(bar);
		usleep(20000);
		
		ensure("Foo thread's backtrace contains foo()",
			foo_thread.backtrace().find("foo") != string::npos);
		ensure("Foo thread's backtrace doesn't contain bar()",
			foo_thread.backtrace().find("bar") == string::npos);
		ensure("Bar thread's backtrace contains bar()",
			bar_thread.backtrace().find("bar") != string::npos);
		ensure("Bar thread's backtrace doesn't contain foo()",
			bar_thread.backtrace().find("foo") == string::npos);
		
		string all_backtraces(oxt::thread::all_backtraces());
		ensure(all_backtraces.find("foo") != string::npos);
		ensure(all_backtraces.find("bar") != string::npos);
		
		foo_quit.done();
		bar_quit.done();
		foo_thread.join();
		bar_thread.join();
	}
}

