#include "TestSupport.h"
#include "Utils/IOUtils.h"
#include <sys/types.h>
#include <cerrno>
#include <string>

using namespace Passenger;
using namespace std;

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
	
	struct IOUtilsTest {
		string restBuffer;
		
		IOUtilsTest() {
			writevResult = 0;
			writevErrno = 0;
			writevCalled = 0;
			writevData.clear();
			setWritevFunction(writev_mock);
		}
		
		~IOUtilsTest() {
			setWritevFunction(NULL);
		}
	};
	
	DEFINE_TEST_GROUP(IOUtilsTest);
	
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
}
