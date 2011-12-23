#include "TestSupport.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <agents/HelperAgent/FileBackedPipe.h>
#include <algorithm>
#include <pthread.h>

using namespace Passenger;
using namespace std;
using namespace boost;

namespace tut {
	struct FileBackedPipeTest {
		TempDir tmpdir;
		BackgroundEventLoop bg;
		FileBackedPipePtr pipe;

		bool consumeImmediately;
		size_t toConsume;
		bool doneAfterConsuming;
		pthread_t consumeCallbackThread;
		string receivedData;
		FileBackedPipe::ConsumeCallback consumedCallback;

		FileBackedPipeTest()
			: tmpdir("tmp.pipe")
		{
			consumeImmediately = true;
			toConsume = 9999;
			doneAfterConsuming = false;
			pipe = make_shared<FileBackedPipe>(bg.safe, "tmp.pipe");
		}
		
		~FileBackedPipeTest() {
			bg.stop();
		}

		void init() {
			pipe->onData = boost::bind(&FileBackedPipeTest::onData,
				this, _1, _2, _3);
			bg.start();
		}

		bool write(const StaticString &data) {
			bool result;
			bg.safe->run(boost::bind(&FileBackedPipeTest::real_write, this, data, &result));
			return result;
		}

		void real_write(StaticString data, bool *result) {
			*result = pipe->write(data.data(), data.size());
		}

		unsigned int getBufferSize() {
			unsigned int result;
			bg.safe->run(boost::bind(&FileBackedPipeTest::real_getBufferSize, this, &result));
			return result;
		}

		void real_getBufferSize(unsigned int *result) {
			*result = pipe->getBufferSize();
		}

		void startPipe() {
			bg.safe->run(boost::bind(&FileBackedPipe::start, pipe.get()));
		}

		void callConsumedCallback(size_t consumed, bool done) {
			bg.safe->run(boost::bind(consumedCallback, consumed, done));
		}

		bool isStarted() {
			bool result;
			bg.safe->run(boost::bind(&FileBackedPipeTest::real_isStarted, this, &result));
			return result;
		}

		void real_isStarted(bool *result) {
			*result = pipe->isStarted();
		}

		void onData(const char *data, size_t size, const FileBackedPipe::ConsumeCallback &consumed) {
			consumeCallbackThread = pthread_self();
			receivedData.append(data, size);
			if (consumeImmediately) {
				consumed(std::min(toConsume, size), doneAfterConsuming);
			} else {
				consumedCallback = consumed;
			}
		}
	};

	DEFINE_TEST_GROUP(FileBackedPipeTest);

	TEST_METHOD(1) {
		// Test writing to an empty, started pipe and consuming all data immediately.
		init();
		startPipe();
		ensure("immediately consumed", write("hello"));
		ensure("callback called from event loop thread",
			pthread_equal(consumeCallbackThread, bg.safe->getCurrentThread()));
		ensure_equals(receivedData, "hello");
		ensure_equals("nothing buffered", getBufferSize(), 0u);
	}

	TEST_METHOD(2) {
		// Test writing to an empty, started pipe and not consuming immediately.
		init();
		startPipe();
		consumeImmediately = false;
		ensure("not immediately consumed", !write("hello"));
		ensure_equals(receivedData, "hello");
		ensure_equals("everything buffered", getBufferSize(), sizeof("hello") - 1);

		receivedData.clear();
		callConsumedCallback(5, false);
		ensure_equals(getBufferSize(), 0u);
	}

	TEST_METHOD(3) {
		// When the consume callback is called with done=false, the pipe should be paused.
		init();
		startPipe();
		doneAfterConsuming = true;
		write("hello");
		ensure(!isStarted());
	}

	TEST_METHOD(4) {
		// After consuming some data, if the pipe is still in started mode then
		// it should emit any remaining data.
	}
}
