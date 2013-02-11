#include "TestSupport.h"
#include "agents/LoggingAgent/FilterSupport.h"

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
		
		bool eval(const StaticString &source, bool debug = false) {
			return Filter(source, debug).run(ctx);
		}
		
		bool validate(const StaticString &source) {
			try {
				Filter f(source);
				return true;
			} catch (const SyntaxError &) {
				return false;
			}
		}
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
	
	TEST_METHOD(2) {
		// Test support for various fields.
		ctx.uri = "foo";
		ctx.controller = "bar";
		ctx.responseTime = 800;
		ctx.status = "200 OK";
		ctx.statusCode = 201;
		ctx.gcTime = 30;
		ensure(eval(
			"uri == 'foo' "
			" && response_time == 800"
			" && response_time_without_gc == 770"
			" && status == '200 OK'"
			" && status_code == 201"
			" && gc_time == 30"
		));
	}
	
	
	/******** String and regexp tests *******/
	
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
		// String syntax supports \\, \n, \r, \t
		ctx.uri = "hello\r\n\tworld\\";
		ensure(Filter("uri == \"hello\\r\\n\\tworld\\\\\"").run(ctx));
	}
	
	TEST_METHOD(14) {
		// Strings can also start and end with single quote characters.
		ctx.uri = "hello world";
		ensure(Filter("uri == 'hello world'").run(ctx));
	}
	
	TEST_METHOD(15) {
		// String begin and end quote characters must match.
		try {
			(void) Filter("uri == 'hello world\"");
			fail("Syntax error expected");
		} catch (const SyntaxError &) {
			// Pass.
		}
		try {
			(void) Filter("uri == \"hello world'");
			fail("Syntax error expected");
		} catch (const SyntaxError &) {
			// Pass.
		}
	}
	
	TEST_METHOD(16) {
		// Regular expressions can also start with %r{ and end with }.
		ctx.uri = "hello world";
		ensure(Filter("uri =~ %r{hello}").run(ctx));
		try {
			(void) Filter("uri =~ /hello}");
			fail("Syntax error expected");
		} catch (const SyntaxError &) {
			// Pass.
		}
		try {
			(void) Filter("uri =~ %r{hello/");
			fail("Syntax error expected");
		} catch (const SyntaxError &) {
			// Pass.
		}
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
	
	
	/******** Boolean and expression combination tests *******/
	
	TEST_METHOD(30) {
		ensure("(1)", Filter("true").run(ctx));
		ensure("(2)", !Filter("false").run(ctx));
		ensure("(3)", Filter("true && 1 == 1").run(ctx));
		ensure("(4)", Filter("true || 1 == 0").run(ctx));
		ensure("(5)", !Filter("false && 1 == 1").run(ctx));
		ensure("(6)", !Filter("false || 1 == 0").run(ctx));
		ensure("(7)", Filter("false || 1 == 1").run(ctx));
	}
	
	TEST_METHOD(31) {
		ensure(Filter("true == true").run(ctx));
		ensure(!Filter("true == false").run(ctx));
		ensure(Filter("true != false").run(ctx));
		ensure(!Filter("true != true").run(ctx));
		
		ensure(Filter("false == false").run(ctx));
		ensure(!Filter("false == true").run(ctx));
		ensure(Filter("false != true").run(ctx));
		ensure(!Filter("false != false").run(ctx));
	}
	
	TEST_METHOD(32) {
		ensure("(1)", eval("true && true && true"));
		ensure("(2)", !eval("true && true && false"));
		ensure("(3)", !eval("true && false && false"));
		ensure("(4)", !eval("false && false && false"));
		ensure("(5)", !eval("false && true && false"));
		ensure("(6)", !eval("false && false && true"));
		ensure("(7)", !eval("false && true && false"));
		
		ensure("(8)", eval("true || true || true"));
		ensure("(9)", eval("true || true || false"));
		ensure("(10)", eval("true || false || false"));
		ensure("(11)", !eval("false || false || false"));
		ensure("(12)", eval("false || true || false"));
		ensure("(13)", eval("false || false || true"));
		ensure("(14)", eval("false || true || false"));
		
		ensure("(15)", eval("false || true && true"));
		ensure("(16)", !eval("true || false && false"));
		ensure("(17)", eval("true || (false && false)"));
		
		ctx.uri = "foo";
		ctx.responseTime = 10;
		ensure("(20)", eval("uri == 'foo' && (response_time == 1 || response_time == 10)"));
		ensure("(21)", eval("(uri == 'foo' && response_time == 1) || response_time == 10"));
	}
	
	
	/******** Error tests *******/
	
	TEST_METHOD(40) {
		// < does not work if left operand is a string
		ensure(!validate("'' < 1"));
		// < does not work if right operand is a string
		ensure(!validate("1 < ''"));
		
		// <= does not work if left operand is a string
		ensure(!validate("'' <= 1"));
		// <= does not work if right operand is a string
		ensure(!validate("1 <= ''"));
		
		// > does not work if left operand is a string
		ensure(!validate("'' > 1"));
		// > does not work if right operand is a string
		ensure(!validate("1 > ''"));
		
		// >= does not work if left operand is a string
		ensure(!validate("'' >= 1"));
		// >= does not work if right operand is a string
		ensure(!validate("1 >= ''"));
		
		// =~ does not work if left operand is not a string
		ensure(!validate("1 =~ //"));
		ensure(!validate("// =~ //"));
		ensure(!validate("false =~ //"));
		// =~ does not work if right operand is not a regexp
		ensure(!validate("'' =~ ''"));
		ensure(!validate("'' =~ 1"));
		ensure(!validate("'' =~ false"));
	}
	
	TEST_METHOD(41) {
		// Source must evaluate to a boolean.
		ensure(!validate("1"));
		ensure(!validate("'hello'"));
		ensure(!validate("/abc/"));
	}
	
	
	/******** ContextFromLog tests *******/
	
	TEST_METHOD(50) {
		// It extracts information from the logs
		ContextFromLog ctx(
			"1234-abcd 1234 0 BEGIN: request processing (1235, 10, 10)\n"
			"1234-abcd 1240 1 URI: /foo\n"
			"1234-abcd 1241 2 Controller action: HomeController#index\n"
			"1234-abcd 1242 3 Status: 200 OK\n"
			"1234-abcd 1243 4 Initial GC time: 1\n"
			"1234-abcd 1244 5 Final GC time: 10\n"
			"1234-abcd 2234 10 END: request processing (2234, 10, 10)\n"
		);
		ensure_equals(ctx.getURI(), "/foo");
		ensure_equals(ctx.getController(), "HomeController");
		ensure_equals(ctx.getResponseTime(), 46655);
		ensure_equals(ctx.getStatus(), "200 OK");
		ensure_equals(ctx.getStatusCode(), 200);
		ensure_equals(ctx.getGcTime(), 9);
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
		// If the begin or end "request processing" event is not available
		// then it derives the response time from the entire transaction.
		ContextFromLog ctx(
			"1234-abcd 1234 0 ATTACH\n"
			"1234-abcd 1235 1 BEGIN: request processing (1235, 10, 10)\n"
			"1234-abcd 1236 2 DETACH\n"
		);
		ensure_equals(ctx.getResponseTime(), 2);
	}
}
