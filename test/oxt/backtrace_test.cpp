#include "tut.h"
#include <oxt/backtrace.hpp>
#include <oxt/tracable_exception.hpp>

using namespace oxt;
using namespace std;

namespace tut {
	struct backtrace_test {
	};
	
	DEFINE_TEST_GROUP(backtrace_test);

	TEST_METHOD(1) {
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
}

