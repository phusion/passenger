#include "TestSupport.h"
#include <string>
#include <algorithm>
#include <cstddef>
#include <Utils/HttpHeaderBufferer.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct HttpHeaderBuffererTest {
		HttpHeaderBufferer bufferer;
		string input;
		
		HttpHeaderBuffererTest() {
			input = "HTTP/1.1 200 OK\r\n"
				"Content-Type: text/plain\r\n"
				"Connection: close\r\n"
				"\r\n";
		}
	};
	
	DEFINE_TEST_GROUP(HttpHeaderBuffererTest);
	
	TEST_METHOD(1) {
		set_test_name("Test initial state");
		ensure(bufferer.acceptingInput());
		ensure(!bufferer.hasError());
	}
	
	TEST_METHOD(2) {
		set_test_name("Test feeding a complete stream everything in one go");
		ensure_equals(bufferer.feed(input.data(), input.size()), input.size());
		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		ensure_equals("It does not copy any data",
			bufferer.getData().data(), input.data());
		ensure_equals(bufferer.getData().size(), input.size());
	}
	
	TEST_METHOD(3) {
		set_test_name("Test feeding a complete stream byte by byte");
		const char *pos = input.data();
		const char *end = input.data() + input.size();
		while (pos < end) {
			ensure(bufferer.acceptingInput());
			ensure(!bufferer.hasError());
			ensure_equals(bufferer.feed(pos, 1), 1u);
			pos++;
		}
		
		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		ensure("It copies the fed data into an internal buffer",
			bufferer.getData().data() != input.data());
		ensure_equals(bufferer.getData(), input);
	}
	
	TEST_METHOD(4) {
		set_test_name("Test feeding a complete stream in pieces of 2 bytes");
		const char *pos = input.data();
		const char *end = input.data() + input.size();
		while (pos < end) {
			ensure("(1)", bufferer.acceptingInput());
			ensure("(2)", !bufferer.hasError());
			size_t size = std::min<size_t>(2, end - pos);
			ensure_equals(bufferer.feed(pos, size), size);
			pos += 2;
		}
		
		ensure("(3)", !bufferer.acceptingInput());
		ensure("(4)", !bufferer.hasError());
		ensure("It copies the fed data into an internal buffer",
			bufferer.getData().data() != input.data());
		ensure_equals(bufferer.getData(), input);
	}
	
	TEST_METHOD(5) {
		set_test_name("Test feeding a complete stream in pieces of 3 bytes");
		const char *pos = input.data();
		const char *end = input.data() + input.size();
		while (pos < end) {
			ensure("(1)", bufferer.acceptingInput());
			ensure("(2)", !bufferer.hasError());
			size_t size = std::min<size_t>(3, end - pos);
			ensure_equals(bufferer.feed(pos, size), size);
			pos += 3;
		}
		
		ensure("(3)", !bufferer.acceptingInput());
		ensure("(4)", !bufferer.hasError());
		ensure("It copies the fed data into an internal buffer",
			bufferer.getData().data() != input.data());
		ensure_equals(bufferer.getData(), input);
	}
	
	TEST_METHOD(20) {
		set_test_name("It refuses to accept any more data after the header terminator until reset is called");
		string input2 = input;
		input2.append("hello world");
		
		ensure_equals(bufferer.feed(input2.data(), input2.size()), input.size());
		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		ensure_equals(bufferer.getData().data(), input2.data());
		ensure_equals(bufferer.getData(), input);
		
		ensure_equals(bufferer.feed(input.data(), input.size()), 0u);
		
		bufferer.reset();
		ensure_equals(bufferer.feed(input2.data(), input2.size()), input.size());
		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		ensure_equals(bufferer.getData().data(), input2.data());
		ensure_equals(bufferer.getData(), input);
	}
	
	TEST_METHOD(21) {
		set_test_name("Same test as above, except we feed byte-by-byte");
		string input2 = input;
		input2.append("hello world");
		const char *pos;
		const char *end;
		
		pos = input2.data();
		end = input2.data() + input.size();
		while (pos < end) {
			ensure_equals("(1)", bufferer.feed(pos, 1), 1u);
			pos++;
		}
		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		
		end = input2.data() + input2.size();
		while (pos < end) {
			ensure_equals("(2)", bufferer.feed(pos, 1), 0u);
			pos++;
		}
		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		ensure(bufferer.getData().data() != input2.data());
		ensure_equals(bufferer.getData(), input);
		
		bufferer.reset();
		pos = input2.data();
		end = input2.data() + input.size();
		while (pos < end) {
			ensure_equals("(3)", bufferer.feed(pos, 1), 1u);
			pos++;
		}
		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		
		end = input2.data() + input2.size();
		while (pos < end) {
			ensure_equals("(4)", bufferer.feed(pos, 1), 0u);
			pos++;
		}
		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		ensure(bufferer.getData().data() != input2.data());
		ensure_equals(bufferer.getData(), input);
	}
	
	TEST_METHOD(22) {
		set_test_name("Test inputting data larger than the max size");
		input.assign(1024, '\0');
		bufferer.setMax(512);
		
		ensure_equals(bufferer.feed(input.data(), input.size()), 512u);
		ensure(!bufferer.acceptingInput());
		ensure(bufferer.hasError());
	}
	
	TEST_METHOD(23) {
		set_test_name("Some as above, except we feed byte-by-byte");
		unsigned int i;
		bufferer.setMax(512);
		
		for (i = 0; i < 512; i++) {
			ensure_equals(bufferer.feed("\0", 1), 1u);
		}
		ensure(!bufferer.acceptingInput());
		ensure(bufferer.hasError());
		
		for (i = 0; i < 512; i++) {
			ensure_equals(bufferer.feed("\0", 1), 0u);
		}
		ensure(!bufferer.acceptingInput());
		ensure(bufferer.hasError());
	}
	
	TEST_METHOD(24) {
		set_test_name("Test garbage");
		input.clear();
		for (int i = 0; i < 256; i++) {
			input.append(1, (char) i);
		}
		bufferer.feed(input.data(), input.size());
		ensure(bufferer.acceptingInput());
		ensure(!bufferer.hasError());
	}

	TEST_METHOD(25) {
		set_test_name("It ignores 100-Continue messages that are fed in one go");
		string preamble = "HTTP/1.1 100 Continue\r\n\r\n";
		string input2 = preamble + input;

		ensure_equals(bufferer.feed(input2.data(), input2.size()), input2.size());
		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		ensure_equals("It ignored the 100-Continue message",
			bufferer.getData(), input);
		ensure_equals("It does not copy any data",
			bufferer.getData().data(),
			input2.data() + preamble.size());
		ensure_equals(bufferer.getData().size(), input.size());
	}

	TEST_METHOD(26) {
		set_test_name("It ignores 100-Continue messages that are fed byte-by-byte");
		string preamble = "HTTP/1.1 100 Continue\r\n\r\n";
		string input2 = preamble + input;
		const char *pos = input2.data();
		const char *end = input2.data() + input2.size();
		while (pos < end) {
			ensure(bufferer.acceptingInput());
			ensure(!bufferer.hasError());
			ensure_equals(bufferer.feed(pos, 1), 1u);
			pos++;
		}
		
		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		ensure("It copies the fed data into an internal buffer",
			bufferer.getData().data() < input2.data()
			|| bufferer.getData().data() > input2.data() + input2.size());
		ensure_equals(bufferer.getData(), input);
	}

	TEST_METHOD(27) {
		set_test_name("It ignores 100-Continue messages, when the non-100 "
			"message is fed separately from the 100 message");
		string preamble = "HTTP/1.1 100 Continue\r\n\r\n";

		ensure_equals(bufferer.feed(preamble.data(), preamble.size()), preamble.size());
		ensure_equals(bufferer.feed(input.data(), input.size()), input.size());

		ensure(!bufferer.acceptingInput());
		ensure(!bufferer.hasError());
		ensure_equals("It ignored the 100-Continue message",
			bufferer.getData(), input);
		ensure_equals("It does not copy any data",
			bufferer.getData().data(),
			input.data());
	}
}
