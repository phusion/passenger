#include <TestSupport.h>
#include <MessageReadersWriters.h>
#include <cstdlib>

using namespace Passenger;
using namespace std;

namespace tut {
	struct MessageReadersWritersTest {
	};
	
	DEFINE_TEST_GROUP(MessageReadersWritersTest);

	/****** Test Uint16Message ******/

	TEST_METHOD(1) {
		// Test initial state.
		Uint16Message m;
		ensure(!m.done());
		ensure_equals((int) sizeof(uint16_t), (int) 2);
	}
	
	TEST_METHOD(2) {
		// Test feeding 0 bytes.
		Uint16Message m;
		for (int i = 0; i < 100; i++) {
			ensure_equals(m.feed("", 0), (size_t) 0);
			ensure(!m.done());
		}
	}
	
	TEST_METHOD(3) {
		// Test feeding bytes one-by-one until complete.
		Uint16Message m;
		ensure_equals(m.feed("\xFF", 1), (size_t) 1);
		ensure(!m.done());
		ensure_equals(m.feed("\xAB", 1), (size_t) 1);
		ensure(m.done());
		ensure_equals(m.value(), 65451);
	}
	
	TEST_METHOD(4) {
		// Test feeding a complete uint16.
		Uint16Message m;
		ensure_equals(m.feed("\xAB\x0F", 2), (size_t) 2);
		ensure(m.done());
		ensure_equals(m.value(), 43791);
	}
	
	TEST_METHOD(5) {
		// Test feeding a message and garbage in 1 feed command.
		Uint16Message m;
		ensure_equals(m.feed("\xAB\x0Fzzzzz", 7), (size_t) 2);
		ensure(m.done());
		ensure_equals(m.value(), 43791);
	}
	
	TEST_METHOD(6) {
		// Test feeding garbage after having fed a complete uint16.
		Uint16Message m;
		m.feed("\xAB\x0F", 2);
		ensure_equals(m.feed("zzzzz", 5), (size_t) 0);
		ensure(m.done());
		ensure_equals(m.value(), 43791);
	}
	
	TEST_METHOD(7) {
		// Test reset.
		Uint16Message m;
		m.feed("\xAB\x0F", 2);
		m.reset();
		ensure_equals(m.feed("\x0F\xAB", 2), (size_t) 2);
		ensure(m.done());
		ensure_equals(m.value(), 4011);
	}
	
	TEST_METHOD(8) {
		// Test generate.
		char buf[2];
		Uint16Message::generate(buf, 12345);
		ensure(memcmp(buf, "\x30\x39", 2) == 0);
	}
	
	
	/****** Test Uint32Message ******/

	TEST_METHOD(11) {
		// Test initial state.
		Uint32Message m;
		ensure(!m.done());
		ensure_equals((int) sizeof(uint32_t), (int) 4);
	}
	
	TEST_METHOD(12) {
		// Test feeding 0 bytes.
		Uint32Message m;
		for (int i = 0; i < 100; i++) {
			ensure_equals(m.feed("", 0), (size_t) 0);
			ensure(!m.done());
		}
	}
	
	TEST_METHOD(13) {
		// Test feeding bytes one-by-one until complete.
		Uint32Message m;
		ensure_equals(m.feed("\xFF", 1), (size_t) 1);
		ensure(!m.done());
		ensure_equals(m.feed("\xAB", 1), (size_t) 1);
		ensure(!m.done());
		ensure_equals(m.feed("\x99", 1), (size_t) 1);
		ensure(!m.done());
		ensure_equals(m.feed("\xCC", 1), (size_t) 1);
		ensure(m.done());
		ensure_equals(m.value(), 4289436108u);
	}
	
	TEST_METHOD(14) {
		// Test feeding a complete uint32.
		Uint32Message m;
		ensure_equals(m.feed("\xAB\x0F\x99\xCC", 4), (size_t) 4);
		ensure(m.done());
		ensure_equals(m.value(), 2869926348u);
	}
	
	TEST_METHOD(15) {
		// Test feeding a message and garbage in 1 feed command.
		Uint32Message m;
		ensure_equals(m.feed("\xAB\x0F\x99\xCCzzzzz", 9), (size_t) 4);
		ensure(m.done());
		ensure_equals(m.value(), 2869926348u);
	}
	
	TEST_METHOD(16) {
		// Test feeding garbage after having fed a complete uint32.
		Uint32Message m;
		m.feed("\xAB\x0F\x99\xCC", 4);
		ensure_equals(m.feed("zzzzz", 5), (size_t) 0);
		ensure(m.done());
		ensure_equals(m.value(), 2869926348u);
	}
	
	TEST_METHOD(17) {
		// Test reset.
		Uint32Message m;
		m.feed("\xAB\x0F\x99\xCC", 2);
		m.reset();
		ensure_equals(m.feed("\x00\x11\x22\x33", 4), (size_t) 4);
		ensure(m.done());
		ensure_equals(m.value(), 1122867u);
	}
	
	TEST_METHOD(18) {
		// Test generate.
		char buf[4];
		Uint32Message::generate(buf, 1234567890);
		ensure(memcmp(buf, "\x49\x96\x02\xD2", 4) == 0);
	}
	
	
	/****** Test ArrayMessage ******/
	
	TEST_METHOD(21) {
		// Test initial state.
		ArrayMessage m;
		ensure(!m.done());
		ensure(!m.hasError());
	}
	
	TEST_METHOD(22) {
		// Test feeding 0 bytes.
		ArrayMessage m;
		for (int i = 0; i < 100; i++) {
			ensure_equals(m.feed("", 0), (size_t) 0);
			ensure(!m.done());
			ensure(!m.hasError());
		}
	}
	
	TEST_METHOD(23) {
		// Test feeding bytes one-by-one until complete.
		ArrayMessage m;
		ensure_equals(m.feed("\x00", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("\x07", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("a", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("b", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("\0", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("c", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("d", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("e", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("\0", 1), (size_t) 1);
		ensure(m.done());
		ensure(!m.hasError());
		
		const vector<StaticString> &value = m.value();
		ensure_equals(value.size(), 2u);
		ensure(value[0] == "ab");
		ensure(value[1] == "cde");
	}
	
	TEST_METHOD(24) {
		// Test feeding a complete message.
		ArrayMessage m;
		const char *buf = "\x00\x07" "ab\0cde\0";
		ensure_equals(m.feed(buf, 9), (size_t) 9);
		ensure(m.done());
		ensure(!m.hasError());
		
		const vector<StaticString> &value = m.value();
		ensure_equals(value.size(), 2u);
		ensure(value[0] == "ab");
		ensure(value[1] == "cde");
		
		// Because we fed a complete message in 1 command,
		// the staticstrings will point to the original buffer.
		ensure_equals(value[0].data(), buf + 2);
		ensure_equals(value[1].data(), buf + 5);
	}
	
	TEST_METHOD(25) {
		// Test feeding a message and garbage in 1 feed command.
		ArrayMessage m;
		const char *buf = "\x00\x07" "ab\0cde\0" "zzzzz";
		ensure_equals(m.feed(buf, 14), (size_t) 9);
		ensure(m.done());
		ensure(!m.hasError());
		
		const vector<StaticString> &value = m.value();
		ensure_equals(value.size(), 2u);
		ensure(value[0] == "ab");
		ensure(value[1] == "cde");
		ensure_equals(value[0].data(), buf + 2);
		ensure_equals(value[1].data(), buf + 5);
	}
	
	TEST_METHOD(26) {
		// Test feeding garbage after having fed a complete message in 1 feed command.
		ArrayMessage m;
		const char *buf = "\x00\x07" "ab\0cde\0";
		m.feed(buf, 9);
		ensure_equals(m.feed("zzzzz", 5), (size_t) 0);
		ensure(m.done());
		ensure(!m.hasError());
		
		const vector<StaticString> &value = m.value();
		ensure_equals(value.size(), 2u);
		ensure(value[0] == "ab");
		ensure(value[1] == "cde");
		ensure_equals(value[0].data(), buf + 2);
		ensure_equals(value[1].data(), buf + 5);
	}
	
	TEST_METHOD(27) {
		// Test feeding garbage after having fed a complete message one-by-one byte.
		ArrayMessage m;
		m.feed("\x00", 1);
		m.feed("\x07", 1);
		m.feed("a", 1);
		m.feed("b", 1);
		m.feed("\0", 1);
		m.feed("c", 1);
		m.feed("d", 1);
		m.feed("e", 1);
		m.feed("\0", 1);
		ensure_equals(m.feed("zzzzz", 5), (size_t) 0);
		ensure(m.done());
		ensure(!m.hasError());
		
		const vector<StaticString> &value = m.value();
		ensure_equals(value.size(), 2u);
		ensure(value[0] == "ab");
		ensure(value[1] == "cde");
	}
	
	TEST_METHOD(28) {
		// It should ignore the last entry if it's not null-terminated.
		ArrayMessage m;
		const char *buf = "\x00\x07" "ab\0cdef";
		ensure_equals(m.feed(buf, 9), (size_t) 9);
		ensure(m.done());
		ensure(!m.hasError());
		
		const vector<StaticString> &value = m.value();
		ensure_equals(value.size(), 1u);
		ensure(value[0] == "ab");
	}
	
	TEST_METHOD(29) {
		// It enters an error state if the size is larger than the set maximum.
		ArrayMessage m;
		m.setMaxSize(7);
		
		const char *buf = "\x00\x07" "ab\0cde\0";
		ensure_equals(m.feed(buf, 9), (size_t) 9);
		ensure(m.done());
		ensure(!m.hasError());
		
		const vector<StaticString> &value = m.value();
		ensure_equals(value.size(), 2u);
		ensure(value[0] == "ab");
		ensure(value[1] == "cde");
		ensure_equals(value[0].data(), buf + 2);
		ensure_equals(value[1].data(), buf + 5);
		
		m.reset();
		m.setMaxSize(6);
		ensure_equals(m.feed("\x00\x07", 2), (size_t) 2);
		ensure(m.done());
		ensure(m.hasError());
		ensure_equals(m.errorCode(), ArrayMessage::TOO_LARGE);
	}
	
	TEST_METHOD(30) {
		// Test parsing a message with no items.
		ArrayMessage m;
		ensure_equals(m.feed("\0\0", 2), (size_t) 2);
		ensure("(1)", m.done());
		ensure("(2)", !m.hasError());
		ensure_equals("(3)", m.value().size(), 0u);
		
		m.reset();
		ensure_equals("(4)", m.feed("\0\1" "a", 3), (size_t) 3);
		ensure("(5)", m.done());
		ensure("(6)", !m.hasError());
		ensure_equals("(7)", m.value().size(), 0u);
	}
	
	TEST_METHOD(31) {
		// Test parsing a message with a single item.
		ArrayMessage m;
		ensure_equals(m.feed("\0\3" "ab\0", 5), (size_t) 5);
		ensure(m.done());
		ensure(!m.hasError());
		ensure_equals(m.value().size(), 1u);
		
		const vector<StaticString> &value = m.value();
		ensure(value[0] == "ab");
	}
	
	TEST_METHOD(32) {
		// Test parsing a message with three items.
		ArrayMessage m;
		ensure_equals(m.feed("\x00\x0C" "ab\0cde\0fghi\0", 2 + 12), (size_t) 2 + 12);
		ensure("(1)", m.done());
		ensure("(2)", !m.hasError());
		ensure_equals("(3)", m.value().size(), 3u);
		
		const vector<StaticString> &value = m.value();
		ensure("(4)", value[0] == "ab");
		ensure("(5)", value[1] == "cde");
		ensure("(6)", value[2] == "fghi");
	}
	
	TEST_METHOD(33) {
		// generate() complains if output array has less than the
		// expected number of items.
		StaticString args[] = { "hello", "world" };
		char buf[sizeof(uint16_t)];
		try {
			ArrayMessage::generate(args, 2, buf, NULL,
				ArrayMessage::outputSize(2) - 1);
			fail();
		} catch (const ArgumentException &) {
			// Success.
		}
	}
	
	TEST_METHOD(34) {
		// generate() works.
		StaticString args[] = { "ab", "cde" };
		vector<StaticString> out;
		out.resize(ArrayMessage::outputSize(2));
		char buf[sizeof(uint16_t)];
		ArrayMessage::generate(args, 2, buf, &out[0], ArrayMessage::outputSize(2));
		
		string concat;
		for (unsigned int i = 0; i < ArrayMessage::outputSize(2); i++) {
			concat.append(out[i].data(), out[i].size());
		}
		ensure_equals(concat, string("\x00\x07" "ab\0cde\0", 9));
	}
	
	
	/****** Test ScalarMessage ******/
	
	TEST_METHOD(41) {
		// Test initial state.
		ScalarMessage m;
		ensure(!m.done());
		ensure(!m.hasError());
	}
	
	TEST_METHOD(42) {
		// Test feeding 0 bytes.
		ScalarMessage m;
		for (int i = 0; i < 100; i++) {
			ensure_equals(m.feed("", 0), (size_t) 0);
			ensure(!m.done());
			ensure(!m.hasError());
		}
	}
	
	TEST_METHOD(43) {
		// Test feeding bytes one-by-one until complete.
		ScalarMessage m;
		
		ensure_equals(m.feed("\x00", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("\x01", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("\x02", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		ensure_equals(m.feed("\x03", 1), (size_t) 1);
		ensure(!m.done());
		ensure(!m.hasError());
		
		for (int i = 0; i < 66050; i++) {
			ensure_equals(m.feed("x", 1), (size_t) 1);
			ensure(!m.done());
			ensure(!m.hasError());
		}
		ensure_equals(m.feed("x", 1), (size_t) 1);
		ensure(m.done());
		ensure(!m.hasError());
		
		const StaticString &value = m.value();
		ensure_equals(value.size(), 66051u);
		for (string::size_type i = 0; i < value.size(); i++) {
			ensure_equals(value[i], 'x');
		}
	}
	
	TEST_METHOD(44) {
		// Test feeding a complete message.
		ScalarMessage m;
		string buf;
		buf.append("\x00\x01\x02\x03", 4);
		buf.append(66051, 'x');
		
		ensure_equals(m.feed(buf.data(), buf.size()), (size_t) buf.size());
		ensure("(1)", m.done());
		ensure("(2)", !m.hasError());
		
		const StaticString &value = m.value();
		ensure_equals("(3)", value.size(), 66051u);
		for (string::size_type i = 0; i < value.size(); i++) {
			ensure_equals("(4)", value[i], 'x');
		}
		
		// Because we fed a complete message in 1 command,
		// the staticstrings will point to the original buffer.
		ensure_equals("(5)", value.data(), buf.data() + 4);
	}
	
	TEST_METHOD(45) {
		// Test feeding a message and garbage in 1 feed command.
		ScalarMessage m;
		string buf;
		buf.append("\x00\x01\x02\x03", 4);
		buf.append(66051, 'x');
		buf.append("zzzzz");
		
		ensure_equals("(1)", m.feed(buf.data(), buf.size()), (size_t) buf.size() - 5);
		ensure("(2)", m.done());
		ensure("(3)", !m.hasError());
		
		const StaticString &value = m.value();
		ensure_equals("(4)", value.size(), 66051u);
		for (string::size_type i = 0; i < value.size(); i++) {
			ensure_equals("(5)", value[i], 'x');
		}
		ensure_equals("(6)", value.data(), buf.data() + 4);
	}
	
	TEST_METHOD(46) {
		// Test feeding garbage after having fed a complete message in 1 feed command.
		ScalarMessage m;
		string buf;
		buf.append("\x00\x01\x02\x03", 4);
		buf.append(66051, 'x');
		
		m.feed(buf.data(), buf.size());
		ensure_equals("(1)", m.feed("zzzzz", 5), (size_t) 0);
		ensure("(2)", m.done());
		ensure("(3)", !m.hasError());
		
		const StaticString &value = m.value();
		ensure_equals("(4)", value.size(), 66051u);
		for (string::size_type i = 0; i < value.size(); i++) {
			ensure_equals("(5)", value[i], 'x');
		}
		ensure_equals("(6)", value.data(), buf.data() + 4);
	}
	
	TEST_METHOD(47) {
		// Test feeding garbage after having fed a complete message one-by-one byte.
		ScalarMessage m;
		m.feed("\x00", 1);
		m.feed("\x01", 1);
		m.feed("\x02", 1);
		m.feed("\x03", 1);
		for (int i = 0; i < 66051; i++) {
			m.feed("x", 1);
		}
		
		ensure_equals(m.feed("zzzzz", 5), (size_t) 0);
		ensure(m.done());
		ensure(!m.hasError());
		
		const StaticString &value = m.value();
		ensure_equals("(4)", value.size(), 66051u);
		for (string::size_type i = 0; i < value.size(); i++) {
			ensure_equals("(5)", value[i], 'x');
		}
	}
	
	TEST_METHOD(48) {
		// It enters an error state if the size is larger than the set maximum.
		ScalarMessage m;

		const char *buf = "\x00\x00\x00\x07" "1234567";
		m.setMaxSize(7);
		ensure_equals(m.feed(buf, 11), (size_t) 11);
		ensure(m.done());
		ensure(!m.hasError());
		
		const StaticString &value = m.value();
		ensure_equals(value.size(), 7u);
		ensure(value == "1234567");
		ensure_equals(value.data(), buf + 4);
		
		m.reset();
		m.setMaxSize(6);
		ensure_equals(m.feed("\x00\x00\x00\x07", 4), (size_t) 4);
		ensure(m.done());
		ensure(m.hasError());
		ensure_equals(m.errorCode(), ScalarMessage::TOO_LARGE);
	}
	
	TEST_METHOD(49) {
		// Test parsing message with no body.
		ScalarMessage m;
		ensure_equals(m.feed("\0\0\0\0", 4), (size_t) 4);
		ensure("(1)", m.done());
		ensure("(2)", !m.hasError());
		ensure_equals("(3)", m.value().size(), 0u);
	}
	
	TEST_METHOD(50) {
		// generate() works.
		char buf[sizeof(uint32_t)];
		StaticString out[2];
		ScalarMessage::generate("hello", buf, out);
		
		ensure(out[0] == StaticString("\x00\x00\x00\x05", 4));
		ensure(out[1] == "hello");
	}
}
