#include "TestSupport.h"
#include "LoggingAgent/FilterSupport.h"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <oxt/thread.hpp>
#include <set>

using namespace Passenger;
using namespace Passenger::FilterSupport;
using namespace std;
using namespace oxt;

namespace tut {
	struct FilterSupportTest {
		SimpleContext ctx;
	};
	
	DEFINE_TEST_GROUP_WITH_LIMIT(FilterSupportTest, 100);
	
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
		Filter f("uri =~ /(hello|world)\\nhi/");
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
	
	TEST_METHOD(12) {
		// Right operand may be a field.
		Filter f("\"hello\" == uri");
		ctx.uri = "hello";
		ensure("(1)", f.run(ctx));
		
		f = Filter("\"hello\" == uri");
		ctx.uri = "world";
		ensure("(2)", !f.run(ctx));
	}
	
	TEST_METHOD(13) {
		// String syntax supports \n, \r, \t
		ctx.uri = "hello\r\n\tworld";
		ensure(Filter("uri == \"hello\\r\\n\\tworld\"").run(ctx));
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
	
	TEST_METHOD(26) {
		// Negative integers work
		ctx.responseTime = -23;
		ensure(Filter("response_time == -23").run(ctx));
	}
	
	TEST_METHOD(27) {
		// Left operand may be a literal.
		ensure("(1)", Filter("2 == 2").run(ctx));
		ensure("(2)", !Filter("2 != 2").run(ctx));
		ensure("(3)", Filter("1 < 2").run(ctx));
		ensure("(4)", !Filter("1 < 0").run(ctx));
		ensure("(5)", Filter("1 <= 1").run(ctx));
		ensure("(6)", !Filter("1 <= 0").run(ctx));
		ensure("(7)", Filter("2 > 1").run(ctx));
		ensure("(8)", !Filter("2 > 2").run(ctx));
		ensure("(9)", Filter("2 >= 2").run(ctx));
		ensure("(10)", !Filter("2 >= 3").run(ctx));
	}
	
	TEST_METHOD(28) {
		// Right operand may be a field.
		ctx.responseTime = 2;
		ensure("(1)", Filter("2 == response_time").run(ctx));
		ensure("(2)", !Filter("2 != 2").run(ctx));
		
		ensure("(3)", Filter("1 < response_time").run(ctx));
		ctx.responseTime = 0;
		ensure("(4)", !Filter("1 < response_time").run(ctx));
		
		ctx.responseTime = 1;
		ensure("(5)", Filter("1 <= response_time").run(ctx));
		ctx.responseTime = 0;
		ensure("(6)", !Filter("1 <= response_time").run(ctx));
		
		ctx.responseTime = 1;
		ensure("(7)", Filter("2 > response_time").run(ctx));
		ctx.responseTime = 2;
		ensure("(8)", !Filter("2 > response_time").run(ctx));
		
		ensure("(9)", Filter("2 >= response_time").run(ctx));
		ctx.responseTime = 3;
		ensure("(10)", !Filter("2 >= response_time").run(ctx));
	}
	
	
	/******** Error tests *******/
	
	TEST_METHOD(40) {
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
	
	
	/******** ContextFromLog tests *******/
	
	TEST_METHOD(50) {
		// It extracts information from the logs
		ContextFromLog ctx(
			"1234-abcd 1234 0 BEGIN: request processing (1235, 10, 10)\n"
			"1234-abcd 1240 1 URI: /foo\n"
			"1234-abcd 1241 2 Controller action: HomeController#index\n"
			"1234-abcd 2234 3 END: request processing (2234, 10, 10)\n"
		);
		ensure_equals(ctx.getURI(), "/foo");
		ensure_equals(ctx.getController(), "HomeController");
		ensure_equals(ctx.getResponseTime(), 46655);
	}
	
	TEST_METHOD(51) {
		// It ignores empty lines and invalid lines
		ContextFromLog ctx(
			"\n"
			"\n"
			"    \n"
			"1234-abcd 1234 0 URI: /foo\n"
			"URI: /bar\n"
			"\n"
		);
		ensure_equals(ctx.getURI(), "/foo");
	}
	
	TEST_METHOD(52) {
		// It does only extracts the response time if both the begin and end events are available
		ContextFromLog ctx(
			"1234-abcd 1234 0 BEGIN: request processing (1235, 10, 10)\n"
		);
		ensure_equals(ctx.getResponseTime(), 0);
	}
}