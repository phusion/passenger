#include "TestSupport.h"
#include "HttpStatusExtractor.h"

using namespace Passenger;
using namespace std;

namespace tut {
	struct HttpStatusExtractorTest {
		HttpStatusExtractor ex;
	};
	
	DEFINE_TEST_GROUP(HttpStatusExtractorTest);
	
	/* TODO:
	 * "\r\n" in this test file should really be replaced with "\x0D\x0A".
	 * So far I haven't countered a platform on which "\r\n" is not equal
	 * to "\x0D\x0A" but the possibility that they're not equal exists.
	 */
	
	TEST_METHOD(1) {
		// Status defaults to "200 OK" and buffer is initially empty.
		ensure_equals(ex.getStatusLine(), "200 OK\r\n");
		ensure_equals(ex.getBuffer(), "");
	}
	
	TEST_METHOD(2) {
		// Test feeding an entire HTTP response (header + body)
		// in 1 pass. The first header is the status line.
		const char data[] =
			"Status: 201 OK\r\n"
			"Content-Type: text/html\r\n"
			"\r\n"
			"hello world!";
		ensure("Parsing completed.", ex.feed(data, sizeof(data) - 1));
		ensure_equals("Status was properly extracted.",
			ex.getStatusLine(), "201 OK\r\n");
		ensure_equals("All data that we've fed so far has been buffered.",
			ex.getBuffer(), data);
	}
	
	TEST_METHOD(3) {
		// Test feeding a single byte initially, and the
		// rest of the status line later.
		ensure("Parsing is not complete.", !ex.feed("S", 1));
		ensure_equals("Status line hasn't changed.",
			ex.getStatusLine(), "200 OK\r\n");
		ensure_equals("All data that we've fed so far has been buffered.",
			ex.getBuffer(), "S");
		
		const char data2[] = "tatus: 300 Abc\r\n";
		ensure("Parsing not yet complete.", !ex.feed(data2, sizeof(data2) - 1));
		
		// Parsing completes when full header has been fed.
		ensure("Parsing is complete.", ex.feed("\r\n", 2));
		ensure_equals("Status line recognized.",
			ex.getStatusLine(), "300 Abc\r\n");
		ensure_equals("All data that we've fed so far has been buffered.",
			ex.getBuffer(), "Status: 300 Abc\r\n\r\n");
	}
	
	TEST_METHOD(4) {
		// Test feeding an incomplete non-status line, which
		// is completed later. The status line is feeded later.
		const char data[] = "Content-Type: text/html";
		ensure(!ex.feed(data, sizeof(data) - 1));
		ensure_equals(ex.getStatusLine(), "200 OK\r\n");
		ensure_equals(ex.getBuffer(), data);
		
		const char data2[] = "\r\nStatus: 201 Hello\r\n\r\n";
		ensure(ex.feed(data2, sizeof(data2) - 1));
		ensure_equals(ex.getStatusLine(), "201 Hello\r\n");
		ensure_equals(ex.getBuffer(),
			"Content-Type: text/html\r\n"
			"Status: 201 Hello\r\n"
			"\r\n");
	}
	
	TEST_METHOD(5) {
		// Test feeding multiple complete lines, none of which
		// is the status line. The status line is feeded later.
		const char data[] =
			"Content-Type: text/html\r\n"
			"Foo: bar\r\n";
		ensure(!ex.feed(data, sizeof(data) - 1));
		ensure_equals(ex.getStatusLine(), "200 OK\r\n");
		ensure_equals(ex.getBuffer(), data);
		
		const char data2[] = "Status: 404 Not Found\r\n";
		ensure(!ex.feed(data2, sizeof(data2) - 1));
		
		// Parsing completes when full header has been fed.
		ensure(ex.feed("\r\n", 2));
		ensure_equals(ex.getStatusLine(), "404 Not Found\r\n");
		ensure_equals(ex.getBuffer(), string(data) + data2 + "\r\n");
	}
	
	TEST_METHOD(6) {
		// Test feeding multiple complete lines and a single incomplete line,
		// none of which is the status line. The header is completed
		// later, but without status line.
		const char data[] =
			"Content-Type: text/html\r\n"
			"Hello: world";
		ensure(!ex.feed(data, sizeof(data) - 1));
		ensure_equals(ex.getStatusLine(), "200 OK\r\n");
		ensure_equals(ex.getBuffer(), data);
		
		const char data2[] = "\r\n\r\nbody data";
		ensure(ex.feed(data2, sizeof(data2) - 1));
		ensure_equals(ex.getStatusLine(), "200 OK\r\n");
		ensure_equals(ex.getBuffer(), string(data) + data2);
	}
	
	TEST_METHOD(7) {
		// Test feeding an incomplete status line which is larger
		// than 3 bytes, which is completed later.
		const char data[] = "Status: 500 Internal Se";
		ensure(!ex.feed(data, sizeof(data) - 1));
		ensure_equals(ex.getStatusLine(), "200 OK\r\n");
		ensure_equals(ex.getBuffer(), data);
		
		const char data2[] = "rver Error\r\n\r\n";
		ensure(ex.feed(data2, sizeof(data2) - 1));
		ensure_equals(ex.getStatusLine(), "500 Internal Server Error\r\n");
		ensure_equals(ex.getBuffer(), string(data) + data2);
	}
	
	TEST_METHOD(8) {
		// Test feeding an entire HTTP response (header + body)
		// in 1 pass. There is a status line, but it is NOT the first
		// header.
		const char data[] =
			"Content-Type: text/html\r\n"
			"Status: 405 Testing\r\n"
			"Hello: world\r\n"
			"\r\n"
			"bla bla";
		ensure(ex.feed(data, sizeof(data) - 1));
		ensure_equals(ex.getStatusLine(), "405 Testing\r\n");
		ensure_equals(ex.getBuffer(), data);
	}
	
	TEST_METHOD(9) {
		// Test feeding multiple complete lines and a single incomplete
		// line. One of the complete lines is the status line, but it
		// is not the first line.
		// The response is completed later.
		const char data[] =
			"Content-Type: text/html\r\n"
			"Status: 100 Foo\r\n"
			"B";
		ensure(!ex.feed(data, sizeof(data) - 1));
		ensure_equals(ex.getStatusLine(), "200 OK\r\n");
		ensure_equals(ex.getBuffer(), data);
		
		const char data2[] = "la: bla\r\n\r\n";
		ensure(ex.feed(data2, sizeof(data2) - 1));
		ensure_equals(ex.getStatusLine(), "100 Foo\r\n");
		ensure_equals(ex.getBuffer(), string(data) + data2);
	}
	
	TEST_METHOD(10) {
		// Test feeding multiple complete lines and a single
		// incomplete status line. The response is completed
		// later
		const char data[] =
			"Content-Type: text/html\r\n"
			"Statu";
		ensure(!ex.feed(data, sizeof(data) - 1));
		ensure_equals(ex.getStatusLine(), "200 OK\r\n");
		ensure_equals(ex.getBuffer(), data);
		
		const char data2[] =
			"s: 202 Blabla\r\n"
			"Frobnicate: true\r\n"
			"\r\n";
		ensure(ex.feed(data2, sizeof(data2) - 1));
		ensure_equals(ex.getStatusLine(), "202 Blabla\r\n");
		ensure_equals(ex.getBuffer(), string(data) + data2);
	}
	
	TEST_METHOD(11) {
		// If the status in the HTTP data doesn't contain a status text,
		// then the status text is added.
		const char data[] = "Status: 200\r\n\r\n";
		ensure(ex.feed(data, sizeof(data) - 1));
		ensure_equals(ex.getStatusLine(), "200 OK\r\n");
	}
	
	TEST_METHOD(12) {
		// If the status in the HTTP data doesn't contain a status text,
		// and the status code is not recognized, then the status text
		// "Unknown Status Code" is added.
		const char data[] = "Status: 999\r\n\r\n";
		ensure(ex.feed(data, sizeof(data) - 1));
		ensure_equals(ex.getStatusLine(), "999 Unknown Status Code\r\n");
	}
}
