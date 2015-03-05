#include <TestSupport.h>
#include <boost/thread.hpp>
#include <string>
#include <BackgroundEventLoop.h>
#include <Constants.h>
#include <Logging.h>
#include <StaticString.h>
#include <ServerKit/FileBufferedChannel.h>
#include <Utils/StrIntUtils.h>

using namespace Passenger;
using namespace Passenger::ServerKit;
using namespace Passenger::MemoryKit;
using namespace std;

namespace tut {
	#define CONSUME_FULLY -2

	struct ServerKit_FileBufferedChannelTest: public ServerKit::Hooks {
		BackgroundEventLoop bg;
		ServerKit::Context context;
		FileBufferedChannel channel;
		boost::mutex syncher;
		int toConsume;
		bool endConsume;
		unsigned int counter;
		unsigned int buffersFlushed;
		string log;

		ServerKit_FileBufferedChannelTest()
			: bg(false, true),
			  context(bg.safe),
			  channel(&context),
			  toConsume(CONSUME_FULLY),
			  endConsume(false),
			  counter(0),
			  buffersFlushed(0)
		{
			initializeLibeio();
			channel.setDataCallback(dataCallback);
			channel.setBuffersFlushedCallback(buffersFlushedCallback);
			channel.setHooks(this);
			Hooks::impl = NULL;
			Hooks::userData = NULL;
		}

		~ServerKit_FileBufferedChannelTest() {
			bg.stop(); // Prevent any runLater callbacks from running.
			channel.deinitialize(); // Cancel any event loop next tick callbacks.
			setLogLevel(DEFAULT_LOG_LEVEL);
			shutdownLibeio();
		}

		void startLoop() {
			bg.start();
		}

		static Channel::Result dataCallback(Channel *_channel, const mbuf &buffer, int errcode)
		{
			FileBufferedChannel *channel = reinterpret_cast<FileBufferedChannel *>(_channel);
			ServerKit_FileBufferedChannelTest *self = (ServerKit_FileBufferedChannelTest *)
				channel->getHooks();
			boost::lock_guard<boost::mutex> l(self->syncher);
			if (errcode == 0) {
				self->counter++;
				if (buffer.empty()) {
					self->log.append("EOF\n");
				} else {
					StaticString str(buffer.start, buffer.size());
					self->log.append("Data: " + cEscapeString(str) + "\n");
				}
			} else {
				self->log.append("Error: " + toString(errcode) + "\n");
			}
			if (self->toConsume == CONSUME_FULLY) {
				return Channel::Result(buffer.size(), self->endConsume);
			} else {
				return Channel::Result(self->toConsume, self->endConsume);
			}
		}

		static void buffersFlushedCallback(FileBufferedChannel *channel) {
			ServerKit_FileBufferedChannelTest *self = (ServerKit_FileBufferedChannelTest *)
				channel->getHooks();
			boost::lock_guard<boost::mutex> l(self->syncher);
			self->buffersFlushed++;
		}

		void feedChannel(const string &data) {
			bg.safe->runLater(boost::bind(&ServerKit_FileBufferedChannelTest::_feedChannel,
				this, data));
		}

		void _feedChannel(string data) {
			assert(data.size() < context.mbuf_pool.mbuf_block_chunk_size);
			mbuf buf = mbuf_get(&context.mbuf_pool);
			memcpy(buf.start, data.data(), data.size());
			buf = mbuf(buf, 0, (unsigned int) data.size());
			channel.feed(buf);
		}

		void feedChannelError(int errcode) {
			bg.safe->runLater(boost::bind(&ServerKit_FileBufferedChannelTest::_feedChannelError,
				this, errcode));
		}

		void _feedChannelError(int errcode) {
			channel.feedError(errcode);
		}

		void channelConsumed(int size, bool end) {
			bg.safe->runLater(boost::bind(&ServerKit_FileBufferedChannelTest::_channelConsumed,
				this, size, end));
		}

		void _channelConsumed(int size, bool end) {
			channel.consumed(size, end);
		}

		Channel::State getChannelState() {
			Channel::State result;
			bg.safe->runSync(boost::bind(&ServerKit_FileBufferedChannelTest::_getChannelState,
				this, &result));
			return result;
		}

		void _getChannelState(Channel::State *result) {
			*result = channel.getState();
		}

		FileBufferedChannel::Mode getChannelMode() {
			FileBufferedChannel::Mode result;
			bg.safe->runSync(boost::bind(&ServerKit_FileBufferedChannelTest::_getChannelMode,
				this, &result));
			return result;
		}

		void _getChannelMode(FileBufferedChannel::Mode *result) {
			*result = channel.getMode();
		}

		FileBufferedChannel::ReaderState getChannelReaderState() {
			FileBufferedChannel::ReaderState result;
			bg.safe->runSync(boost::bind(&ServerKit_FileBufferedChannelTest::_getChannelReaderState,
				this, &result));
			return result;
		}

		void _getChannelReaderState(FileBufferedChannel::ReaderState *result) {
			*result = channel.getReaderState();
		}

		FileBufferedChannel::WriterState getChannelWriterState() {
			FileBufferedChannel::WriterState result;
			bg.safe->runSync(boost::bind(&ServerKit_FileBufferedChannelTest::_getChannelWriterState,
				this, &result));
			return result;
		}

		void _getChannelWriterState(FileBufferedChannel::WriterState *result) {
			*result = channel.getWriterState();
		}

		unsigned int getChannelBytesBuffered() {
			unsigned int result;
			bg.safe->runSync(boost::bind(&ServerKit_FileBufferedChannelTest::_getChannelBytesBuffered,
				this, &result));
			return result;
		}

		void _getChannelBytesBuffered(unsigned int *result) {
			*result = channel.getBytesBuffered();
		}

		void channelEnableAutoStartMover(bool enabled) {
			bg.safe->runSync(boost::bind(&ServerKit_FileBufferedChannelTest::_channelEnableAutoStartMover,
				this, enabled));
		}

		void _channelEnableAutoStartMover(bool enabled) {
			context.defaultFileBufferedChannelConfig.autoStartMover = enabled;
		}

		void startChannel() {
			bg.safe->runLater(boost::bind(&ServerKit_FileBufferedChannelTest::_startChannel,
				this));
		}

		void _startChannel() {
			channel.start();
		}

		void setChannelDataCallback(FileBufferedChannel::DataCallback callback) {
			bg.safe->runLater(boost::bind(&ServerKit_FileBufferedChannelTest::_setChannelDataCallback,
				this, callback));
		}

		void _setChannelDataCallback(FileBufferedChannel::DataCallback callback) {
			channel.setDataCallback(callback);
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(ServerKit_FileBufferedChannelTest, 100);

	#define LOCK() boost::unique_lock<boost::mutex> l(syncher)
	#define UNLOCK() l.unlock()


	/***** Initial state *****/

	TEST_METHOD(1) {
		set_test_name("It is initially in the in-memory mode, and the reader is initially inactive");
		startLoop();
		ensure_equals(getChannelMode(), FileBufferedChannel::IN_MEMORY_MODE);
		ensure_equals(getChannelReaderState(), FileBufferedChannel::RS_INACTIVE);
	}


	/***** When in the in-memory mode *****/

	TEST_METHOD(5) {
		set_test_name("Upon feeding data, it calls the callback");

		startLoop();
		feedChannel("hello");
		EVENTUALLY(5,
			LOCK();
			result = log == "Data: hello\n";
		);
		EVENTUALLY(5,
			result = getChannelReaderState() == FileBufferedChannel::RS_INACTIVE;
		);
	}

	TEST_METHOD(6) {
		set_test_name("Upon feeding data, and the previous data callback isn't done "
			"consuming yet, it calls the callback with the new data "
			"after the previous data callback is done consuming");

		toConsume = -1;
		startLoop();

		feedChannel("hello");
		feedChannel("world");
		feedChannel("!");
		SHOULD_NEVER_HAPPEN(100,
			LOCK();
			result = log.find("world") != string::npos
				|| log.find("!") != string::npos;
		);

		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world\n";
		);
		channelConsumed(sizeof("world") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world\n"
				"Data: !\n";
		);

		channelConsumed(sizeof("!") - 1, false);
		EVENTUALLY(5,
			result = getChannelReaderState() == FileBufferedChannel::RS_INACTIVE;
		);
	}

	TEST_METHOD(7) {
		set_test_name("Upon feeding data, if the total amount of data is below the threshold, "
			"then it remains in the in-memory mode");

		toConsume = -1;
		startLoop();

		feedChannel("hello");
		SHOULD_NEVER_HAPPEN(100,
			result = getChannelMode() != FileBufferedChannel::IN_MEMORY_MODE;
		);
	}

	TEST_METHOD(9) {
		set_test_name("Upon feeding EOF, it calls the callback with an EOF");

		startLoop();
		feedChannel("");
		EVENTUALLY(5,
			LOCK();
			result = log == "EOF\n";
		);
	}

	TEST_METHOD(10) {
		set_test_name("Upon feeding EOF, the internal reader eventually switches to RS_TERMINATED");

		startLoop();
		feedChannel("");
		EVENTUALLY(5,
			result = getChannelReaderState() == FileBufferedChannel::RS_TERMINATED;
		);
	}

	TEST_METHOD(11) {
		set_test_name("Once EOF has been fed, any further data feds have no effect");

		startLoop();
		feedChannel("");
		feedChannel("hello");
		EVENTUALLY(5,
			LOCK();
			result = log == "EOF\n";
		);
		SHOULD_NEVER_HAPPEN(100,
			LOCK();
			result = log != "EOF\n";
		);
	}

	TEST_METHOD(12) {
		set_test_name("If the callback indicates that it is done consuming, the internal "
			"reader eventually switches to RS_TERMINATED");

		endConsume = true;
		startLoop();
		feedChannel("hello");
		EVENTUALLY(5,
			LOCK();
			result = log == "Data: hello\n";
		);
		EVENTUALLY(5,
			result = getChannelReaderState() == FileBufferedChannel::RS_TERMINATED;
		);
	}

	TEST_METHOD(13) {
		set_test_name("Once the callback has indicated that it is done consuming, any further data "
			"feds have no effect");

		endConsume = true;
		startLoop();

		feedChannel("hello");
		feedChannel("world");
		EVENTUALLY(5,
			LOCK();
			result = log == "Data: hello\n";
		);
		SHOULD_NEVER_HAPPEN(100,
			LOCK();
			result = log != "Data: hello\n";
		);
	}

	TEST_METHOD(14) {
		set_test_name("Upon feeding an error, it calls the callback with an error");

		startLoop();
		feedChannelError(EIO);
		EVENTUALLY(5,
			LOCK();
			result = log == "Error: " + toString(EIO) + "\n";
		);
	}

	TEST_METHOD(15) {
		set_test_name("Upon feeding an error, the internal reader eventually switches to RS_TERMINATED");

		startLoop();
		feedChannelError(EIO);
		EVENTUALLY(5,
			result = getChannelReaderState() == FileBufferedChannel::RS_TERMINATED;
		);
	}

	TEST_METHOD(16) {
		set_test_name("Once an error has been fed, any further data feds have no effect");

		startLoop();
		feedChannelError(EIO);
		feedChannel("hello");
		EVENTUALLY(5,
			LOCK();
			result = log == "Error: " + toString(EIO) + "\n";
		);
		SHOULD_NEVER_HAPPEN(100,
			LOCK();
			result = log != "Error: " + toString(EIO) + "\n";
		);
	}


	/***** When switching from in-memory mode to in-file mode *****/

	TEST_METHOD(20) {
		set_test_name("Upon feeding so much data that the threshold is passed, "
			"it switches to the in-file mode and calls the callback later with the fed data");

		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();

		feedChannel("hello");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);

		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log == "Data: hello\n";
		);
		EVENTUALLY(5,
			result = getChannelReaderState() == FileBufferedChannel::RS_INACTIVE;
		);
	}

	TEST_METHOD(21) {
		set_test_name("Any fed data is immediately passed to the callback");

		context.defaultFileBufferedChannelConfig.threshold = 1;
		context.defaultFileBufferedChannelConfig.delayInFileModeSwitching = 50000;
		context.defaultFileBufferedChannelConfig.autoTruncateFile = false;
		startLoop();

		feedChannel("hello");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		ensure_equals(getChannelWriterState(), FileBufferedChannel::WS_CREATING_FILE);
		EVENTUALLY(5,
			LOCK();
			result = log == "Data: hello\n";
		);
	}

	TEST_METHOD(22) {
		set_test_name("If the previous callback isn't done consuming, any fed data is "
			"buffered in memory, and passed to the callback when the previous callback "
			"is done");

		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		context.defaultFileBufferedChannelConfig.delayInFileModeSwitching = 50000;
		context.defaultFileBufferedChannelConfig.autoTruncateFile = false;
		startLoop();

		feedChannel("hello");
		feedChannel("world");
		feedChannel("!");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		ensure_equals(getChannelWriterState(), FileBufferedChannel::WS_CREATING_FILE);
		SHOULD_NEVER_HAPPEN(100,
			LOCK();
			result = log != "Data: hello\n";
		);
		ensure_equals(getChannelBytesBuffered(), sizeof("helloworld!") - 1);

		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);
		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world\n";
		);
		ensure_equals(getChannelBytesBuffered(), sizeof("helloworld!") - 1);

		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);
		channelConsumed(sizeof("world") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world\n"
				"Data: !\n";
		);
		ensure_equals(getChannelBytesBuffered(), sizeof("helloworld!") - 1);
	}


	/***** When in the in-file mode *****/

	TEST_METHOD(30) {
		set_test_name("It slowly moves memory buffers to disk");

		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();

		feedChannel("hello");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		EVENTUALLY(5,
			result = getChannelWriterState() == FileBufferedChannel::WS_INACTIVE;
		);
		ensure_equals(getChannelBytesBuffered(), 0u);
	}

	TEST_METHOD(31) {
		set_test_name("If all memory buffers have been moved to disk, then "
			"when new data is fed, the new data is also eventually moved to disk");

		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();

		feedChannel("hello");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		EVENTUALLY(5,
			result = getChannelWriterState() == FileBufferedChannel::WS_INACTIVE;
		);
		ensure_equals(getChannelBytesBuffered(), 0u);

		feedChannel("world");
		EVENTUALLY(5,
			result = getChannelBytesBuffered() == 0;
		);
		ensure_equals(getChannelWriterState(), FileBufferedChannel::WS_INACTIVE);
	}

	TEST_METHOD(32) {
		set_test_name("If there is unread data on disk, it reads them and passes "
			"them to the callback");

		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();

		feedChannel("hello");
		feedChannel("world!");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		EVENTUALLY(5,
			result = getChannelWriterState() == FileBufferedChannel::WS_INACTIVE;
		);
		ensure_equals(getChannelBytesBuffered(), 0u);

		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world!\n";
		);
	}

	TEST_METHOD(33) {
		set_test_name("Suppose that a data chunk from disk is being passed to the callback. "
			"If the callback consumes the chunk immediately and is willing to accept "
			"further data, then the FileBufferedChannel will repeat this process with the "
			"next chunk from disk");

		// Setup a FileBufferedChannel in the in-file mode.
		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();
		feedChannel("hello");
		feedChannel("world!");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		EVENTUALLY(5,
			result = getChannelWriterState() == FileBufferedChannel::WS_INACTIVE;
		);
		ensure_equals(getChannelBytesBuffered(), 0u);

		// Consume the initial "hello" so that the FileBufferedChannel starts
		// reading "world" from disk. When "world" is read, we first consume
		// "world" only, then "!" too.
		context.defaultFileBufferedChannelConfig.maxDiskChunkReadSize = sizeof("world") - 1;
		toConsume = CONSUME_FULLY;
		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world\n"
				"Data: !\n";
		);
	}

	TEST_METHOD(34) {
		set_test_name("Suppose that a data chunk from disk is being passed to the callback. "
			"If the callback consumes the chunk asynchronously, and is willing "
			"to accept further data, then the FileBufferedChannel will repeat this process "
			"with the next chunk from disk after the channel has become idle");

		// Setup a FileBufferedChannel in the in-file mode.
		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();
		feedChannel("hello");
		feedChannel("world!");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		EVENTUALLY(5,
			result = getChannelWriterState() == FileBufferedChannel::WS_INACTIVE;
		);
		ensure_equals(getChannelBytesBuffered(), 0u);

		// Consume the initial "hello" so that the FileBufferedChannel starts
		// reading "world" from disk.
		context.defaultFileBufferedChannelConfig.maxDiskChunkReadSize = sizeof("world") - 1;
		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n";
		);
		// We haven't consumed "world" yet, so the FileBufferedChannel should
		// be waiting for it to become idle.
		EVENTUALLY(5,
			result = getChannelReaderState() == FileBufferedChannel::RS_WAITING_FOR_CHANNEL_IDLE;
		);

		// Now consume "world".
		channelConsumed(sizeof("world") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world\n";
		);
		// We haven't consumed "!" yet, so the FileBufferedChannel should
		// be waiting for it to become idle.
		EVENTUALLY(5,
			result = getChannelReaderState() == FileBufferedChannel::RS_WAITING_FOR_CHANNEL_IDLE;
		);

		// Now consume "!".
		channelConsumed(sizeof("!") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world\n"
				"Data: !\n";
		);
	}

	TEST_METHOD(35) {
		set_test_name("Suppose that a data chunk from disk is being passed to the callback. "
			"If the callback consumes the chunk immediately, but is not willing "
			"to accept further data, then the FileBufferedChannel will terminate");

		// Setup a FileBufferedChannel in the in-file mode.
		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();
		feedChannel("hello");
		feedChannel("world!");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		EVENTUALLY(5,
			result = getChannelWriterState() == FileBufferedChannel::WS_INACTIVE;
		);
		ensure_equals(getChannelBytesBuffered(), 0u);

		// Consume the initial "hello" so that the FileBufferedChannel starts
		// reading "world" from disk. When it is read, we will consume it fully
		// while ending the channel.
		context.defaultFileBufferedChannelConfig.maxDiskChunkReadSize = sizeof("world") - 1;
		toConsume = CONSUME_FULLY;
		endConsume = true;
		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world\n";
		);
		EVENTUALLY(5,
			result = getChannelReaderState() == FileBufferedChannel::RS_TERMINATED;
		);
		SHOULD_NEVER_HAPPEN(100,
			LOCK();
			result = log !=
				"Data: hello\n"
				"Data: world\n";
		);
	}

	TEST_METHOD(36) {
		set_test_name("If there is no unread data on disk, it passes the next "
			"in-memory buffer to the callback");

		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();

		feedChannel("hello");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		EVENTUALLY(5,
			result = getChannelWriterState() == FileBufferedChannel::WS_INACTIVE;
		);
		ensure_equals(getChannelBytesBuffered(), 0u);

		channelEnableAutoStartMover(false);
		feedChannel("world!");
		feedChannel("the end");
		ensure_equals(getChannelBytesBuffered(), sizeof("world!the end") - 1);
		ensure_equals("channelEnableAutoStartMover works",
			getChannelWriterState(), FileBufferedChannel::WS_INACTIVE);

		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = counter == 2
				&& getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		channelConsumed(sizeof("world!") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = counter == 3
				&& getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world!\n"
				"Data: the end\n";
		);
	}

	TEST_METHOD(37) {
		set_test_name("Upon feeding EOF, the EOF is passed to the callback after "
			"all on-disk and in-memory data is passed");

		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();

		feedChannel("hello");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		EVENTUALLY(5,
			result = getChannelWriterState() == FileBufferedChannel::WS_INACTIVE;
		);
		ensure_equals(getChannelBytesBuffered(), 0u);

		channelEnableAutoStartMover(false);
		feedChannel("world!");
		feedChannel("the end");
		feedChannel("");
		ensure_equals(getChannelBytesBuffered(), sizeof("world!the end") - 1);
		ensure_equals("channelDisableAutoStartMover works",
			getChannelWriterState(), FileBufferedChannel::WS_INACTIVE);

		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = counter == 2
				&& getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		channelConsumed(sizeof("world!") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = counter == 3
				&& getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);
		SHOULD_NEVER_HAPPEN(100,
			LOCK();
			result = log !=
				"Data: hello\n"
				"Data: world!\n"
				"Data: the end\n";
		);

		channelConsumed(sizeof("the end") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = counter == 4
				&& getChannelState() == Channel::EOF_WAITING;
		);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world!\n"
				"Data: the end\n"
				"EOF\n";
		);
	}

	TEST_METHOD(38) {
		set_test_name("Upon feeding an error, it switches to the error mode immediately "
			"and it doesn't call the callback");

		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();

		feedChannel("hello");
		feedChannel("world");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		EVENTUALLY(5,
			result = getChannelWriterState() == FileBufferedChannel::WS_INACTIVE;
		);
		ensure_equals(getChannelBytesBuffered(), 0u);

		feedChannelError(EIO);
		EVENTUALLY(5,
			result = getChannelReaderState() == FileBufferedChannel::RS_TERMINATED;
		);
		ensure_equals(getChannelMode(), FileBufferedChannel::ERROR_WAITING);

		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Error: " + toString(EIO) + "\n";
		);
	}


	/***** Switching from in-file mode to in-memory mode *****/

	TEST_METHOD(40) {
		set_test_name("When all on-disk and in-memory buffers have been read, it switches to in-memory mode");

		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		startLoop();

		feedChannel("hello");
		feedChannel("world!");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		EVENTUALLY(5,
			result = getChannelWriterState() == FileBufferedChannel::WS_INACTIVE;
		);
		ensure_equals(getChannelBytesBuffered(), 0u);

		channelConsumed(sizeof("hello") - 1, false);
		EVENTUALLY(5,
			LOCK();
			result = counter == 2
				&& getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		channelConsumed(sizeof("world!") - 1, false);
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_MEMORY_MODE;
		);

		{
			LOCK();
			toConsume = CONSUME_FULLY;
		}
		feedChannel("!");
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world!\n"
				"Data: !\n";
		);
	}

	TEST_METHOD(41) {
		set_test_name("It calls the buffersFlushedCallback if the switching happens while "
			"there are buffers in memory that haven't been written to disk yet");

		toConsume = -1;
		context.defaultFileBufferedChannelConfig.threshold = 1;
		context.defaultFileBufferedChannelConfig.delayInFileModeSwitching = 1000;
		startLoop();

		feedChannel("hello");
		feedChannel("world!");
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_FILE_MODE;
		);
		ensure_equals(getChannelBytesBuffered(), 11u);

		channelConsumed(sizeof("hello") - 1, false);
		channelConsumed(sizeof("world!") - 1, false);
		EVENTUALLY(5,
			result = getChannelMode() == FileBufferedChannel::IN_MEMORY_MODE;
		);
		EVENTUALLY(5,
			LOCK();
			result = buffersFlushed == 1;
		);
	}


	/***** When stopped *****/

	TEST_METHOD(45) {
		set_test_name("Upon feeding data, it calls the callback when start() is called");

		channel.stop();
		startLoop();

		feedChannel("hello");
		ensure_equals(getChannelBytesBuffered(), 5u);
		feedChannel("world");
		ensure_equals(getChannelBytesBuffered(), 10u);
		ensure_equals(getChannelReaderState(),
			FileBufferedChannel::RS_WAITING_FOR_CHANNEL_IDLE);
		SHOULD_NEVER_HAPPEN(100,
			result = getChannelBytesBuffered() != 10;
		);
		SHOULD_NEVER_HAPPEN(100,
			LOCK();
			result = !log.empty();
		);

		startChannel();
		EVENTUALLY(5,
			result = getChannelBytesBuffered() == 0;
		);
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: hello\n"
				"Data: world\n";
		);
	}

	static Channel::Result test_46_callback(Channel *_channel, const mbuf &buffer,
		int errcode)
	{
		FileBufferedChannel *channel = reinterpret_cast<FileBufferedChannel *>(_channel);
		ServerKit_FileBufferedChannelTest *self = (ServerKit_FileBufferedChannelTest *)
			channel->getHooks();
		boost::mutex &syncher = self->syncher;

		{
			LOCK();
			self->counter++;
		}
		channel->stop();
		return Channel::Result(buffer.size(), false);
	}

	TEST_METHOD(46) {
		set_test_name("If stop() is called in the callback, it doesn't call the "
			"callback with remaining buffers until start() is called");

		channel.setDataCallback(test_46_callback);
		startLoop();
		feedChannel("hello");
		feedChannel("world");
		EVENTUALLY(5,
			result = getChannelBytesBuffered() == 5;
		);
		{
			LOCK();
			ensure_equals(counter, 1u);
		}

		setChannelDataCallback(dataCallback);
		startChannel();
		EVENTUALLY(5,
			result = getChannelBytesBuffered() == 0;
		);
		{
			LOCK();
			ensure_equals(counter, 2u);
		}
	}
}
