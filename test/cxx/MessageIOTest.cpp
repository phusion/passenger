#include <TestSupport.h>
#include <Utils/IOUtils.h>
#include <Utils/MessageIO.h>
#include <Utils/SystemTime.h>

using namespace Passenger;
using namespace std;
using namespace boost;

namespace tut {
	struct MessageIOTest {
		Pipe pipes;
		
		MessageIOTest() {
			pipes = createPipe();
		}
	};

	DEFINE_TEST_GROUP(MessageIOTest);
	
	/***** Test readUint16() and writeUint16() *****/
	
	TEST_METHOD(1) {
		// They work.
		writeUint16(pipes[1], 0x3F56);
		writeUint16(pipes[1], 0x3F57);
		writeUint16(pipes[1], 0x3F58);
		
		unsigned char buf[2];
		ensure_equals(readExact(pipes[0], buf, 2), 2u);
		ensure_equals(buf[0], 0x3F);
		ensure_equals(buf[1], 0x56);
		
		ensure_equals(readUint16(pipes[0]), 0x3F57u);
		
		uint16_t out;
		ensure(readUint16(pipes[0], out));
		ensure_equals(out, 0x3F58);
	}
	
	TEST_METHOD(2) {
		// readUint16() throws EOFException on premature EOF.
		writeExact(pipes[1], "x", 1);
		pipes[1].close();
		try {
			readUint16(pipes[0]);
			fail("EOFException expected");
		} catch (const EOFException &) {
		}
	}
	
	TEST_METHOD(3) {
		// readUint16(uint32_t &) returns false EOFException on premature EOF.
		writeExact(pipes[1], "x", 1);
		pipes[1].close();
		uint16_t out;
		ensure(!readUint16(pipes[0], out));
	}
	
	TEST_METHOD(4) {
		// Test timeout.
		unsigned long long timeout = 30000;
		unsigned long long startTime = SystemTime::getUsec();
		try {
			readUint16(pipes[0], &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure("About 30 ms elapsed (1)", elapsed >= 29000 && elapsed <= 95000);
			ensure("Time is correctly deducted from 'timeout' (1)", timeout <= 2000);
		}
		
		writeUntilFull(pipes[1]);
		
		timeout = 30000;
		startTime = SystemTime::getUsec();
		try {
			writeUint16(pipes[1], 0x12, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure("About 30 ms elapsed (3)", elapsed >= 29000 && elapsed <= 95000);
			ensure("Time is correctly deducted from 'timeout' (4)", timeout <= 2000);
		}
	}
	
	/***** Test readUint32() and writeUint32() *****/
	
	TEST_METHOD(10) {
		// They work.
		writeUint32(pipes[1], 0x12343F56);
		writeUint32(pipes[1], 0x12343F57);
		writeUint32(pipes[1], 0x12343F58);
		
		unsigned char buf[4];
		ensure_equals(readExact(pipes[0], buf, 4), 4u);
		ensure_equals(buf[0], 0x12);
		ensure_equals(buf[1], 0x34);
		ensure_equals(buf[2], 0x3F);
		ensure_equals(buf[3], 0x56);
		
		ensure_equals(readUint32(pipes[0]), 0x12343F57u);
		
		uint32_t out;
		ensure(readUint32(pipes[0], out));
		ensure_equals(out, 0x12343F58u);
	}
	
	TEST_METHOD(11) {
		// readUint32() throws EOFException on premature EOF.
		writeExact(pipes[1], "xyz", 3);
		pipes[1].close();
		try {
			readUint32(pipes[0]);
			fail("EOFException expected");
		} catch (const EOFException &) {
		}
	}
	
	TEST_METHOD(12) {
		// readUint16(uint32_t &) returns false EOFException on premature EOF.
		writeExact(pipes[1], "xyz", 3);
		pipes[1].close();
		uint32_t out;
		ensure(!readUint32(pipes[0], out));
	}
	
	TEST_METHOD(13) {
		// Test timeout.
		unsigned long long timeout = 30000;
		unsigned long long startTime = SystemTime::getUsec();
		try {
			readUint32(pipes[0], &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure(elapsed >= 29000 && elapsed <= 90000);
			ensure(timeout <= 2000);
		}
		
		writeUntilFull(pipes[1]);
		
		timeout = 30000;
		startTime = SystemTime::getUsec();
		try {
			writeUint32(pipes[1], 0x1234, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure(elapsed >= 29000 && elapsed <= 90000);
			ensure(timeout <= 2000);
		}
	}
	
	/***** Test readArrayMessage() and writeArrayMessage() *****/
	
	TEST_METHOD(20) {
		// Test <= 10 arguments.
		writeArrayMessage(pipes[1], "ab", "cd", "efg", NULL);
		writeArrayMessage(pipes[1], "ab", "cd", "efh", NULL);
		
		unsigned char buf[12];
		readExact(pipes[0], buf, 12);
		ensure_equals(buf[0], 0u);
		ensure_equals(buf[1], 10u);
		ensure_equals(buf[2], 'a');
		ensure_equals(buf[3], 'b');
		ensure_equals(buf[4], '\0');
		ensure_equals(buf[5], 'c');
		ensure_equals(buf[6], 'd');
		ensure_equals(buf[7], '\0');
		ensure_equals(buf[8], 'e');
		ensure_equals(buf[9], 'f');
		ensure_equals(buf[10], 'g');
		ensure_equals(buf[11], '\0');
		
		vector<string> args = readArrayMessage(pipes[0]);
		ensure_equals(args.size(), 3u);
		ensure_equals(args[0], "ab");
		ensure_equals(args[1], "cd");
		ensure_equals(args[2], "efh");
	}
	
	TEST_METHOD(21) {
		// Test > 10 arguments.
		writeArrayMessage(pipes[1], "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "a", "b", NULL);
		writeArrayMessage(pipes[1], "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", NULL);
		
		unsigned char buf[26];
		readExact(pipes[0], buf, 26);
		ensure_equals(buf[0], 0u);
		ensure_equals(buf[1], 24u);
		ensure_equals(buf[2], '1');
		ensure_equals(buf[3], '\0');
		ensure_equals(buf[4], '2');
		ensure_equals(buf[5], '\0');
		ensure_equals(buf[6], '3');
		ensure_equals(buf[7], '\0');
		ensure_equals(buf[8], '4');
		ensure_equals(buf[9], '\0');
		ensure_equals(buf[10], '5');
		ensure_equals(buf[11], '\0');
		ensure_equals(buf[12], '6');
		ensure_equals(buf[13], '\0');
		ensure_equals(buf[14], '7');
		ensure_equals(buf[15], '\0');
		ensure_equals(buf[16], '8');
		ensure_equals(buf[17], '\0');
		ensure_equals(buf[18], '9');
		ensure_equals(buf[19], '\0');
		ensure_equals(buf[20], '0');
		ensure_equals(buf[21], '\0');
		ensure_equals(buf[22], 'a');
		ensure_equals(buf[23], '\0');
		ensure_equals(buf[24], 'b');
		ensure_equals(buf[25], '\0');
		
		vector<string> args = readArrayMessage(pipes[0]);
		ensure_equals(args.size(), 12u);
		ensure_equals(args[0], "c");
		ensure_equals(args[1], "d");
		ensure_equals(args[2], "e");
		ensure_equals(args[3], "f");
		ensure_equals(args[4], "g");
		ensure_equals(args[5], "h");
		ensure_equals(args[6], "i");
		ensure_equals(args[7], "j");
		ensure_equals(args[8], "k");
		ensure_equals(args[9], "l");
		ensure_equals(args[10], "m");
		ensure_equals(args[11], "n");
	}
	
	TEST_METHOD(22) {
		// readArrayMessage() throws EOFException on premature EOF.
		writeExact(pipes[1], "\x00");
		pipes[1].close();
		try {
			readArrayMessage(pipes[0]);
			fail("EOFException expected (1)");
		} catch (const EOFException &) {
		}
		
		pipes = createPipe();
		writeExact(pipes[1], "\x00\x04a\x00b");
		pipes[1].close();
		try {
			readArrayMessage(pipes[0]);
			fail("EOFException expected (2)");
		} catch (const EOFException &) {
		}
	}
	
	TEST_METHOD(23) {
		// Test timeout.
		unsigned long long timeout = 30000;
		unsigned long long startTime = SystemTime::getUsec();
		try {
			readArrayMessage(pipes[0], &timeout);
			fail("TimeoutException expected (1)");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure(elapsed >= 29000 && elapsed <= 90000);
			ensure(timeout <= 2000);
		}
		
		writeUntilFull(pipes[1]);
		
		timeout = 30000;
		startTime = SystemTime::getUsec();
		try {
			writeArrayMessage(pipes[1], &timeout, "hi", "ho", NULL);
			fail("TimeoutException expected (2)");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure(elapsed >= 29000 && elapsed <= 90000);
			ensure(timeout <= 2000);
		}
	}
	
	/***** Test readScalarMessage() and writeScalarMessage() *****/
	
	TEST_METHOD(30) {
		// They work.
		writeScalarMessage(pipes[1], "hello");
		writeScalarMessage(pipes[1], "world");
		
		unsigned char buf[4 + 5];
		readExact(pipes[0], buf, 4 + 5);
		ensure_equals(buf[0], 0u);
		ensure_equals(buf[1], 0u);
		ensure_equals(buf[2], 0u);
		ensure_equals(buf[3], 5u);
		ensure_equals(buf[4], 'h');
		ensure_equals(buf[5], 'e');
		ensure_equals(buf[6], 'l');
		ensure_equals(buf[7], 'l');
		ensure_equals(buf[8], 'o');
		
		ensure_equals(readScalarMessage(pipes[0]), "world");
	}
	
	TEST_METHOD(31) {
		// readScalarMessage() throws EOFException on premature EOF.
		writeExact(pipes[1], StaticString("\x00", 1));
		pipes[1].close();
		try {
			readScalarMessage(pipes[0]);
			fail("EOFException expected (1)");
		} catch (const EOFException &) {
		}
		
		pipes = createPipe();
		writeExact(pipes[1], StaticString("\x00\x00\x00\x04" "abc", 4 + 3));
		pipes[1].close();
		try {
			readScalarMessage(pipes[0]);
			fail("EOFException expected (2)");
		} catch (const EOFException &) {
		}
	}
	
	TEST_METHOD(32) {
		// readScalarMessage() throws SecurityException if the
		// body larger than the limit
		writeExact(pipes[1], StaticString("\x00\x00\x00\x05", 4));
		try {
			readScalarMessage(pipes[0], 4);
			fail("SecurityException expected (1)");
		} catch (const SecurityException &) {
		}
	}
	
	TEST_METHOD(33) {
		// Test timeout.
		unsigned long long timeout = 30000;
		unsigned long long startTime = SystemTime::getUsec();
		try {
			readScalarMessage(pipes[0], 0, &timeout);
			fail("TimeoutException expected (1)");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure(elapsed >= 29000 && elapsed <= 90000);
			ensure(timeout <= 2000);
		}
		
		writeUntilFull(pipes[1]);
		
		timeout = 30000;
		startTime = SystemTime::getUsec();
		try {
			writeScalarMessage(pipes[1], "hello", &timeout);
			fail("TimeoutException expected (2)");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure(elapsed >= 29000 && elapsed <= 90000);
			ensure(timeout <= 2000);
		}
	}
}
