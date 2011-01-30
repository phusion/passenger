#include "TestSupport.h"
#include "LoggingAgent/Filter.h"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <oxt/thread.hpp>
#include <set>

using namespace Passenger;
using namespace Passenger::FilterSupport;
using namespace std;
using namespace oxt;

namespace tut {
	struct FilterTest {
		SimpleContext ctx;
	};
	
	DEFINE_TEST_GROUP(FilterTest);
	
	/******** Generic tests *******/
	
	TEST_METHOD(1) {
		// Filter source cannot be blank.
		
		try {
			Filter f("");
			fail("SyntaxError expected for empty filter source");
		} catch (const SyntaxError &) {
			// Success.
		}
		
		try {
			Filter f("    ");
			fail("SyntaxError expected for blank filter source");
		} catch (const SyntaxError &) {
			// Success.
		}
	}
	
	
	/******** String tests *******/
	
	TEST_METHOD(5) {
		// Test string comparison
		Filter f("uri == \"hello world\"");
		ctx.uri = "hello world";
		ensure("(1)", f.run(ctx));
		ctx.uri = "something else";
		ensure("(2)", !f.run(ctx));
	}
	
	TEST_METHOD(6) {
		// Test string negative comparison
		Filter f("uri != \"hello world\"");
		ctx.uri = "hello world";
		ensure("(1)", !f.run(ctx));
		ctx.uri = "something else";
		ensure("(2)", f.run(ctx));
	}
	
	TEST_METHOD(7) {
		// Test string regexp matching
		Filter f("uri =~ /hello world/");
		ctx.uri = "hello world";
		ensure("(1)", f.run(ctx));
		ctx.uri = "hello";
		ensure("(2)", !f.run(ctx));
	}
	
	TEST_METHOD(8) {
		// Test advanced string regexp matching
		Filter f("uri =~ /(hello|world)\nhi/");
		ctx.uri = "hello\nhi";
		ensure("(1)", f.run(ctx));
		ctx.uri = "world\nhi";
		ensure("(2)", f.run(ctx));
		ctx.uri = "hello\n";
		ensure("(3)", !f.run(ctx));
	}
	
	TEST_METHOD(9) {
		// Regexp matching is case-sensitive by default.
		Filter f("uri =~ /Hello World/");
		ctx.uri = "hello world";
		ensure(!f.run(ctx));
	}
	
	TEST_METHOD(10) {
		// Regexp matching can be made case-insensitive.
		Filter f("uri =~ /Hello World/i");
		ctx.uri = "hello world";
		ensure(f.run(ctx));
	}
	
	TEST_METHOD(11) {
		// Left operand may be a literal.
		Filter f("\"hello\" == \"hello\"");
		ensure("(1)", f.run(ctx));
		
		f = Filter("\"hello\" == \"world\"");
		ensure("(2)", !f.run(ctx));
	}
	
	
	/******** Integer tests *******/
	
	TEST_METHOD(20) {
		// Test integer equality comparison
		Filter f("response_time == 10");
		ctx.responseTime = 10;
		ensure("(1)", f.run(ctx));
		ctx.responseTime = 11;
		ensure("(2)", !f.run(ctx));
	}
	
	TEST_METHOD(21) {
		// Test integer inequality comparison
		Filter f("response_time != 10");
		ctx.responseTime = 10;
		ensure("(1)", !f.run(ctx));
		ctx.responseTime = 11;
		ensure("(2)", f.run(ctx));
	}
	
	TEST_METHOD(22) {
		// Test integer larger than comparison
		Filter f("response_time > 10");
		ctx.responseTime = 11;
		ensure("(1)", f.run(ctx));
		ctx.responseTime = 10;
		ensure("(2)", !f.run(ctx));
	}
	
	TEST_METHOD(23) {
		// Test integer larger than or equals comparison
		Filter f("response_time >= 10");
		ctx.responseTime = 10;
		ensure("(1)", f.run(ctx));
		ctx.responseTime = 9;
		ensure("(2)", !f.run(ctx));
	}
	
	TEST_METHOD(24) {
		// Test integer smaller than comparison
		Filter f("response_time < 10");
		ctx.responseTime = 9;
		ensure("(1)", f.run(ctx));
		ctx.responseTime = 10;
		ensure("(2)", !f.run(ctx));
	}
	
	TEST_METHOD(25) {
		// Test integer smaller than or equals comparison
		Filter f("response_time <= 10");
		ctx.responseTime = 10;
		ensure("(1)", f.run(ctx));
		ctx.responseTime = 11;
		ensure("(2)", !f.run(ctx));
	}
	
	
	/******** Error tests *******/
	
	TEST_METHOD(30) {
		// < does not work if left operand is a string
		// < does not work if right operand is a string
		// <= does not work if left operand is a string
		// <= does not work if right operand is a string
		// > does not work if left operand is a string
		// < does not work if right operand is a string
		// >= does not work if left operand is a string
		// >= does not work if right operand is a string
		// =~ does not work if left operand is not a string
		// =~ does not work if right operand is not a regexp
	}
}