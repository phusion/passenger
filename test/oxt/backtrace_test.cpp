#include "../tut/tut.h"
#include "counter.hpp"
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
	
	static void foo(CounterPtr parent_counter, CounterPtr child_counter) {
		TRACE_POINT();
		child_counter->increment();    // Tell parent that we've created the trace point.
		parent_counter->wait_until(1); // Wait until parent thread says we can exit.
	}
	
	static void bar(CounterPtr parent_counter, CounterPtr child_counter) {
		TRACE_POINT();
		child_counter->increment();    // Tell parent that we've created the trace point.
		parent_counter->wait_until(1); // Wait until parent thread says we can exit.
	}
	
	TEST_METHOD(2) {
		// Test whether oxt::thread's backtrace support works.
		CounterPtr parent_counter = Counter::create_ptr();
		CounterPtr child_counter  = Counter::create_ptr();
		oxt::thread foo_thread(boost::bind(foo, parent_counter, child_counter));
		oxt::thread bar_thread(boost::bind(bar, parent_counter, child_counter));
		
		// Wait until all threads have created trace points.
		child_counter->wait_until(2);
		
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
		
		parent_counter->increment(); // Tell threads to quit.
		foo_thread.join();
		bar_thread.join();
	}
}

