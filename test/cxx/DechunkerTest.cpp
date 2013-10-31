#include "TestSupport.h"
#include <algorithm>
#include <string>
#include <Utils/Dechunker.h>
#include <Utils/StrIntUtils.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct DechunkerTest {
		Dechunker dechunker;
		string input;
		vector<string> chunks;
		bool ended;
		
		DechunkerTest() {
			dechunker.onData = onData;
			dechunker.onEnd = onEnd;
			dechunker.userData = this;
			ended = false;
		}
		
		void addChunk(const string &data) {
			input.append(integerToHex(data.size()));
			input.append("\r\n");
			input.append(data);
			input.append("\r\n");
		}
		
		static void onData(const char *data, size_t len, void *userData) {
			DechunkerTest *self = (DechunkerTest *) userData;
			self->chunks.push_back(string(data, len));
		}

		static void onEnd(void *userData) {
			DechunkerTest *self = (DechunkerTest *) userData;
			self->ended = true;
		}
	};
	
	DEFINE_TEST_GROUP(DechunkerTest);
	
	TEST_METHOD(1) {
		// Test initial state.
		ensure(dechunker.acceptingInput());
		ensure(!dechunker.hasError());
		ensure(!ended);
		ensure_equals(dechunker.getErrorMessage(), (const char *) NULL);
	}
	
	TEST_METHOD(2) {
		// Test feeding a complete stream everything in one go.
		addChunk("hello");
		addChunk("world");
		addChunk("");
		ensure_equals(dechunker.feed(input.data(), input.size()), input.size());
		ensure(!dechunker.acceptingInput());
		ensure(!dechunker.hasError());
		ensure_equals(chunks.size(), 2u);
		ensure_equals(chunks[0], "hello");
		ensure_equals(chunks[1], "world");
		ensure(ended);
	}
	
	TEST_METHOD(3) {
		// Test feeding a complete stream byte by byte.
		addChunk("hel");
		addChunk("lo");
		addChunk("");
		
		const char *pos = input.data();
		const char *end = input.data() + input.size();
		while (pos < end) {
			ensure(dechunker.acceptingInput());
			ensure(!dechunker.hasError());
			ensure_equals(dechunker.feed(pos, 1), 1u);
			pos++;
		}
		
		ensure(!dechunker.acceptingInput());
		ensure(!dechunker.hasError());
		ensure_equals(chunks.size(), 5u);
		ensure_equals(chunks[0], "h");
		ensure_equals(chunks[1], "e");
		ensure_equals(chunks[2], "l");
		ensure_equals(chunks[3], "l");
		ensure_equals(chunks[4], "o");
		ensure(ended);
	}
	
	TEST_METHOD(4) {
		// Test feeding a complete stream in pieces of 2 bytes.
		addChunk("hello");
		addChunk("world");
		addChunk("");
		
		const char *pos = input.data();
		const char *end = input.data() + input.size();
		while (pos < end) {
			ensure(dechunker.acceptingInput());
			ensure(!dechunker.hasError());
			size_t size = std::min<size_t>(2, end - pos);
			ensure_equals(dechunker.feed(pos, size), size);
			pos += 2;
		}
		
		ensure(!dechunker.acceptingInput());
		ensure(!dechunker.hasError());
		ensure_equals(chunks.size(), 6u);
		ensure_equals(chunks[0], "h");
		ensure_equals(chunks[1], "el");
		ensure_equals(chunks[2], "lo");
		ensure_equals(chunks[3], "w");
		ensure_equals(chunks[4], "or");
		ensure_equals(chunks[5], "ld");
		ensure(ended);
	}
	
	TEST_METHOD(5) {
		// Test feeding a complete stream in pieces of 3 bytes.
		addChunk("hello");
		addChunk("world");
		addChunk("");
		
		const char *pos = input.data();
		const char *end = input.data() + input.size();
		while (pos < end) {
			ensure(dechunker.acceptingInput());
			ensure(!dechunker.hasError());
			size_t size = std::min<size_t>(3, end - pos);
			ensure_equals(dechunker.feed(pos, size), size);
			pos += 3;
		}
		
		ensure(!dechunker.acceptingInput());
		ensure(!dechunker.hasError());
		ensure_equals(chunks.size(), 4u);
		ensure_equals(chunks[0], "hel");
		ensure_equals(chunks[1], "lo");
		ensure_equals(chunks[2], "wo");
		ensure_equals(chunks[3], "rld");
		ensure(ended);
	}
	
	TEST_METHOD(6) {
		// Test support for chunk extensions.
		input = "2;foobar\r\n"
			"xy\r\n"
			"0\r\n"
			"\r\n";
		ensure_equals(dechunker.feed(input.data(), input.size()), input.size());
		ensure(!dechunker.acceptingInput());
		ensure(!dechunker.hasError());
		ensure_equals(chunks.size(), 1u);
		ensure_equals(chunks[0], "xy");
		ensure(ended);
	}
	
	TEST_METHOD(20) {
		// It refuses to accept any more data after EOF until reset is called.
		addChunk("hello");
		addChunk("");
		
		dechunker.feed(input.data(), input.size());
		ensure_equals(dechunker.feed(input.data(), input.size()), 0u);
		dechunker.reset();
		ensure_equals(dechunker.feed(input.data(), input.size()), input.size());
		ensure(!dechunker.acceptingInput());
		ensure(!dechunker.hasError());
		
		ensure_equals(chunks.size(), 2u);
		ensure_equals(chunks[0], "hello");
		ensure_equals(chunks[1], "hello");
		ensure(ended);
	}
	
	TEST_METHOD(21) {
		// Test invalid size string.
		input = "12x\r\n";
		ensure_equals(dechunker.feed(input.data(), input.size()), 2u);
		ensure(!dechunker.acceptingInput());
		ensure(dechunker.hasError());
		ensure(!ended);
	}
	
	TEST_METHOD(22) {
		// Test invalid chunk header terminator.
		input = "12\r\t";
		ensure_equals(dechunker.feed(input.data(), input.size()), 3u);
		ensure(!dechunker.acceptingInput());
		ensure(dechunker.hasError());
		ensure(!ended);
	}
	
	TEST_METHOD(23) {
		// Test invalid chunk header terminator when chunk extensions are involved.
		input = "12;foo\r\t";
		ensure_equals(dechunker.feed(input.data(), input.size()), 7u);
		ensure(!dechunker.acceptingInput());
		ensure(dechunker.hasError());
		ensure(!ended);
	}
	
	TEST_METHOD(24) {
		// Test invalid chunk terminator.
		input = "2\r\n"
			"xyz";
		ensure_equals(dechunker.feed(input.data(), input.size()), 5u);
		ensure(!dechunker.acceptingInput());
		ensure(dechunker.hasError());
		ensure(!ended);
	}
	
	TEST_METHOD(25) {
		// Test invalid terminating chunk terminator.
		input = "2\r\n"
			"xy\r\n"
			"0\r\n"
			"\rx";
		ensure_equals(dechunker.feed(input.data(), input.size()), 11u);
		ensure(!dechunker.acceptingInput());
		ensure(dechunker.hasError());
		ensure(!ended);
	}
	
	TEST_METHOD(26) {
		// Test garbage.
		for (int i = 0; i < 256; i++) {
			input.append(1, (char) i);
		}
		dechunker.feed(input.data(), input.size());
		ensure(!dechunker.acceptingInput());
		ensure(dechunker.hasError());
		ensure(!ended);
	}

	TEST_METHOD(27) {
		// Test feeding a partial stream.
		addChunk("hello");
		addChunk("world");
		ensure_equals(dechunker.feed(input.data(), input.size()), input.size());
		ensure(dechunker.acceptingInput());
		ensure(!dechunker.hasError());
		ensure_equals(chunks.size(), 2u);
		ensure_equals(chunks[0], "hello");
		ensure_equals(chunks[1], "world");
		ensure(!ended);
	}
}
