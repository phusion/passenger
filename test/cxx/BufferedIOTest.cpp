#include "TestSupport.h"
#include "Utils/BufferedIO.h"
#include "Utils/Timer.h"
#include "Utils/IOUtils.h"
#include <algorithm>

using namespace Passenger;
using namespace std;

namespace tut {
	struct BufferedIOTest {
		FileDescriptor reader, writer;
		BufferedIO io;
		
		string readData;
		unsigned int counter;
		char buf[100];
		BufferedIO::AcceptFunction a_eof;
		BufferedIO::AcceptFunction a_twoBytesRead;
		
		BufferedIOTest() {
			Pipe p = createPipe();
			reader = p.first;
			writer = p.second;
			io = BufferedIO(reader);
			counter = 0;
			memset(buf, 0, sizeof(buf));
			a_eof = boost::bind(&BufferedIOTest::eof, this, _1, _2);
			a_twoBytesRead = boost::bind(&BufferedIOTest::twoBytesRead, this, _1, _2);
		}
		
		void write(const StaticString &data) {
			::write(writer, data.c_str(), data.size());
		}
		
		pair<unsigned int, bool> twoBytesRead(const char *data, unsigned int size) {
			if (counter == 2) {
				return make_pair(0, true);
			} else {
				unsigned int toRead = min(2u - counter, size);
				readData.append(data, toRead);
				counter += toRead;
				return make_pair(toRead, counter == 2);
			}
		}
		
		pair<unsigned int, bool> eof(const char *data, unsigned int size) {
			readData.append(data, size);
			return make_pair(size, false);
		}
		
		static void writeAfterSomeTime(int fd, int sleepTime, string data) {
			syscalls::usleep(sleepTime);
			writeExact(fd, data);
		}
		
		static void closeAfterSomeTime(FileDescriptor fd, int sleepTime) {
			syscalls::usleep(sleepTime);
			fd.close();
		}
	};
	
	DEFINE_TEST_GROUP(BufferedIOTest);

	/***** Test readUntil() *****/
	
	TEST_METHOD(1) {
		// If the connection is already closed and the buffer is empty, then it returns 0.
		writer.close();
		ensure_equals(io.readUntil(a_eof), 0u);
		ensure_equals(readData, "");
		ensure_equals(io.getBuffer(), "");
	}
	
	TEST_METHOD(2) {
		// If the connection is already closed and the buffer is non-empty,
		// then it reads from the buffer.
		writer.close();
		io.unread("hello world");
		ensure_equals(io.readUntil(a_twoBytesRead), 2u);
		ensure_equals(readData, "he");
		ensure_equals(io.readUntil(a_eof), 9u);
		ensure_equals(readData, "hello world");
		ensure_equals(io.getBuffer(), "");
	}
	
	TEST_METHOD(3) {
		// If the buffer is empty then it reads from the connection.
		write("hello world");
		writer.close();
		ensure_equals("(1)", io.readUntil(a_twoBytesRead), 2u);
		ensure_equals("(2)", readData, "he");
		ensure_equals("(5)", io.readUntil(a_eof), 9u);
		ensure_equals("(6)", readData, "hello world");
		ensure_equals("(7)", io.readUntil(a_eof), 0u);
		ensure_equals("(8)", readData, "hello world");
		ensure_equals(io.getBuffer(), "");
	}
	
	TEST_METHOD(4) {
		// If the buffer is non-empty then it reads from the
		// buffer first, then from the connection.
		io.unread("hel");
		write("lo world");
		writer.close();
		
		ensure_equals("(1)", io.readUntil(a_twoBytesRead), 2u);
		ensure_equals("(2)", readData, "he");
		counter = 0;
		ensure_equals("(3)", io.readUntil(a_twoBytesRead), 2u);
		ensure_equals("(4)", readData, "hell");
		ensure_equals("(5)", io.readUntil(a_eof), 7u);
		ensure_equals("(6)", readData, "hello world");
		ensure_equals("(7)", io.readUntil(a_eof), 0u);
		ensure_equals("(8)", readData, "hello world");
		ensure_equals(io.getBuffer(), "");
	}
	
	TEST_METHOD(5) {
		// It blocks until the acceptor function says it's done or until EOF.
		TempThread thr1(boost::bind(writeAfterSomeTime, writer, 20000, "aa"));
		Timer timer1;
		ensure_equals(io.readUntil(a_twoBytesRead), 2u);
		ensure_equals(readData, "aa");
		ensure("At least 18 msec elapsed", timer1.elapsed() >= 18);
		ensure("At most 30 msec elapsed", timer1.elapsed() <= 30);
		
		TempThread thr2(boost::bind(closeAfterSomeTime, writer, 20000));
		Timer timer2;
		ensure_equals(io.readUntil(a_twoBytesRead), 0u);
		ensure_equals(readData, "aa");
		ensure("At least 18 msec elapsed", timer2.elapsed() >= 18);
		ensure("At most 30 msec elapsed", timer2.elapsed() <= 30);
	}
	
	TEST_METHOD(6) {
		// It throws TimeoutException if it cannot read enough data
		// within the specified timeout.
		unsigned long long timeout = 50000;
		io.unread("he");
		write("llo");
		Timer timer;
		try {
			io.readUntil(a_eof, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			ensure("At least 45 msec elapsed", timer.elapsed() >= 45);
			ensure("At most 65 msec elapsed", timer.elapsed() < 65);
			ensure("It deducts the waited time from the timeout", timeout < 5000);
			ensure_equals(readData, "hello");
			ensure_equals(io.getBuffer(), "");
		}
	}
	
	/***** Test read() *****/
	
	TEST_METHOD(10) {
		// If the connection is already closed and the buffer is empty, then it returns 0.
		writer.close();
		ensure_equals(io.read(buf, sizeof(buf)), 0u);
		ensure_equals(io.getBuffer(), "");
	}
	
	TEST_METHOD(11) {
		// If the connection is already closed and the buffer is non-empty
		// and >= N bytes, then it reads everything from the buffer.
		io.unread("hel");
		write("lo world");
		writer.close();
		ensure_equals(io.read(buf, 5), 5u);
		ensure_equals(StaticString(buf), "hello");
		ensure_equals(io.getBuffer(), " world");
	}
	
	TEST_METHOD(12) {
		// If the connection is already closed and the buffer is non-empty
		// and < N bytes, then it reads N bytes from the buffer and the rest
		// from the connection.
		io.unread("hel");
		write("lo world");
		writer.close();
		ensure_equals(io.read(buf, sizeof(buf)), 11u);
		ensure_equals(StaticString(buf), "hello world");
		ensure_equals(io.getBuffer(), "");
	}
	
	TEST_METHOD(13) {
		// If the buffer is empty then it reads from the connection.
		write("hello world");
		ensure_equals(io.read(buf, 5), 5u);
		ensure_equals(StaticString(buf), "hello");
		ensure_equals(io.getBuffer(), " world");
	}
	
	TEST_METHOD(14) {
		// If the buffer is non-empty then it reads from the
		// buffer first, then from the connection.
		write("hello world");
		
		ensure_equals(io.read(buf, 2), 2u);
		ensure_equals(StaticString(buf), "he");
		ensure_equals(io.getBuffer(), "llo world");
		
		ensure_equals(io.read(buf, 7), 7u);
		ensure_equals(StaticString(buf), "llo wor");
		ensure_equals(io.getBuffer(), "ld");
	}
	
	TEST_METHOD(15) {
		// It blocks until the given number of bytes are read or until EOF.
		TempThread thr1(boost::bind(writeAfterSomeTime, writer, 20000, "aa"));
		Timer timer1;
		ensure_equals(io.read(buf, 2), 2u);
		ensure_equals(StaticString(buf), "aa");
		ensure("At least 18 msec elapsed", timer1.elapsed() >= 18);
		ensure("At most 30 msec elapsed", timer1.elapsed() <= 30);
		
		TempThread thr2(boost::bind(closeAfterSomeTime, writer, 20000));
		Timer timer2;
		ensure_equals(io.read(buf, sizeof(buf)), 0u);
		ensure_equals(StaticString(buf), "aa");
		ensure("At least 18 msec elapsed", timer2.elapsed() >= 18);
		ensure("At most 30 msec elapsed", timer2.elapsed() <= 30);
	}
	
	TEST_METHOD(16) {
		// It throws TimeoutException if it cannot read enough data
		// within the specified timeout.
		unsigned long long timeout = 50000;
		io.unread("he");
		write("llo");
		Timer timer;
		try {
			io.read(buf, sizeof(buf), &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			ensure("At least 45 msec elapsed", timer.elapsed() >= 45);
			ensure("At most 65 msec elapsed", timer.elapsed() < 65);
			ensure("It deducts the waited time from the timeout", timeout < 5000);
			ensure_equals(io.getBuffer(), "");
		}
	}
	
	/***** Test readAll() *****/
	
	TEST_METHOD(20) {
		// It reads everything until EOF.
		TempThread thr1(boost::bind(writeAfterSomeTime, writer, 20000, "aa"));
		TempThread thr2(boost::bind(closeAfterSomeTime, writer, 40000));
		Timer timer;
		ensure_equals(io.readAll(), "aa");
		ensure_equals(io.getBuffer(), "");
		ensure("At least 38 msec elapsed", timer.elapsed() >= 38);
		ensure("At most 50 msec elapsed", timer.elapsed() <= 50);
	}
	
	TEST_METHOD(21) {
		// It throws TimeoutException if it cannot read enough data
		// within the specified timeout.
		unsigned long long timeout = 50000;
		io.unread("he");
		write("llo");
		Timer timer;
		try {
			io.readAll(&timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			ensure("At least 45 msec elapsed", timer.elapsed() >= 45);
			ensure("At most 65 msec elapsed", timer.elapsed() < 65);
			ensure("It deducts the waited time from the timeout", timeout < 5000);
			ensure_equals(io.getBuffer(), "");
		}
	}
	
	/***** Test readLine() *****/
	
	TEST_METHOD(25) {
		// If the connection is already closed and the buffer is empty,
		// then it returns the empty string.
		writer.close();
		ensure_equals(io.readLine(), "");
		ensure_equals(io.getBuffer(), "");
	}
	
	TEST_METHOD(26) {
		// If the connection is already closed and the buffer is non-empty,
		// then it returns the first line in the buffer.
		writer.close();
		io.unread("hello\nworld\n.");
		ensure_equals(io.readLine(), "hello\n");
		ensure_equals(io.getBuffer(), "world\n.");
		ensure_equals(io.readLine(), "world\n");
		ensure_equals(io.getBuffer(), ".");
		ensure_equals(io.readLine(), ".");
		ensure_equals(io.getBuffer(), "");
	}
	
	TEST_METHOD(27) {
		// If the buffer is empty then it reads from the connection.
		write("hello\nworld\n.");
		ensure_equals(io.readLine(), "hello\n");
		ensure_equals(io.getBuffer(), "world\n.");
	}
	
	TEST_METHOD(28) {
		// If the buffer is non-empty then it reads from the
		// buffer first, then from the connection.
		io.unread("hello");
		write("\nworld\n.");
		ensure_equals(io.readLine(), "hello\n");
		ensure_equals(io.getBuffer(), "world\n.");
		ensure_equals(io.readLine(), "world\n");
		ensure_equals(io.getBuffer(), ".");
	}
	
	TEST_METHOD(29) {
		// If the line is too long then it throws a SecurityException.
		write("abcd");
		try {
			io.readLine(3);
			fail("SecurityException expected");
		} catch (const SecurityException &) {
			// Pass.
		}
	}
	
	TEST_METHOD(30) {
		// It blocks until a line can be read or until EOF.
		TempThread thr1(boost::bind(writeAfterSomeTime, writer, 20000, "hello"));
		TempThread thr2(boost::bind(writeAfterSomeTime, writer, 35000, "\nworld\n."));
		Timer timer1;
		ensure_equals(io.readLine(), "hello\n");
		ensure_equals(io.getBuffer(), "world\n.");
		ensure("At least 33 msec elapsed", timer1.elapsed() >= 33);
		ensure("At most 45 msec elapsed", timer1.elapsed() <= 45);
		
		TempThread thr3(boost::bind(closeAfterSomeTime, writer, 20000));
		Timer timer2;
		ensure_equals(io.readLine(), "world\n");
		ensure_equals(io.getBuffer(), ".");
		ensure_equals(io.readLine(), ".");
		ensure_equals(io.getBuffer(), "");
		ensure("At least 18 msec elapsed", timer2.elapsed() >= 18);
		ensure("At most 30 msec elapsed", timer2.elapsed() <= 30);
	}
	
	TEST_METHOD(31) {
		// It throws TimeoutException if it cannot read enough data
		// within the specified timeout.
		unsigned long long timeout = 30000;
		io.unread("he");
		write("llo");
		Timer timer;
		try {
			io.readLine(1024, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			ensure("At least 25 msec elapsed", timer.elapsed() >= 25);
			ensure("At most 40 msec elapsed", timer.elapsed() < 40);
			ensure("It deducts the waited time from the timeout", timeout < 5000);
			ensure_equals(io.getBuffer(), "");
		}
	}
}
