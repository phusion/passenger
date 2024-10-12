#include <TestSupport.h>
#include <IOTools/IOUtils.h>
#include <SystemTools/SystemTime.h>
#include <oxt/system_calls.hpp>
#include <boost/bind/bind.hpp>
#include <sys/types.h>
#include <cerrno>
#include <string>
#include <algorithm>

using namespace Passenger;
using namespace std;
using namespace boost;
using namespace oxt;

namespace tut {
	static ssize_t writevResult;
	static int writevErrno;
	static int writevCalled;
	static string writevData;

	static ssize_t writev_mock(int fildes, const struct iovec *iov, int iovcnt) {
		if (writevResult >= 0) {
			string data;
			for (int i = 0; i < iovcnt && data.size() < (size_t) writevResult; i++) {
				data.append(
					(const char *) iov[i].iov_base,
					iov[i].iov_len);
			}
			data.resize(writevResult);
			writevData.append(data);
		}
		writevCalled++;
		errno = writevErrno;
		return writevResult;
	}

	struct IOTools_IOUtilsTest: public TestBase {
		string restBuffer;

		IOTools_IOUtilsTest() {
			writevResult = 0;
			writevErrno = 0;
			writevCalled = 0;
			writevData.clear();
			setWritevFunction(writev_mock);
		}

		~IOTools_IOUtilsTest() {
			setWritevFunction(NULL);
		}

		Pipe createNonBlockingPipe() {
			Pipe p = createPipe(__FILE__, __LINE__);
			setNonBlocking(p.second);
			return p;
		}

		static void writeDataAfterSomeTime(int fd, unsigned int sleepTimeInUsec) {
			try {
				syscalls::usleep(sleepTimeInUsec);
				syscalls::write(fd, "hi", 2);
			} catch (const boost::thread_interrupted &) {
				// Do nothing.
			}
		}

		static void writeDataSlowly(int fd, unsigned int bytesToWrite, unsigned int bytesPerSec) {
			try {
				MonotonicTimeUsec startTime = SystemTime::getMonotonicUsec();
				MonotonicTimeUsec deadline = startTime + static_cast<MonotonicTimeUsec>(bytesToWrite) * 1e6 / bytesPerSec;
				unsigned int bytesWritten = 0;
				string data(bytesToWrite, 'x');

				while (bytesWritten < bytesToWrite && !boost::this_thread::interruption_requested()) {
					MonotonicTimeUsec elapsed = SystemTime::getMonotonicUsec() - startTime;
					unsigned int shouldHaveWritten = static_cast<unsigned int>(elapsed * (static_cast<double>(bytesPerSec) / 1e6));

					if (shouldHaveWritten > bytesWritten) {
						ssize_t ret = syscalls::write(fd, data.data(), shouldHaveWritten - bytesWritten);
						if (ret == -1) {
							int e = errno;
							throw SystemException("Write error", e);
						}
						bytesWritten += ret;
					}

					MonotonicTimeUsec now = SystemTime::getMonotonicUsec();
					if (now < deadline) {
						syscalls::usleep(std::min<useconds_t>(deadline - now, 10000));
					}
				}
			} catch (const boost::thread_interrupted &) {
				// Do nothing.
			}
		}

		static void readDataAfterSomeTime(int fd, unsigned int sleepTimeInUsec) {
			try {
				char buf[1024 * 8];
				syscalls::usleep(sleepTimeInUsec);
				syscalls::read(fd, buf, sizeof(buf));
			} catch (const boost::thread_interrupted &) {
				// Do nothing.
			}
		}

		static void readDataSlowly(int fd, int bytesToRead, int bytesPerSec) {
			try {
				unsigned long long start = SystemTime::getUsec();
				unsigned long long deadline = start +
					(bytesToRead * 1000000.0 / bytesPerSec);
				int alreadyRead = 0;

				while (alreadyRead < bytesToRead && !boost::this_thread::interruption_requested()) {
					unsigned long long elapsed = SystemTime::getUsec();
					double progress = (elapsed - start) / (double) (deadline - start);
					int shouldHaveRead = progress * bytesToRead;
					int shouldNowRead = shouldHaveRead - alreadyRead;

					if (shouldNowRead > 0) {
						char *buf = new char[shouldNowRead];
						ssize_t ret = syscalls::read(fd, buf, shouldNowRead);
						int e = errno;
						delete[] buf;
						if (ret == -1) {
							throw SystemException("read error", e);
						} else if (ret == 0) {
							break;
						}
						alreadyRead += ret;
					}
					syscalls::usleep(1000);
				}
			} catch (const boost::thread_interrupted &) {
				// Do nothing.
			}
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(IOTools_IOUtilsTest, 100);

	/***** Test gatheredWrite() with empty input rest buffer *****/

	TEST_METHOD(1) {
		// Test complete write of a single data buffer.
		StaticString data = "hello world";
		writevResult = data.size();
		ensure_equals(gatheredWrite(0, &data, 1, restBuffer), writevResult);
		ensure_equals(writevData, "hello world");
		ensure(restBuffer.empty());
	}

	TEST_METHOD(2) {
		// Test complete write of multiple data buffers.
		StaticString data[] = { "hello ", "world", "!!!!!!" };
		writevResult = strlen("hello world!!!!!!");
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "hello world!!!!!!");
		ensure(restBuffer.empty());
	}

	TEST_METHOD(3) {
		// Test partial write of a single data buffer.
		StaticString data = "hello world";
		writevResult = 3;
		ensure_equals(gatheredWrite(0, &data, 1, restBuffer), writevResult);
		ensure_equals(writevData, "hel");
		ensure_equals(restBuffer, "lo world");
	}

	TEST_METHOD(4) {
		// Test partial write of multiple data buffers:
		// first buffer is partially written.
		StaticString data[] = { "hello ", "world", "!!!!!!" };
		writevResult = 2;
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "he");
		ensure_equals(restBuffer, "llo world!!!!!!");
	}

	TEST_METHOD(5) {
		// Test partial write of multiple data buffers:
		// first buffer is completely written.
		StaticString data[] = { "hello ", "world", "!!!!!!" };
		writevResult = 6;
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "hello ");
		ensure_equals(restBuffer, "world!!!!!!");
	}

	TEST_METHOD(6) {
		// Test partial write of multiple data buffers:
		// non-first buffer is partially written.
		StaticString data[] = { "hello ", "world", "!!!!!!" };
		writevResult = 8;
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "hello wo");
		ensure_equals(restBuffer, "rld!!!!!!");
	}

	TEST_METHOD(7) {
		// Test partial write of multiple data buffers:
		// non-first buffer is completely written.
		StaticString data[] = { "hello ", "world", "!!!!!!" };
		writevResult = 11;
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "hello world");
		ensure_equals(restBuffer, "!!!!!!");
	}

	TEST_METHOD(8) {
		// Test failed write of a single data buffer: blocking error.
		StaticString data = "hello world";
		writevResult = -1;
		writevErrno = EAGAIN;
		ensure_equals(gatheredWrite(0, &data, 1, restBuffer), 0);
		ensure_equals(restBuffer, "hello world");
	}

	TEST_METHOD(9) {
		// Test failed write of a single data buffer: other error.
		StaticString data = "hello world";
		writevResult = -1;
		writevErrno = EBADF;
		ssize_t ret = gatheredWrite(0, &data, 1, restBuffer);
		int e = errno;
		ensure_equals(ret, -1);
		ensure_equals(e, EBADF);
		ensure_equals("Rest buffer remains untouched", restBuffer, "");
	}

	TEST_METHOD(10) {
		// Test failed write of multiple data buffers: blocking error.
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = -1;
		writevErrno = EAGAIN;
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), 0);
		ensure_equals(restBuffer, "hello world!!!");
	}

	TEST_METHOD(11) {
		// Test failed write of multiple data buffers: other error.
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = -1;
		writevErrno = EBADF;
		ssize_t ret = gatheredWrite(0, data, 3, restBuffer);
		int e = errno;
		ensure_equals(ret, -1);
		ensure_equals(e, EBADF);
		ensure_equals("Rest buffer remains untouched", restBuffer, "");
	}

	TEST_METHOD(12) {
		// Test writing nothing.
		StaticString data[] = { "", "", "" };
		ssize_t ret = gatheredWrite(0, data, 3, restBuffer);
		int e = errno;
		ensure_equals(ret, 0);
		ensure_equals(e, 0);
		ensure_equals(writevCalled, 0);
		ensure_equals(restBuffer, "");
	}

	TEST_METHOD(13) {
		// Test writing multiple buffers where some are empty.
		StaticString data[] = { "hello ", "", "world" };
		writevResult = strlen("hello world");
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "hello world");
		ensure_equals(restBuffer, "");
	}

	/***** Test gatheredWrite() with non-empty input rest buffer *****/

	TEST_METHOD(15) {
		// Test complete write with a single data buffer.
		restBuffer = "oh ";
		StaticString data = "hello world";
		writevResult = restBuffer.size() + data.size();
		ensure_equals(gatheredWrite(0, &data, 1, restBuffer), writevResult);
		ensure_equals(writevData, "oh hello world");
		ensure(restBuffer.empty());
	}

	TEST_METHOD(16) {
		// Test complete write with multiple data buffers.
		restBuffer = "oh ";
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = strlen("oh hello world!!!");
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "oh hello world!!!");
		ensure(restBuffer.empty());
	}

	TEST_METHOD(17) {
		// Test partial write of a single data buffer.
		StaticString data = "hello world";
		writevResult = 3;
		ensure_equals(gatheredWrite(0, &data, 1, restBuffer), writevResult);
		ensure_equals(writevData, "hel");
		ensure_equals(restBuffer, "lo world");
	}

	TEST_METHOD(18) {
		// Test partial write of multiple data buffers:
		// rest buffer is partially written.
		restBuffer = "oh ";
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = 2;
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "oh");
		ensure_equals(restBuffer, " hello world!!!");
	}

	TEST_METHOD(19) {
		// Test partial write of multiple data buffers:
		// rest buffer is completely written.
		restBuffer = "oh ";
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = strlen("oh ");
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "oh ");
		ensure_equals(restBuffer, "hello world!!!");
	}

	TEST_METHOD(20) {
		// Test partial write of multiple data buffers:
		// first buffer is partially written.
		restBuffer = "oh ";
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = strlen("oh h");
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "oh h");
		ensure_equals(restBuffer, "ello world!!!");
	}

	TEST_METHOD(21) {
		// Test partial write of multiple data buffers:
		// first buffer is completely written.
		restBuffer = "oh ";
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = strlen("oh hello ");
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "oh hello ");
		ensure_equals(restBuffer, "world!!!");
	}

	TEST_METHOD(22) {
		// Test partial write of multiple data buffers:
		// non-first buffer is partially written.
		restBuffer = "oh ";
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = strlen("oh hello wo");
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "oh hello wo");
		ensure_equals(restBuffer, "rld!!!");
	}

	TEST_METHOD(23) {
		// Test partial write of multiple data buffers:
		// non-first buffer is completely written.
		restBuffer = "oh ";
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = strlen("oh hello world");
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "oh hello world");
		ensure_equals(restBuffer, "!!!");
	}

	TEST_METHOD(24) {
		// Test failed write of a single data buffer: blocking error.
		restBuffer = "oh ";
		StaticString data = "hello world";
		writevResult = -1;
		writevErrno = EAGAIN;
		ensure_equals(gatheredWrite(0, &data, 1, restBuffer), 0);
		ensure_equals(restBuffer, "oh hello world");
	}

	TEST_METHOD(25) {
		// Test failed write of a single data buffer: other error.
		restBuffer = "oh ";
		StaticString data = "hello world";
		writevResult = -1;
		writevErrno = EBADF;
		ssize_t ret = gatheredWrite(0, &data, 1, restBuffer);
		int e = errno;
		ensure_equals(ret, -1);
		ensure_equals(e, EBADF);
		ensure_equals("Rest buffer remains untouched", restBuffer, "oh ");
	}

	TEST_METHOD(26) {
		// Test failed write of multiple data buffers: blocking error.
		restBuffer = "oh ";
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = -1;
		writevErrno = EAGAIN;
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), 0);
		ensure_equals(restBuffer, "oh hello world!!!");
	}

	TEST_METHOD(27) {
		// Test failed write of multiple data buffers: other error.
		restBuffer = "oh ";
		StaticString data[] = { "hello ", "world", "!!!" };
		writevResult = -1;
		writevErrno = EBADF;
		ssize_t ret = gatheredWrite(0, data, 3, restBuffer);
		int e = errno;
		ensure_equals(ret, -1);
		ensure_equals(e, EBADF);
		ensure_equals("Rest buffer remains untouched", restBuffer, "oh ");
	}

	TEST_METHOD(28) {
		// Test writing multiple buffers that are all empty.
		restBuffer = "oh ";
		StaticString data[] = { "", "", "" };
		writevResult = 3;
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "oh ");
		ensure_equals(restBuffer, "");
	}

	TEST_METHOD(29) {
		// Test writing multiple buffers where one is empty.
		restBuffer = "oh ";
		StaticString data[] = { "hello ", "", "world" };
		writevResult = strlen("oh hello world");
		ensure_equals(gatheredWrite(0, data, 3, restBuffer), writevResult);
		ensure_equals(writevData, "oh hello world");
		ensure_equals(restBuffer, "");
	}


	/***** Test gatheredWrite() blocking version *****/

	TEST_METHOD(35) {
		// It doesn't call writev() if requested to send 0 bytes.
		StaticString data[2] = { "", "" };
		gatheredWrite(0, data, 2);
		ensure_equals(writevCalled, 0);
	}

	TEST_METHOD(36) {
		// Test sending all data in a single writev() call.
		StaticString data[] = { "hello", "my", "world" };
		writevResult = strlen("hellomyworld");
		gatheredWrite(0, data, 3);
		ensure_equals(writevData, "hellomyworld");
		ensure_equals(writevCalled, 1);
	}

	TEST_METHOD(42) {
		// Test writing byte-by-byte.
		StaticString data[] = { "hello", "my", "world", "!!" };
		writevResult = 1;
		gatheredWrite(0, data, 4);
		ensure_equals(writevCalled, (int) strlen("hellomyworld!!"));
		ensure_equals(writevData, "hellomyworld!!");
	}

	TEST_METHOD(43) {
		// Test writev() writing in chunks of 2 bytes.
		StaticString data[] = { "hello", "my", "world", "!!" };
		writevResult = 2;
		gatheredWrite(0, data, 4);
		ensure_equals(writevCalled, (int) strlen("hellomyworld!!") / 2);
		ensure_equals(writevData, "hellomyworld!!");
	}

	static ssize_t writev_mock_44(int fildes, const struct iovec *iov, int iovcnt) {
		if (writevCalled == 3) {
			// Have the last call return 2 instead of 4.
			writevResult = 2;
		}
		return writev_mock(fildes, iov, iovcnt);
	}

	TEST_METHOD(44) {
		// Test writev() writing in chunks of 4 bytes.
		setWritevFunction(writev_mock_44);
		StaticString data[] = { "hello", "my", "world", "!!" };
		writevResult = 4;
		gatheredWrite(0, data, 4);
		ensure_equals(writevCalled, 4);
		ensure_equals(writevData, "hellomyworld!!");
	}

	TEST_METHOD(45) {
		// Test writev() timeout support.
		setWritevFunction(NULL);
		Pipe p = createPipe(__FILE__, __LINE__);
		unsigned long long startTime = SystemTime::getUsec();
		unsigned long long timeout = 30000;
		char data1[1024], data2[1024];
		StaticString data[] = {
			StaticString(data1, sizeof(data1) - 1),
			StaticString(data2, sizeof(data2) - 1)
		};
		memset(data1, 'x', sizeof(data1));
		memset(data2, 'y', sizeof(data2));

		try {
			for (int i = 0; i < 1024; i++) {
				gatheredWrite(p[1], data, 2, &timeout);
			}
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure("At least 29 msec have passed", elapsed >= 29000);
			ensure("At most 95 msec have passed", elapsed <= 95000);
			ensure(timeout <= 2000);
		}
	}

	/***** Test waitUntilReadable() *****/

	TEST_METHOD(50) {
		// waitUntilReadable() waits for the specified timeout if no data is readable.
		Pipe p = createPipe(__FILE__, __LINE__);
		unsigned long long timeout = 25000;
		ensure("No data is available", !waitUntilReadable(p.first, &timeout));
		ensure("The passed time is deducted from the timeout", timeout < 5000);
	}

	TEST_METHOD(51) {
		// waitUntilReadable() waits for less than the specified timeout if data
		// is not available immediately but still available before the timeout.
		Pipe p = createPipe(__FILE__, __LINE__);
		TempThread thr(boost::bind(&writeDataAfterSomeTime, p.second, 35000));

		unsigned long long timeout = 1000000;
		ensure("Data is available", waitUntilReadable(p.first, &timeout));
		ensure("At least 35 msec passed.", timeout <= 1000000 - 35000);
		ensure("At most 250 msec passed.", timeout >= 1000000 - 250000);  // depends on system scheduler though
	}

	TEST_METHOD(52) {
		// waitUntilReadable() returns immediately if timeout is 0.
		Pipe p = createPipe(__FILE__, __LINE__);
		unsigned long long timeout = 0;
		ensure("No data is available", !waitUntilReadable(p.first, &timeout));
		ensure_equals("Timeout is not modified", timeout, 0u);

		write(p.second, "hi", 2);
		ensure("Data is available", waitUntilReadable(p.first, &timeout));
		ensure_equals("Timeout is not modified", timeout, 0u);
	}

	TEST_METHOD(53) {
		// waitUntilReadable() returns immediately if there's data immediately available.
		Pipe p = createPipe(__FILE__, __LINE__);
		unsigned long long timeout = 100000;
		write(p.second, "hi", 2);
		ensure("Data is available", waitUntilReadable(p.first, &timeout));
		ensure("Timeout is not modified", timeout >= 100000 - 5000);
	}

	/***** Test readExact() *****/

	TEST_METHOD(54) {
		// readExact() throws TimeoutException if no data is received within the timeout.
		Pipe p = createPipe(__FILE__, __LINE__);
		unsigned long long timeout = 50000;
		char buf;
		try {
			readExact(p.first, &buf, 1, &timeout);
			fail("No TimeoutException thrown.");
		} catch (const TimeoutException &) {
			ensure("The passed time is deducted from timeout", timeout < 5000);
		}
	}

	TEST_METHOD(55) {
		// readExact() throws TimeoutException if not enough data is received within the timeout.
		Pipe p = createPipe(__FILE__, __LINE__);
		unsigned long long timeout = 20000;
		char buf[100];

		TempThread thr(boost::bind(&writeDataSlowly, p.second, sizeof(buf), 1));

		try {
			readExact(p.first, &buf, sizeof(buf), &timeout);
			fail("No TimeoutException thrown.");
		} catch (const TimeoutException &) {
			ensure("The passed time is deducted from timeout", timeout < 5000);
		}
	}

	TEST_METHOD(56) {
		// readExact() throws TimeoutException if timeout is 0 and no data is immediately available.
		Pipe p = createPipe(__FILE__, __LINE__);
		unsigned long long timeout = 0;
		char buf;
		try {
			readExact(p.first, &buf, 1, &timeout);
			fail("No TimeoutException thrown.");
		} catch (const TimeoutException &) {
			ensure_equals("Timeout unchanged", timeout, 0u);
		}
	}

	TEST_METHOD(57) {
		// readExact() throws TimeoutException if timeout is 0 and not enough data is
		// immediately available.
		Pipe p = createPipe(__FILE__, __LINE__);
		unsigned long long timeout = 0;
		write(p.second, "hi", 2);
		try {
			char buf[100];
			readExact(p.first, &buf, sizeof(buf), &timeout);
			fail("No TimeoutException thrown.");
		} catch (const TimeoutException &) {
			ensure_equals("Timeout is unchanged", timeout, 0u);
		}
	}

	TEST_METHOD(58) {
		set_test_name("readExact() deducts the amount of time spent on waiting from the timeout variable");
		Pipe p = createPipe(__FILE__, __LINE__);
		unsigned long long timeout = 500000;
		char buf[3];

		// Spawn a thread that writes 100 bytes per second, i.e. each byte takes 10 msec.
		TempThread thr(boost::bind(&writeDataSlowly, p.second, 1000, 100));

		// We read 3 bytes.
		ensure_equals(readExact(p.first, &buf, sizeof(buf), &timeout), 3u);
		ensure("Should have taken at least 25 msec", timeout <= 500000 - 25000);
		ensure("Should have taken at most 150 msec", timeout >= 500000 - 150000);
	}

	TEST_METHOD(59) {
		// readExact() does not wait and does not modify the timeout variable if there's
		// immediately enough data available.
		Pipe p = createPipe(__FILE__, __LINE__);
		unsigned long long timeout = 100000;
		char buf[2];

		write(p.second, "hi", 2);
		ensure_equals(readExact(p.first, &buf, 2, &timeout), 2u);
		ensure("Timeout not modified", timeout >= 95000);
	}

	/***** Test waitUntilWritable() *****/

	TEST_METHOD(60) {
		// waitUntilWritable() waits for the specified timeout if no data is writable.
		Pipe p = createNonBlockingPipe();
		writeUntilFull(p.second);
		unsigned long long timeout = 25000;
		ensure("Socket did not become writable", !waitUntilWritable(p.second, &timeout));
		ensure("The passed time is deducted from the timeout", timeout < 5000);
	}

	TEST_METHOD(61) {
		// waitUntilWritable() waits for less than the specified timeout if the fd
		// is not immediately writable but still writable before the timeout.
		Pipe p = createNonBlockingPipe();
		writeUntilFull(p.second);
		TempThread thr(boost::bind(&readDataAfterSomeTime, p.first, 35000));

		unsigned long long timeout = 1000000;
		ensure("Socket became writable", waitUntilWritable(p.second, &timeout));
		ensure("At least 35 msec passed.", timeout <= 1000000 - 35000);
		ensure("At most 250 msec passed.", timeout >= 1000000 - 250000);  // depends on system scheduler though
	}

	TEST_METHOD(62) {
		// waitUntilWritable() returns immediately if timeout is 0.
		Pipe p = createNonBlockingPipe();
		writeUntilFull(p.second);
		unsigned long long timeout = 0;
		ensure("Socket is not writable", !waitUntilWritable(p.second, &timeout));
		ensure_equals("Timeout is not modified", timeout, 0u);

		char buf[1024 * 8];
		read(p.first, buf, sizeof(buf));
		ensure("Socket became writable", waitUntilWritable(p.second, &timeout));
		ensure_equals("Timeout is not modified", timeout, 0u);
	}

	TEST_METHOD(63) {
		// waitUntilWritable() returns immediately if the fd is immediately writable.
		Pipe p = createNonBlockingPipe();
		writeUntilFull(p.second);
		unsigned long long timeout = 100000;
		char buf[1024 * 8];
		read(p.first, buf, sizeof(buf));
		ensure("Socket became writable", waitUntilWritable(p.second, &timeout));
		ensure("Timeout is not modified", timeout >= 100000 - 5000);
	}

	/***** Test readExact() *****/

	TEST_METHOD(64) {
		// writeExact() throws TimeoutException if fd does not become writable within the timeout.
		Pipe p = createNonBlockingPipe();
		writeUntilFull(p.second);
		unsigned long long timeout = 50000;
		try {
			writeExact(p.second, "x", 1, &timeout);
			fail("No TimeoutException thrown.");
		} catch (const TimeoutException &) {
			ensure("The passed time is deducted from timeout", timeout < 5000);
		}
	}

	TEST_METHOD(65) {
		// writeExact() throws TimeoutException if not enough data is written within the timeout.
		Pipe p = createNonBlockingPipe();
		writeUntilFull(p.second);
		unsigned long long timeout = 20000;
		char buf[1024 * 3];

		TempThread thr(boost::bind(&readDataSlowly, p.first, sizeof(buf), 512));

		try {
			writeExact(p.second, "x", 1, &timeout);
			fail("No TimeoutException thrown.");
		} catch (const TimeoutException &) {
			ensure("The passed time is deducted from timeout", timeout < 5000);
		}
	}

	TEST_METHOD(66) {
		// writeExact() throws TimeoutException if timeout is 0 and the fd is not immediately writable.
		Pipe p = createNonBlockingPipe();
		writeUntilFull(p.second);
		unsigned long long timeout = 0;
		try {
			writeExact(p.second, "x", 1, &timeout);
			fail("No TimeoutException thrown.");
		} catch (const TimeoutException &) {
			ensure_equals("Timeout unchanged", timeout, 0u);
		}
	}

	TEST_METHOD(67) {
		// writeExact() throws TimeoutException if timeout is 0 not enough data could be written immediately.
		Pipe p = createNonBlockingPipe();
		writeUntilFull(p.second);
		unsigned long long timeout = 0;

		char buf[1024];
		read(p.first, buf, sizeof(buf));

		char buf2[1024 * 8];
		memset(buf2, 0, sizeof(buf2));

		try {
			writeExact(p.second, buf2, sizeof(buf2), &timeout);
			fail("No TimeoutException thrown.");
		} catch (const TimeoutException &) {
			ensure_equals("Timeout is unchanged", timeout, 0u);
		}
	}

	TEST_METHOD(68) {
		// readExact() deducts the amount of time spent on waiting from the timeout variable.
		Pipe p = createNonBlockingPipe();
		unsigned long long timeout = 100000;

		// Spawn a thread that reads 200000 bytes in 35 msec.
		TempThread thr(boost::bind(&readDataSlowly, p.first, 5714286, 5714286));

		// We write 200000 bytes.
		char buf[200000];
		memset(buf, 0, sizeof(buf));
		writeExact(p.second, &buf, sizeof(buf), &timeout);
		ensure("Should have taken at least 20 msec", timeout <= 100000 - 20000);
		ensure("Should have taken at most 95 msec", timeout >= 100000 - 95000);
	}

	TEST_METHOD(69) {
		// writeExact() does not wait and does not modify the timeout variable if
		// all data can be written immediately.
		Pipe p = createNonBlockingPipe();
		unsigned long long timeout = 100000;
		char buf[1024];
		memset(buf, 0, sizeof(buf));
		writeExact(p.second, buf, sizeof(buf), &timeout);
		ensure("Timeout not modified", timeout >= 95000);
	}

	/***** Test getSocketAddressType() *****/

	TEST_METHOD(70) {
		ensure_equals(getSocketAddressType(""), SAT_UNKNOWN);
		ensure_equals(getSocketAddressType("/foo.socket"), SAT_UNKNOWN);
		ensure_equals(getSocketAddressType("unix:"), SAT_UNKNOWN);
		ensure_equals(getSocketAddressType("unix:/"), SAT_UNIX);
		ensure_equals(getSocketAddressType("unix:/foo.socket"), SAT_UNIX);
		ensure_equals(getSocketAddressType("tcp:"), SAT_UNKNOWN);
		ensure_equals(getSocketAddressType("tcp://"), SAT_UNKNOWN);
		// Doesn't check whether it contains port
		ensure_equals(getSocketAddressType("tcp://127.0.0.1"), SAT_TCP);
		ensure_equals(getSocketAddressType("tcp://127.0.0.1:80"), SAT_TCP);
	}

	TEST_METHOD(71) {
		ensure_equals(parseUnixSocketAddress("unix:/foo.socket"), "/foo.socket");
		try {
			parseUnixSocketAddress("unix:");
			fail("ArgumentException expected");
		} catch (const ArgumentException &e) {
			// Pass.
		}
	}

	TEST_METHOD(72) {
		string host;
		unsigned short port;

		parseTcpSocketAddress("tcp://127.0.0.1:80", host, port);
		ensure_equals(host, "127.0.0.1");
		ensure_equals(port, 80);

		parseTcpSocketAddress("tcp://[::1]:80", host, port);
		ensure_equals(host, "::1");
		ensure_equals(port, 80);

		try {
			parseTcpSocketAddress("tcp://", host, port);
			fail("ArgumentException expected (1)");
		} catch (const ArgumentException &e) {
			// Pass.
		}

		try {
			parseTcpSocketAddress("tcp://127.0.0.1", host, port);
			fail("ArgumentException expected (2)");
		} catch (const ArgumentException &e) {
			// Pass.
		}

		try {
			parseTcpSocketAddress("tcp://127.0.0.1:", host, port);
			fail("ArgumentException expected (3)");
		} catch (const ArgumentException &e) {
			// Pass.
		}

		try {
			parseTcpSocketAddress("tcp://[::1]", host, port);
			fail("ArgumentException expected (4)");
		} catch (const ArgumentException &e) {
			// Pass.
		}

		try {
			parseTcpSocketAddress("tcp://[::1]:", host, port);
			fail("ArgumentException expected (5)");
		} catch (const ArgumentException &e) {
			// Pass.
		}
	}

	/***** Test readFileDescriptor() and writeFileDescriptor() *****/

	TEST_METHOD(80) {
		// Test whether it works.
		SocketPair sockets = createUnixSocketPair(__FILE__, __LINE__);
		Pipe pipes = createPipe(__FILE__, __LINE__);
		writeFileDescriptor(sockets[0], pipes[1]);
		FileDescriptor fd(readFileDescriptor(sockets[1]), __FILE__, __LINE__);
		writeExact(fd, "hello");
		char buf[6];
		ensure_equals(readExact(pipes[0], buf, 5), 5u);
		buf[5] = '\0';
		ensure_equals(StaticString(buf), "hello");
	}

	TEST_METHOD(81) {
		// Test whether timeout works.
		SocketPair sockets = createUnixSocketPair(__FILE__, __LINE__);
		Pipe pipes = createPipe(__FILE__, __LINE__);

		unsigned long long timeout = 30000;
		unsigned long long startTime = SystemTime::getUsec();
		try {
			FileDescriptor fd(readFileDescriptor(sockets[0], &timeout),
				__FILE__, __LINE__);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure("readFileDescriptor() timed out after at least 29 msec",
				elapsed >= 29000);
			ensure("readFileDescriptor() timed out after at most 95 msec",
				elapsed <= 95000);
			ensure(timeout <= 2000);
		}

		writeUntilFull(sockets[0]);

		startTime = SystemTime::getUsec();
		timeout = 30000;
		try {
			writeFileDescriptor(sockets[0], pipes[0], &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = SystemTime::getUsec() - startTime;
			ensure("writeFileDescriptor() timed out after 30 msec",
				elapsed >= 29000 && elapsed <= 95000);
			ensure(timeout <= 2000);
		}
	}


	/***** Test readAll() *****/

	TEST_METHOD(85) {
		set_test_name("readAll() with unlimited maxSize");
		Pipe p = createPipe(__FILE__, __LINE__);
		writeExact(p[1], "hello world");
		p[1].close();
		pair<string, bool> result = readAll(p[0],
			std::numeric_limits<size_t>::max());
		ensure_equals(result.first, "hello world");
		ensure(result.second);
	}

	TEST_METHOD(86) {
		set_test_name("readAll() with size smaller than actual data");
		Pipe p = createPipe(__FILE__, __LINE__);
		writeExact(p[1], "hello world");
		p[1].close();
		pair<string, bool> result = readAll(p[0], 5);
		ensure_equals(result.first, "hello");
		ensure(!result.second);
	}
}
