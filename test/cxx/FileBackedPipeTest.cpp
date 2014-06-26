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
		bool resetOnData;
		pthread_t consumeCallbackThread;
		AtomicInt consumeCallbackCount;
		string receivedData;
		bool ended;
		FileBackedPipe::ConsumeCallback consumedCallback;
		AtomicInt commitCount;

		FileBackedPipeTest()
			: tmpdir("tmp.pipe", true) // Removing the directory may not work over NFS
		{
			consumeImmediately = true;
			toConsume = 9999;
			doneAfterConsuming = false;
			resetOnData = false;
			consumeCallbackCount = 0;
			ended = false;
			pipe = boost::make_shared<FileBackedPipe>("tmp.pipe");
			pipe->userData = this;
			pipe->onData = onData;
			pipe->onEnd = onEnd;
			pipe->onCommit = onCommit;
		}
		
		~FileBackedPipeTest() {
			bg.stop();
			pipe.reset();
		}

		void init() {
			pipe->reset(bg.safe);
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

		void stopPipe() {
			bg.safe->run(boost::bind(&FileBackedPipe::stop, pipe.get()));
		}

		void endPipe() {
			bg.safe->run(boost::bind(&FileBackedPipe::end, pipe.get()));
		}

		void callConsumedCallback(size_t consumed, bool done) {
			bg.safe->run(boost::bind(consumedCallback.toFunction(), consumed, done));
		}

		bool isStarted() {
			bool result;
			bg.safe->run(boost::bind(&FileBackedPipeTest::real_isStarted, this, &result));
			return result;
		}

		void real_isStarted(bool *result) {
			*result = pipe->isStarted();
		}

		bool reachedEnd() {
			bool result;
			bg.safe->run(boost::bind(&FileBackedPipeTest::real_reachedEnd, this, &result));
			return result;
		}

		void real_reachedEnd(bool *result) {
			*result = pipe->reachedEnd();
		}

		bool isCommittingToDisk() {
			bool result;
			bg.safe->run(boost::bind(&FileBackedPipeTest::real_isCommittingToDisk, this, &result));
			return result;
		}

		void real_isCommittingToDisk(bool *result) {
			*result = pipe->isCommittingToDisk();
		}

		FileBackedPipe::DataState getDataState() {
			FileBackedPipe::DataState result;
			bg.safe->run(boost::bind(&FileBackedPipeTest::real_getDataState, this, &result));
			return result;
		}

		void real_getDataState(FileBackedPipe::DataState *result) {
			*result = pipe->getDataState();
		}

		static void onData(const FileBackedPipePtr &source, const char *data,
			size_t size, const FileBackedPipe::ConsumeCallback &consumed)
		{
			FileBackedPipeTest *self = (FileBackedPipeTest *) source->userData;
			self->consumeCallbackThread = pthread_self();
			if (!self->receivedData.empty()) {
				self->receivedData.append("\n");
			}
			self->receivedData.append(data, size);
			self->consumeCallbackCount++;
			if (self->resetOnData) {
				source->reset();
			}
			if (self->consumeImmediately) {
				consumed(std::min(self->toConsume, size), self->doneAfterConsuming);
			} else {
				self->consumedCallback = consumed;
			}
		}

		static void onEnd(const FileBackedPipePtr &source) {
			FileBackedPipeTest *self = (FileBackedPipeTest *) source->userData;
			self->ended = true;
		}

		static void onCommit(const FileBackedPipePtr &source) {
			FileBackedPipeTest *self = (FileBackedPipeTest *) source->userData;
			self->commitCount++;
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
		ensure("not committing to disk", !isCommittingToDisk());
	}

	TEST_METHOD(2) {
		// Test writing to an empty, started pipe and not consuming immediately.
		init();
		startPipe();
		consumeImmediately = false;
		write("hello");
		ensure_equals(receivedData, "hello");
		ensure_equals("everything buffered", getBufferSize(), sizeof("hello") - 1);

		receivedData.clear();
		callConsumedCallback(5, false);
		ensure_equals(getBufferSize(), 0u);
		ensure("not committing to disk", !isCommittingToDisk());
	}

	TEST_METHOD(3) {
		// Test writing to an empty, stopped pipe and starting it later.
		init();
		write("hello");
		startPipe();
		ensure_equals(consumeCallbackCount, 1);
		ensure_equals(receivedData, "hello");
		ensure_equals(getBufferSize(), 0u);
		ensure("not committing to disk", !isCommittingToDisk());
	}

	TEST_METHOD(4) {
		// When the consume callback is called with done=false, the pipe should be paused.
		init();
		startPipe();
		doneAfterConsuming = true;
		write("hello");
		ensure(!isStarted());
		ensure_equals(getBufferSize(), 0u);
		ensure("not committing to disk", !isCommittingToDisk());
	}

	TEST_METHOD(5) {
		// After consuming some data, if the pipe is still in started mode then
		// it should emit any remaining data.
		init();
		startPipe();
		toConsume = 3;
		write("hello");
		ensure_equals(getBufferSize(), 0u);
		ensure("not committing to disk", !isCommittingToDisk());
		ensure_equals(receivedData,
			"hello\n"
			"lo");
		ensure_equals(consumeCallbackCount, 2);
	}

	TEST_METHOD(6) {
		// Writing to a stopped pipe will cause the data to be buffered.
		// This buffer will be passed to the data callback when we
		// start the pipe again. If the data callback doesn't consume
		// everything at once then the pipe will try again until
		// everything's consumed.
		init();
		toConsume = 3;
		write("hello");
		ensure_equals(getBufferSize(), 5u);
		ensure("not committing to disk", !isCommittingToDisk());
		ensure_equals(receivedData, "");
		ensure_equals(consumeCallbackCount, 0);
		startPipe();
		ensure_equals(getBufferSize(), 0u);
		ensure("not committing to disk", !isCommittingToDisk());
		ensure_equals(consumeCallbackCount, 2);
		ensure_equals(receivedData,
			"hello\n"
			"lo");
	}

	TEST_METHOD(7) {
		// Test writing to a pipe whose consume callback hasn't
		// been called yet and whose data state is IN_MEMORY.
		init();
		startPipe();
		consumeImmediately = false;
		write("hello");

		write("world");
		ensure_equals(getDataState(), FileBackedPipe::IN_MEMORY);
		ensure_equals(getBufferSize(), 10u);
		ensure("not committing to disk", !isCommittingToDisk());
		ensure_equals(consumeCallbackCount, 1);
		ensure_equals(receivedData, "hello");

		callConsumedCallback(4, false);
		ensure_equals(getBufferSize(), 6u);
		ensure("not committing to disk", !isCommittingToDisk());
		ensure_equals(consumeCallbackCount, 2);
		ensure_equals(receivedData,
			"hello\n"
			"oworld");
		
		callConsumedCallback(6, false);
		ensure_equals(getBufferSize(), 0u);
		ensure("not committing to disk", !isCommittingToDisk());
		ensure_equals(consumeCallbackCount, 2);
		ensure_equals(receivedData,
			"hello\n"
			"oworld");
	}

	TEST_METHOD(8) {
		// Test writing to a pipe whose consume callback hasn't
		// been called yet and whose data state is OPENING_FILE.
		pipe->setThreshold(3);
		pipe->openTimeout = 30;
		init();
		startPipe();
		consumeImmediately = false;
		write("hello");

		write("world");
		ensure_equals("(1)", getDataState(), FileBackedPipe::OPENING_FILE);
		ensure_equals("(2)", getBufferSize(), 10u);
		ensure("committing to disk", isCommittingToDisk());
		ensure_equals("(3)", consumeCallbackCount, 1);
		ensure_equals("(4)", receivedData, "hello");

		callConsumedCallback(4, false);
		ensure_equals("(5)", getDataState(), FileBackedPipe::OPENING_FILE);
		ensure_equals("(6)", getBufferSize(), 6u);
		ensure_equals("(7)", consumeCallbackCount, 2);
		ensure_equals("(8)", receivedData,
			"hello\n"
			"oworld");
		
		callConsumedCallback(6, false);
		ensure_equals("(9)", getDataState(), FileBackedPipe::OPENING_FILE);
		ensure_equals("(10)", getBufferSize(), 0u);
		ensure_equals("(11)", consumeCallbackCount, 2);
		ensure_equals("(12)", receivedData,
			"hello\n"
			"oworld");
	}

	TEST_METHOD(9) {
		// Test writing to a pipe whose consume callback hasn't
		// been called yet and whose data state is IN_FILE.
		pipe->setThreshold(3);
		init();
		startPipe();
		consumeImmediately = false;
		write("hello");

		write("world");
		EVENTUALLY(5,
			result = getDataState() == FileBackedPipe::IN_FILE && consumeCallbackCount == 1;
		);
		ensure_equals("(2)", getBufferSize(), 10u);
		ensure_equals("(3)", receivedData, "hello");

		callConsumedCallback(4, false);
		EVENTUALLY(5,
			result = consumeCallbackCount == 2;
		);
		ensure_equals("(4)", getDataState(), FileBackedPipe::IN_FILE);
		ensure_equals("(5)", getBufferSize(), 6u);
		ensure_equals("(7)", receivedData,
			"hello\n"
			"oworld");
		
		callConsumedCallback(6, false);
		ensure_equals("(8)", getDataState(), FileBackedPipe::IN_FILE);
		ensure_equals("(9)", getBufferSize(), 0u);
		ensure_equals("(10)", consumeCallbackCount, 2);
		ensure_equals("(11)", receivedData,
			"hello\n"
			"oworld");
	}

	TEST_METHOD(10) {
		// When the data doesn't fit in the memory buffer it will
		// write to a file. Test whether writing to the file and
		// reading from the file works correctly.
		pipe->setThreshold(5);
		init();
		write("hello");
		ensure_equals(getBufferSize(), 5u);
		ensure_equals(getDataState(), FileBackedPipe::IN_MEMORY);
		write("world");
		ensure_equals(getBufferSize(), 10u);
		EVENTUALLY(5,
			result = getBufferSize() == 10 && getDataState() == FileBackedPipe::IN_FILE;
		);
		startPipe();
		EVENTUALLY(5,
			result = getBufferSize() == 0 && receivedData == "helloworld";
		);
	}

	TEST_METHOD(11) {
		// Test end() on a started, empty pipe.
		init();
		startPipe();
		endPipe();
		ensure_equals(consumeCallbackCount, 0);
		ensure(ended);
	}

	TEST_METHOD(12) {
		// Test end() on a started pipe after writing data to
		// it that's immediately consumed.
		init();
		startPipe();
		write("hello");
		endPipe();
		ensure_equals(consumeCallbackCount, 1);
		ensure_equals(receivedData, "hello");
		ensure(ended);
	}

	TEST_METHOD(13) {
		// Test end() on a started pipe that has data buffered in memory.
		init();
		consumeImmediately = false;
		startPipe();
		write("hello");
		endPipe();
		ensure_equals(getDataState(), FileBackedPipe::IN_MEMORY);
		ensure(!ended);

		callConsumedCallback(3, false);
		ensure_equals(receivedData,
			"hello\n"
			"lo");
		ensure(!ended);
		callConsumedCallback(2, false);
		ensure(ended);
	}

	TEST_METHOD(14) {
		// Test end() on a started pipe that has data buffered on disk.
		pipe->setThreshold(1);
		consumeImmediately = false;
		init();
		startPipe();
		write("hello");
		endPipe();
		EVENTUALLY(5,
			result = getDataState() == FileBackedPipe::IN_FILE && !ended;
		);

		callConsumedCallback(3, false);
		EVENTUALLY(5,
			result =
				receivedData ==
					"hello\n"
					"lo"
				&& !ended;
		);

		callConsumedCallback(2, false);
		ensure(ended);
	}

	TEST_METHOD(15) {
		// Test end() on an empty, stopped pipe.
		init();
		endPipe();
		startPipe();
		ensure_equals(consumeCallbackCount, 0);
		ensure_equals(receivedData, "");
		ensure(ended);
	}

	TEST_METHOD(16) {
		// Test end() on a non-empty, stopped pipe with dataState == IN_MEMORY.
		init();
		write("hello");
		endPipe();
		startPipe();
		EVENTUALLY(5,
			result = consumeCallbackCount == 1;
		);
		ensure_equals(receivedData, "hello");
		ensure(ended);
	}

	TEST_METHOD(17) {
		// Test end() on a non-empty, stopped pipe with dataState == IN_FILE.
		pipe->setThreshold(3);
		pipe->openTimeout = 30;
		init();
		write("hello");
		ensure_equals(getDataState(), FileBackedPipe::OPENING_FILE);
		endPipe();
		startPipe();
		EVENTUALLY(5,
			result = consumeCallbackCount == 1;
		);
		ensure_equals(getDataState(), FileBackedPipe::OPENING_FILE);
		ensure_equals(receivedData, "hello");
		ensure(ended);
	}

	TEST_METHOD(18) {
		// Test end() on a non-empty, stopped pipe with dataState == IN_FILE.
		pipe->setThreshold(3);
		init();
		write("hello");
		endPipe();
		startPipe();
		EVENTUALLY(5,
			result = getDataState() == FileBackedPipe::IN_FILE;
		);
		EVENTUALLY(5,
			result = consumeCallbackCount == 1;
		);
		ensure_equals(receivedData, "hello");
		ensure(ended);
	}

	TEST_METHOD(20) {
		// Starting a pipe whose end has already been processed will have no effect.
		init();
		startPipe();
		write("hello");
		endPipe();
		ensure_equals(consumeCallbackCount, 1);
		ensure(ended);
		
		stopPipe();
		ensure(reachedEnd());
		ensure(!isStarted());

		startPipe();
		ensure_equals(consumeCallbackCount, 1);
		ensure(ended);
		ensure(reachedEnd());
		ensure(!isStarted());
	}

	TEST_METHOD(21) {
		// If the written data is immediately consumed, then write() returns true
		// and the commit callback is never called.
		init();
		startPipe();
		ensure(write("hello"));
		SHOULD_NEVER_HAPPEN(40,
			result = commitCount > 0;
		);
	}

	TEST_METHOD(22) {
		// If the written data is not immediately consumed but fits
		// into the memory buffer, then write() returns true and the
		// commit callback is never called.
		consumeImmediately = false;
		init();
		startPipe();
		ensure(write("hello"));
		SHOULD_NEVER_HAPPEN(40,
			result = commitCount > 0;
		);
	}
	
	TEST_METHOD(23) {
		// If the pipe is paused and the written data fits into the memory
		// buffer, then write() returns true and the commit callback is never called.
		init();
		ensure(write("hello"));
		SHOULD_NEVER_HAPPEN(40,
			result = commitCount > 0;
		);
	}

	TEST_METHOD(24) {
		// If the written data is not immediately consumed and must be written
		// to the disk, then write() returns false. onCommit is called after
		// the data has been written out to the disk.
		pipe->setThreshold(3);
		pipe->openTimeout = 20;
		init();
		ensure(!write("hello"));
		ensure("committing to disk", isCommittingToDisk());
		EVENTUALLY(1,
			result = commitCount == 1;
		);
		ensure("not committing to disk", !isCommittingToDisk());
	}

	TEST_METHOD(25) {
		// It may be reset inside the onData callback.
		resetOnData = true;
		consumeImmediately = false;
		init();
		startPipe();
		write("hello");
		ensure(!isStarted());
		ensure_equals(getBufferSize(), 0u);
	}

	TEST_METHOD(26) {
		// It may be reset inside the onData callback while there is data buffered in memory.
		consumeImmediately = false;
		init();
		startPipe();
		write("hello");
		ensure_equals("(1)", getBufferSize(), 5u);
		resetOnData = true;
		callConsumedCallback(1, false);
		ensure("(2)", !isStarted());
		ensure_equals("(3)", getBufferSize(), 0u);
	}

	TEST_METHOD(27) {
		// It may be reset inside the onData callback while there is data buffered in memory,
		// soon to be written on disk.
		pipe->setThreshold(3);
		pipe->openTimeout = 40;
		consumeImmediately = false;
		init();
		startPipe();
		write("hello");
		ensure_equals("(1)", getBufferSize(), 5u);
		ensure("(2)", isCommittingToDisk());
		usleep(20000);
		ensure("(3)", isCommittingToDisk());
		resetOnData = true;
		callConsumedCallback(1, false);
		ensure("(4)", !isStarted());
		ensure_equals("(5)", getBufferSize(), 0u);
	}

	TEST_METHOD(28) {
		// It may be reset inside the onData callback while there is data buffered on disk.
		pipe->setThreshold(3);
		consumeImmediately = false;
		init();
		startPipe();
		write("hello");
		ensure_equals("(1)", getBufferSize(), 5u);
		usleep(20000);
		ensure("(2)", !isCommittingToDisk());

		resetOnData = true;
		// The following call will trigger a libeio read operation on the buffer file.
		callConsumedCallback(1, false);

		EVENTUALLY(1,
			result = !isStarted();
		);
		ensure("(3)", !isStarted());
		ensure_equals("(4)", getBufferSize(), 0u);
	}
}
