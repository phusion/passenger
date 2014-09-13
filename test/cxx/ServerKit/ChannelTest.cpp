#include <TestSupport.h>
#include <BackgroundEventLoop.h>
#include <ServerKit/Channel.h>
#include <Constants.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>

using namespace Passenger;
using namespace Passenger::ServerKit;
using namespace Passenger::MemoryKit;
using namespace std;

namespace tut {
	#define CONSUME_FULLY -2

	struct ServerKit_ChannelTest: public ServerKit::Hooks {
		BackgroundEventLoop bg;
		ServerKit::Context context;
		Channel channel;
		boost::mutex syncher;
		string log;
		int toConsume;
		bool endConsume;
		unsigned int counter, idleCount, endAcked, bytesConsumed;
		Channel::State lastState;

		ServerKit_ChannelTest()
			: bg(),
			  context(bg.safe),
			  channel(&context)
		{
			channel.dataCallback = dataCallback;
			channel.consumedCallback = consumedCallback;
			channel.hooks = this;
			Hooks::impl = NULL;
			Hooks::userData = NULL;
			toConsume = CONSUME_FULLY;
			endConsume = false;
			counter = 0;
			idleCount = 0;
			endAcked = 0;
			bytesConsumed = 0;
			lastState = Channel::IDLE;
			bg.start();
		}

		~ServerKit_ChannelTest() {
			channel.deinitialize(); // Cancel any event loop next tick callbacks.
			setLogLevel(DEFAULT_LOG_LEVEL);
		}

		static Channel::Result dataCallback(Channel *channel, const mbuf &buffer, int errcode) {
			ServerKit_ChannelTest *self = (ServerKit_ChannelTest *) channel->hooks;
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

		static void consumedCallback(Channel *channel, unsigned int size) {
			ServerKit_ChannelTest *self = (ServerKit_ChannelTest *) channel->hooks;
			boost::lock_guard<boost::mutex> l(self->syncher);
			self->bytesConsumed += size;
			if (channel->isIdle()) {
				self->idleCount++;
			} else if (channel->endAcked()) {
				self->endAcked++;
			}
		}

		unsigned int getCounter() {
			boost::lock_guard<boost::mutex> l(syncher);
			return counter;
		}

		void startChannel() {
			bg.safe->runLater(boost::bind(&ServerKit_ChannelTest::realStartChannel, this));
		}

		void realStartChannel() {
			channel.start();
		}

		void stopChannel() {
			bg.safe->runLater(boost::bind(&ServerKit_ChannelTest::realStopChannel, this));
		}

		void realStopChannel() {
			channel.stop();
		}

		bool channelIsStarted() {
			bool result;
			bg.safe->runSync(boost::bind(&ServerKit_ChannelTest::realChannelIsStarted, this, &result));
			return result;
		}

		void realChannelIsStarted(bool *result) {
			*result = channel.isStarted();
		}

		bool channelHasError() {
			bool result;
			bg.safe->runSync(boost::bind(&ServerKit_ChannelTest::realChannelHasError, this, &result));
			return result;
		}

		void realChannelHasError(bool *result) {
			*result = channel.hasError();
		}

		void feedChannel(const string &data) {
			bg.safe->runLater(boost::bind(&ServerKit_ChannelTest::realFeedChannel, this, data));
		}

		void realFeedChannel(string data) {
			assert(data.size() < context.mbuf_pool.mbuf_block_chunk_size);
			mbuf buf = mbuf_get(&context.mbuf_pool);
			memcpy(buf.start, data.data(), data.size());
			buf = mbuf(buf, 0, (unsigned int) data.size());
			channel.feed(buf);
		}

		void feedChannelError(int errcode) {
			bg.safe->runLater(boost::bind(&ServerKit_ChannelTest::realFeedChannelError,
				this, errcode));
		}

		void realFeedChannelError(int errcode) {
			channel.feedError(errcode);
		}

		void channelConsumed(int size, bool end) {
			bg.safe->runLater(boost::bind(&ServerKit_ChannelTest::realChannelConsumed,
				this, size, end));
		}

		void realChannelConsumed(int size, bool end) {
			channel.consumed(size, end);
		}

		void setChannelDataCallback(const Channel::DataCallback &callback) {
			bg.safe->runSync(boost::bind(&ServerKit_ChannelTest::realSetChannelDataCallback,
				this, callback));
		}

		void realSetChannelDataCallback(Channel::DataCallback callback) {
			channel.dataCallback = callback;
		}

		Channel::State getChannelState() {
			Channel::State result;
			bg.safe->runSync(boost::bind(&ServerKit_ChannelTest::realGetChannelState, this, &result));
			return result;
		}

		void realGetChannelState(Channel::State *result) {
			*result = channel.getState();
		}

		int getChannelErrcode() {
			int result;
			bg.safe->runSync(boost::bind(&ServerKit_ChannelTest::realGetChannelErrcode, this, &result));
			return result;
		}

		void realGetChannelErrcode(int *result) {
			*result = channel.getErrcode();
		}

		bool channelIsAcceptingInput() {
			bool result;
			bg.safe->runSync(boost::bind(&ServerKit_ChannelTest::realChannelIsAcceptingInput, this, &result));
			return result;
		}

		void realChannelIsAcceptingInput(bool *result) {
			*result = channel.acceptingInput();
		}

		bool channelMayAcceptInputLater() {
			bool result;
			bg.safe->runSync(boost::bind(&ServerKit_ChannelTest::realChannelMayAcceptInputLater, this, &result));
			return result;
		}

		void realChannelMayAcceptInputLater(bool *result) {
			*result = channel.mayAcceptInputLater();
		}

		void logChannelStateLater() {
			bg.safe->runLater(boost::bind(&ServerKit_ChannelTest::logChannelState, this));
		}

		void logChannelState() {
			boost::lock_guard<boost::mutex> l(syncher);
			log.append("State: " + toString((int) channel.getState()) + "\n");
		}

		void feedSomeDataAndWaitForConsumption() {
			feedChannel("aaabbb");
			EVENTUALLY(5,
				boost::lock_guard<boost::mutex> l(syncher);
				result = !log.empty();
			);
			{
				boost::lock_guard<boost::mutex> l(syncher);
				ensure_equals(log, "Data: aaabbb\n");
			}
			EVENTUALLY(5,
				result = getChannelState() == Channel::IDLE;
			);
		}
	};

	#define LOCK() boost::unique_lock<boost::mutex> l(syncher)
	#define UNLOCK() l.unlock()

	#define DEFINE_DATA_CALLBACK_METHOD(name, code) \
		static Channel::Result name(Channel *channel, const mbuf &buffer, int errcode) { \
			ServerKit_ChannelTest *self = (ServerKit_ChannelTest *) channel->hooks; \
			boost::mutex &syncher = self->syncher; \
			/* Shut up compiler warning */  \
			(void) syncher; \
			code \
		}

	DEFINE_TEST_GROUP_WITH_LIMIT(ServerKit_ChannelTest, 100);


	/***** Initial state *****/

	TEST_METHOD(1) {
		set_test_name("It is idle, accepts input, is not error'red and hasn't ended");
		ensure_equals(channel.getState(), Channel::IDLE);
		ensure(channel.acceptingInput());
		ensure(!channel.hasError());
		ensure(!channel.ended());
	}

	TEST_METHOD(2) {
		set_test_name("Upon being fed data, it calls the callback, transitions "
			"to the idle state and calls the consumption callback");
		feedChannel("aaabbb");
		EVENTUALLY(5,
			LOCK();
			result = !log.empty();
		);
		{
			LOCK();
			ensure_equals(log, "Data: aaabbb\n");
			ensure_equals(idleCount, 1u);
			ensure_equals(bytesConsumed, 6u);
		}
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
	}

	TEST_METHOD(3) {
		set_test_name("Upon being fed EOF, it calls the callback with an empty buffer "
			"and transitions to the EOF state");
		feedChannel("");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(log, "EOF\n");
			ensure_equals(endAcked, 1u);
			ensure_equals(bytesConsumed, 0u);
		}
	}

	TEST_METHOD(4) {
		set_test_name("Upon being fed an error, it calls the callback with an error code "
			"and transitions to the EOF state");
		feedChannelError(EIO);
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(log, "Error: " + toString(EIO) + "\n");
			ensure_equals(endAcked, 1u);
			ensure_equals(bytesConsumed, 0u);
		}
	}


	/***** When the callback is done consuming data and the Channel is now idle *****/

	TEST_METHOD(10) {
		set_test_name("It is idle and accepts input");

		feedSomeDataAndWaitForConsumption();
		ensure_equals(getChannelState(), Channel::IDLE);
		ensure(channelIsAcceptingInput());
	}

	TEST_METHOD(11) {
		set_test_name("It calls the consumption callback");

		feedSomeDataAndWaitForConsumption();
		LOCK();
		ensure_equals(idleCount, 1u);
	}

	TEST_METHOD(12) {
		set_test_name("Upon being fed data, it calls the callback and transitions "
			"to the idle state");

		feedSomeDataAndWaitForConsumption();
		feedChannel("aaabbb");
		EVENTUALLY(5,
			LOCK();
			result = log.find("Data: aaabbb\n") != string::npos;
		);
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
	}

	TEST_METHOD(13) {
		set_test_name("Upon being fed EOF, it calls the callback with an empty "
			"buffer and transitions to the EOF state");

		feedSomeDataAndWaitForConsumption();
		feedChannel("");
		EVENTUALLY(5,
			LOCK();
			result = log.find("EOF") != string::npos;
		);
		{
			LOCK();
			ensure_equals(log,
				"Data: aaabbb\n"
				"EOF\n");
		}
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
	}

	TEST_METHOD(14) {
		set_test_name("Upon being fed an error, it calls the callback with an "
			"error code and transitions to the EOF state");

		feedSomeDataAndWaitForConsumption();
		feedChannelError(EIO);
		EVENTUALLY(5,
			LOCK();
			result = log.find("Error") != string::npos;
		);
		{
			LOCK();
			ensure_equals(log,
				"Data: aaabbb\n"
				"Error: " + toString(EIO) + "\n");
		}
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
	}


	/***** When the callback is in progress *****/

	DEFINE_DATA_CALLBACK_METHOD(test_20_callback,
		LOCK();
		self->lastState = self->channel.getState();
		return Channel::Result(buffer.size(), false);
	);

	TEST_METHOD(20) {
		set_test_name("It is in the calling state");

		setChannelDataCallback(test_20_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(lastState, Channel::CALLING);
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_21_callback,
		LOCK();
		self->counter++;
		if (self->counter == 1) {
			self->log.append("Feeding error\n");
			UNLOCK();
			self->channel.feedError(EIO);
		} else {
			self->log.append("Received error: " + toString(errcode) + "\n");
			UNLOCK();
		}
		return Channel::Result(buffer.size(), false);
	);

	TEST_METHOD(21) {
		set_test_name("Upon being fed an error, it transitions to the EOF state immediately "
			"and doesn't call the callback with an error code");

		setChannelDataCallback(test_21_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(log,
				"Feeding error\n");
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_22_callback,
		self->channel.start();
		LOCK();
		self->log.append("Channel state: " + toString(self->channel.getState()) + "\n");
		self->log.append("Channel started: " + toString(self->channel.isStarted()) + "\n");
		return Channel::Result(buffer.size(), false);
	);

	TEST_METHOD(22) {
		set_test_name("Upon calling start(), nothing happens");

		setChannelDataCallback(test_22_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(log,
				"Channel state: " + toString(Channel::CALLING) + "\n"
				"Channel started: 1\n");
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_23_callback,
		self->channel.stop();
		return Channel::Result(buffer.size(), false);
	);

	TEST_METHOD(23) {
		set_test_name("Upon calling stop(), it transitions to the stopped state");

		setChannelDataCallback(test_23_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED;
		);
		SHOULD_NEVER_HAPPEN(100,
			result = getChannelState() != Channel::STOPPED;
		);
	}

	DEFINE_DATA_CALLBACK_METHOD(test_24_callback,
		self->channel.stop();
		{
			LOCK();
			self->log.append("Channel state: " + toString(self->channel.getState()) + "\n");
			self->log.append("Channel started: " + toString(self->channel.isStarted()) + "\n");
		}
		self->channel.start();
		{
			LOCK();
			self->log.append("Channel state: " + toString(self->channel.getState()) + "\n");
			self->log.append("Channel started: " + toString(self->channel.isStarted()) + "\n");
		}
		return Channel::Result(buffer.size(), false);
	);

	TEST_METHOD(24) {
		set_test_name("Upon calling stop() then start(), it transitions to the calling state");

		setChannelDataCallback(test_24_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(log,
				"Channel state: " + toString(Channel::STOPPED_WHILE_CALLING) + "\n"
				"Channel started: 0\n"
				"Channel state: " + toString(Channel::CALLING) + "\n"
				"Channel started: 1\n");
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_25_callback,
		LOCK();
		self->counter++;
		if (self->counter == 1) {
			UNLOCK();
			self->channel.stop();
			self->channel.start();
		} else {
			StaticString str(buffer.start, buffer.size());
			self->log.append("Data: " + cEscapeString(str) + "\n");
			self->log.append("Error: " + toString(errcode) + "\n");
		}
		return Channel::Result(buffer.size(), false);
	);

	TEST_METHOD(25) {
		set_test_name("Upon calling stop() then start(), it calls the callback next time data is fed");

		setChannelDataCallback(test_25_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		feedChannel("def");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(log,
				"Data: def\n"
				"Error: 0\n");
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_26_27_callback,
		LOCK();
		self->counter++;
		if (self->counter == 1) {
			UNLOCK();
			self->channel.stop();
			self->channel.start();
		} else {
			StaticString str(buffer.start, buffer.size());
			self->log.append("Data: " + cEscapeString(str) + "\n");
			self->log.append("Error: " + toString(errcode) + "\n");
		}
		return Channel::Result(buffer.size(), false);
	);

	TEST_METHOD(26) {
		set_test_name("Upon calling stop() then start(), it calls the callback next time EOF is fed");

		setChannelDataCallback(test_26_27_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		feedChannel("");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(log,
				"Data: \n"
				"Error: 0\n");
		}
	}

	TEST_METHOD(27) {
		set_test_name("Upon calling stop() then start(), it calls the callback next time an error is fed");

		setChannelDataCallback(test_26_27_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		feedChannelError(EIO);
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(log,
				"Data: \n"
				"Error: " + toString(EIO) + "\n");
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_28_callback,
		self->channel.deinitialize();
		LOCK();
		self->log.append("Buffer size: " + toString(buffer.size()));
		return Channel::Result(buffer.size(), false);
	);

	TEST_METHOD(28) {
		set_test_name("Deinitializing the channel doesn't invalidate the buffer argument");

		setChannelDataCallback(test_28_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = !log.empty();
		);
		ensure_equals(log, "Buffer size: 3");
	}


	/***** When the callback is not in progress *****/

	TEST_METHOD(30) {
		set_test_name("Upon calling start(), nothing happens");

		startChannel();
		ensure_equals(getChannelState(), Channel::IDLE);
		ensure(channelIsStarted());
	}

	TEST_METHOD(31) {
		set_test_name("Upon calling stop(), it transitions to the stopped state");

		stopChannel();
		ensure_equals(getChannelState(), Channel::STOPPED);
		ensure(!channelIsStarted());
	}

	static void test_32_callback(ServerKit_ChannelTest *self) {
		self->channel.stop();
		self->channel.start();
		{
			boost::mutex &syncher = self->syncher;
			LOCK();
			self->log.append("Channel state: " + toString(self->channel.getState()) + "\n");
		}
	}

	TEST_METHOD(32) {
		set_test_name("Upon calling stop() then start(), it transitions to the idle state");

		bg.safe->runLater(boost::bind(test_32_callback, this));
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(log,
				"Channel state: " + toString(Channel::IDLE) + "\n");
		}
	}

	static void test_33_callback(ServerKit_ChannelTest *self) {
		self->channel.stop();
		self->channel.start();
		{
			boost::mutex &syncher = self->syncher;
			LOCK();
			self->log.append("Channel state: " + toString(self->channel.getState()) + "\n");
		}
		self->channel.feed(mbuf("abc"));
	}

	TEST_METHOD(33) {
		set_test_name("Upon calling stop() then start(), it calls the callback next time data is fed");

		bg.safe->runLater(boost::bind(test_33_callback, this));
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(log,
				"Channel state: " + toString(Channel::IDLE) + "\n"
				"Data: abc\n");
			ensure_equals(counter, 1u);
		}
	}

	static void test_34_callback(ServerKit_ChannelTest *self) {
		self->channel.stop();
		self->channel.start();
		{
			boost::mutex &syncher = self->syncher;
			LOCK();
			self->log.append("Channel state: " + toString(self->channel.getState()) + "\n");
		}
		self->channel.feed(mbuf());
	}

	TEST_METHOD(34) {
		set_test_name("Upon calling stop() then start(), it calls the callback next time EOF is fed");

		bg.safe->runLater(boost::bind(test_34_callback, this));
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(log,
				"Channel state: " + toString(Channel::IDLE) + "\n"
				"EOF\n");
			ensure_equals(counter, 1u);
		}
	}

	static void test_35_callback(ServerKit_ChannelTest *self) {
		self->channel.stop();
		self->channel.start();
		{
			boost::mutex &syncher = self->syncher;
			LOCK();
			self->log.append("Channel state: " + toString(self->channel.getState()) + "\n");
		}
		self->channel.feedError(EIO);
	}

	TEST_METHOD(35) {
		set_test_name("Upon calling stop() then start(), it calls the callback next time an error is fed");

		bg.safe->runLater(boost::bind(test_35_callback, this));
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(log,
				"Channel state: " + toString(Channel::IDLE) + "\n"
				"Error: " + toString(EIO) + "\n");
			ensure_equals(counter, 0u);
		}
	}


	/***** If the callback immediately consumed the buffer partially *****/

	TEST_METHOD(40) {
		set_test_name("If the callback has ended consumption, the Channel transitions "
			"to the 'EOF reached' state and calls the endAck callback");

		{
			LOCK();
			toConsume = 1;
			endConsume = true;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(endAcked, 1u);
			ensure_equals(log,
				"Data: abc\n");
		}
	}

	TEST_METHOD(41) {
		set_test_name("If the callback has not ended consumption, the Channel calls "
			"the callback again with the remainder of the the buffer, until the buffer is fully consumed");

		{
			LOCK();
			toConsume = 1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(log,
				"Data: abc\n"
				"Data: bc\n"
				"Data: c\n");
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_42_callback,
		LOCK();
		self->counter++;
		if (self->counter != 1) {
			UNLOCK();
			self->channel.feedError(EIO);
		}
		return Channel::Result(1, false);
	);

	TEST_METHOD(42) {
		set_test_name("Upon being fed an error, it transitions to the EOF state immediately, "
			"and it won't call the callback with an error code");

		setChannelDataCallback(test_42_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(counter, 2u);
			ensure(channelHasError());
			ensure_equals(getChannelErrcode(), EIO);
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_43_callback,
		LOCK();
		self->counter++;
		if (self->counter == 1) {
			UNLOCK();
			self->channel.stop();
		}
		return Channel::Result(1, false);
	);

	TEST_METHOD(43) {
		set_test_name("If stop() was called, it doesn't call the callback with the "
			"remainder of the buffer");

		setChannelDataCallback(test_43_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED;
		);
		{
			LOCK();
			ensure_equals(counter, 1u);
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_44_callback,
		self->channel.stop();
		LOCK();
		self->counter++;
		return Channel::Result(1, true);
	);

	TEST_METHOD(44) {
		set_test_name("If stop() was called, and the callback has ended consumption, then "
			"the Channel transitions to the 'EOF reached' state and calls the endAck callback");

		setChannelDataCallback(test_44_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(counter, 1u);
			ensure_equals(endAcked, 1u);
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_45_callback,
		self->channel.stop();
		self->channel.start();
		LOCK();
		self->counter++;
		return Channel::Result(1, true);
	);

	TEST_METHOD(45) {
		set_test_name("If stop() then start() was called, and the channel has ended consumption, "
			"then it transitions to the 'EOF reached' state and calls the endAck callback");

		setChannelDataCallback(test_45_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(counter, 1u);
			ensure_equals(endAcked, 1u);
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_46_callback,
		LOCK();
		StaticString str(buffer.start, buffer.size());
		self->counter++;
		self->log.append("Data: " + cEscapeString(str) + "\n");
		if (self->counter == 1) {
			UNLOCK();
			self->channel.stop();
			self->channel.start();
		}
		return Channel::Result(1, false);
	);

	TEST_METHOD(46) {
		set_test_name("If stop() then start() was called, and the channel has not ended consumption, "
			"then it calls the callback with the remainder of the data in the next tick");

		setChannelDataCallback(test_46_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(counter, 3u);
			ensure_equals(log,
				"Data: abc\n"
				"Data: bc\n"
				"Data: c\n");
		}
	}

	DEFINE_DATA_CALLBACK_METHOD(test_47_callback,
		LOCK();
		StaticString str(buffer.start, buffer.size());
		self->counter++;
		self->log.append("Data: " + cEscapeString(str) + "\n");
		if (self->counter == 1) {
			UNLOCK();
			self->channel.deinitialize();
		}
		return Channel::Result(1, false);
	);

	TEST_METHOD(47) {
		set_test_name("If it had been deinitialized in the callback, it doesn't call the "
			"callback again");

		setChannelDataCallback(test_47_callback);
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::CALLING;
		);
		{
			LOCK();
			ensure_equals(counter, 1u);
			ensure_equals(log,
				"Data: abc\n");
		}
	}

	TEST_METHOD(48) {
		set_test_name("If the callback has ended consumption, upon fully consuming the buffer, "
			"the Channel transitions to the 'EOF reached' state and calls the endAck callback");

		{
			LOCK();
			endConsume = true;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(counter, 1u);
			ensure_equals(endAcked, 1u);
		}
	}

	TEST_METHOD(49) {
		set_test_name("If the callback has not ended consumption, upon fully "
			"consuming the buffer, the Channel calls the consumption callback");

		{
			LOCK();
			toConsume = 1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(counter, 3u);
			ensure_equals(idleCount, 1u);
		}
	}


	/***** If the callback immediately consumed the buffer fully *****/

	TEST_METHOD(50) {
		set_test_name("If the callback has ended consumption, the Channel"
			"transitions to the 'EOF reached' state and calls the endAck callback");

		{
			LOCK();
			endConsume = true;
		}
		feedChannel("aaabbb");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(log, "Data: aaabbb\n");
			ensure_equals(endAcked, 1u);
		}
	}

	TEST_METHOD(52) {
		set_test_name("If the callback has not ended consumption, "
			"the Channel transitions to the idle state and calls the consumption callback");

		feedSomeDataAndWaitForConsumption();
		ensure_equals(getChannelState(), Channel::IDLE);
		ensure_equals(idleCount, 1u);
	}



	/***** If the callback deferred consumption *****/

	TEST_METHOD(55) {
		set_test_name("It transitions to the 'waiting for callback' state");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);
		SHOULD_NEVER_HAPPEN(100,
			result = getChannelState() != Channel::WAITING_FOR_CALLBACK;
		);
	}

	TEST_METHOD(56) {
		set_test_name("Upon being fed an error, it transitions to the 'EOF waiting' state");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannelError(EIO);
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_WAITING;
		);
		SHOULD_NEVER_HAPPEN(100,
			result = getChannelState() != Channel::EOF_WAITING;
		);
		ensure_equals(getChannelErrcode(), EIO);
	}

	TEST_METHOD(57) {
		set_test_name("When consumed() is called with the end flag, it "
			"transitions to the 'EOF reached' state and calls the endAck callback");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		channelConsumed(3, true);
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(endAcked, 1u);
		}
	}

	TEST_METHOD(58) {
		set_test_name("When consumed() is called with the full buffer size, "
			"it transitions to the idle state");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		channelConsumed(3, false);
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
	}

	TEST_METHOD(59) {
		set_test_name("Upon calling stop(), it transitions to the stopped state");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		stopChannel();
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED_WHILE_WAITING;
		);
	}

	static void test_60_stop_start_channel(ServerKit_ChannelTest *self) {
		self->channel.stop();
		self->channel.start();
		{
			boost::mutex &syncher = self->syncher;
			LOCK();
			self->counter++;
			self->log.append("Channel state: " + toString(self->channel.getState()) + "\n");
		}
	}

	TEST_METHOD(60) {
		set_test_name("Upon calling stop() then start(), it transitions to the 'waiting for callback' state");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		stopChannel();
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED_WHILE_WAITING;
		);
		bg.safe->runLater(boost::bind(test_60_stop_start_channel, this));
		EVENTUALLY(5,
			LOCK();
			result = counter == 2;
		);
		{
			LOCK();
			ensure_equals(log,
				"Data: abc\n"
				"Channel state: " + toString(Channel::WAITING_FOR_CALLBACK) + "\n");
		}
	}

	TEST_METHOD(61) {
		set_test_name("Upon calling stop() then start() then feedError(), "
			"it transitions to the EOF state immediately");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		stopChannel();
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED_WHILE_WAITING;
		);

		startChannel();
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		feedChannelError(EIO);
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		SHOULD_NEVER_HAPPEN(100,
			result = getChannelState() != Channel::EOF_REACHED;
		);
	}

	TEST_METHOD(62) {
		set_test_name("When consumed() is called with a partial buffer size, "
			"it calls the callback again with the remainder of the buffer");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		{
			LOCK();
			toConsume = CONSUME_FULLY;
		}
		channelConsumed(2, false);
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(counter, 2u);
			ensure_equals(log,
				"Data: abc\n"
				"Data: c\n");
		}
	}

	TEST_METHOD(63) {
		set_test_name("If stop() was called, and consumed() is called with a partial buffer size, "
			"then it doesn't call the callback");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		stopChannel();
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED_WHILE_WAITING;
		);

		{
			LOCK();
			toConsume = CONSUME_FULLY;
		}
		channelConsumed(2, false);
		SHOULD_NEVER_HAPPEN(100,
			LOCK();
			result = counter > 1;
		);
		ensure_equals(getChannelState(), Channel::STOPPED);
	}

	static void test_64_start_channel(ServerKit_ChannelTest *self) {
		self->channel.start();
		{
			boost::mutex &syncher = self->syncher;
			LOCK();
			self->counter++;
			self->log.append("Channel state: " + toString(self->channel.getState()) + "\n");
		}
	}

	TEST_METHOD(64) {
		set_test_name("If stop() was called, and consumed() is called with a partial buffer size, "
			"then it calls the callback with the remainder of the data one tick after next time "
			"start() is called");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		stopChannel();
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED_WHILE_WAITING;
		);

		{
			LOCK();
			toConsume = CONSUME_FULLY;
		}
		channelConsumed(2, false);
		bg.safe->runLater(boost::bind(test_64_start_channel, this));
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(counter, 3u);
			ensure_equals(log,
				"Data: abc\n"
				"Channel state: " + toString(Channel::PLANNING_TO_CALL) + "\n"
				"Data: c\n");
		}
	}

	TEST_METHOD(65) {
		set_test_name("If stop() was called, and consumed() is called with a full buffer size, "
			"then it doesn't call the callback");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		stopChannel();
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED_WHILE_WAITING;
		);

		channelConsumed(3, false);
		SHOULD_NEVER_HAPPEN(100,
			LOCK();
			result = counter > 1;
		);
		ensure_equals(getChannelState(), Channel::STOPPED);
	}

	static void test_66_start_channel(ServerKit_ChannelTest *self) {
		self->channel.start();
		{
			boost::mutex &syncher = self->syncher;
			LOCK();
			self->counter++;
			self->log.append("Channel state: " + toString(self->channel.getState()) + "\n");
			self->log.append("Idle count so far: " + toString(self->idleCount) + "\n");
		}
	}

	TEST_METHOD(66) {
		set_test_name("If stop() was called, and consumed() is called with a full buffer size, "
			"then when start() is called, it transitions to the idle state and calls the consumption callback");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("abc");
		EVENTUALLY(5,
			result = getChannelState() == Channel::WAITING_FOR_CALLBACK;
		);

		stopChannel();
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED_WHILE_WAITING;
		);

		channelConsumed(3, false);
		bg.safe->runLater(boost::bind(test_66_start_channel, this));
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		{
			LOCK();
			ensure_equals(counter, 2u);
			ensure_equals(idleCount, 1u);
			ensure_equals(log,
				"Data: abc\n"
				"Channel state: " + toString(Channel::IDLE) + "\n"
				"Idle count so far: 1\n");
		}
	}


	/***** Upon being fed EOF *****/

	TEST_METHOD(70) {
		set_test_name("If the callback does not immediately consume the EOF, "
			"the endAck callback is called when consumed() is called");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannel("");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_WAITING;
		);
		{
			LOCK();
			ensure_equals(endAcked, 0u);
		}

		channelConsumed(0, false);
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(endAcked, 1u);
		}
	}

	TEST_METHOD(71) {
		set_test_name("If the callback immediately consumes the EOF, "
			"the endAck callback is called when the data callback returns");

		feedChannel("");
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		{
			LOCK();
			ensure_equals(endAcked, 1u);
		}
	}

	TEST_METHOD(72) {
		set_test_name("It no longer accepts further input");

		feedChannel("");
		ensure(!channelIsAcceptingInput());
		ensure(!channelMayAcceptInputLater());
	}


	/***** Upon being fed an error *****/

	TEST_METHOD(75) {
		set_test_name("If the callback does not immediately consume the error, "
			"the endAck callback is called when consumed() is called");

		{
			LOCK();
			toConsume = -1;
		}
		feedChannelError(EIO);
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_WAITING;
		);
		{
			LOCK();
			ensure_equals(endAcked, 0u);
		}

		channelConsumed(0, false);
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		ensure_equals(getChannelErrcode(), EIO);
		{
			LOCK();
			ensure_equals(endAcked, 1u);
		}
	}

	TEST_METHOD(76) {
		set_test_name("If the callback immediately consumes the error, "
			"the endAck callback is called when the data callback returns");

		feedChannelError(EIO);
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
		ensure_equals(getChannelErrcode(), EIO);
		{
			LOCK();
			ensure_equals(endAcked, 1u);
		}
	}

	TEST_METHOD(77) {
		set_test_name("It no longer accepts further input");

		feedChannelError(EIO);
		ensure(!channelIsAcceptingInput());
		ensure(!channelMayAcceptInputLater());
	}
}
