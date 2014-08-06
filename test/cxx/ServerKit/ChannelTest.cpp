#include <TestSupport.h>
#include <BackgroundEventLoop.h>
#include <ServerKit/Channel.h>
#include <Constants.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>
#include <MemoryKit/mbuf.cpp>

using namespace Passenger;
using namespace Passenger::ServerKit;
using namespace Passenger::MemoryKit;
using namespace std;

namespace tut {
	struct ServerKit_ChannelTest: public Hooks {
		BackgroundEventLoop bg;
		ServerKit::Context context;
		Channel channel;
		boost::mutex syncher;
		string log;
		int toConsume;
		unsigned int counter;
		
		ServerKit_ChannelTest()
			: bg(),
			  context(bg.safe),
			  channel(&context)
		{
			channel.callback = callback;
			channel.hooks = this;
			Hooks::impl = NULL;
			Hooks::userData = NULL;
			toConsume = -1;
			counter = 0;
			bg.start();
		}

		~ServerKit_ChannelTest() {
			channel.deinitialize(); // Cancel any event loop next tick callbacks.
			setLogLevel(DEFAULT_LOG_LEVEL);
		}

		static int callback(Channel *channel, const mbuf &buffer, int errcode) {
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
				if (self->toConsume == -1) {
					return buffer.size();
				} else {
					return self->toConsume;
				}
			} else {
				self->log.append("Error: " + toString(errcode) + "\n");
				return 0;
			}
		}

		unsigned int getCounter() {
			boost::lock_guard<boost::mutex> l(syncher);
			return counter;
		}

		void startChannel() {
			bg.safe->run(boost::bind(&ServerKit_ChannelTest::realStartChannel, this));
		}

		void realStartChannel() {
			channel.start();
		}

		bool channelIsStarted() {
			bool result;
			bg.safe->run(boost::bind(&ServerKit_ChannelTest::realChannelIsStarted, this, &result));
			return result;
		}

		void realChannelIsStarted(bool *result) {
			*result = channel.isStarted();
		}

		void feedChannel(const string &data) {
			bg.safe->run(boost::bind(&ServerKit_ChannelTest::realFeedChannel, this, data));
		}

		void realFeedChannel(string data) {
			assert(data.size() < context.mbuf_pool.mbuf_block_chunk_size);
			mbuf buf = mbuf_get(&context.mbuf_pool);
			memcpy(buf.start, data.data(), data.size());
			buf = mbuf(buf, 0, (unsigned int) data.size());
			channel.feed(buf);
		}

		void feedChannelError(int errcode) {
			bg.safe->run(boost::bind(&ServerKit_ChannelTest::realFeedChannelError,
				this, errcode));
		}

		void realFeedChannelError(int errcode) {
			channel.feedError(errcode);
		}

		void setChannelCallback(const Channel::Callback &callback) {
			bg.safe->runSync(boost::bind(&ServerKit_ChannelTest::realSetChannelCallback,
				this, callback));
		}

		void realSetChannelCallback(Channel::Callback callback) {
			channel.callback = callback;
		}

		Channel::State getChannelState() {
			Channel::State result;
			bg.safe->runSync(boost::bind(&ServerKit_ChannelTest::realGetChannelState, this, &result));
			return result;
		}

		void realGetChannelState(Channel::State *result) {
			*result = channel.getState();
		}

		void logChannelStateLater() {
			bg.safe->runLater(boost::bind(&ServerKit_ChannelTest::logChannelState, this));
		}

		void logChannelState() {
			boost::lock_guard<boost::mutex> l(syncher);
			log.append("State: " + toString((int) channel.getState()) + "\n");
		}
	};

	#define LOCK() boost::lock_guard<boost::mutex> l(syncher)

	#define DEFINE_ON_DATA_METHOD(name, code) \
		static int name(Channel *channel, const mbuf &buffer, int errcode) { \
			ServerKit_ChannelTest *self = (ServerKit_ChannelTest *) channel->hooks; \
			boost::mutex &syncher = self->syncher; \
			string &log = self->log; \
			/* Shut up compiler warning */  \
			(void) syncher; \
			(void) log; \
			code \
		}
	
	#define DEFINE_FINISH_METHOD(name, code) \
		static void name(ServerKit_ChannelTest *self) { \
			boost::mutex &syncher = self->syncher; \
			string &log = self->log; \
			boost::shared_ptr<MyChannel> &channel = self->channel; \
			/* Shut up compiler warning */ \
			(void) syncher; \
			(void) log; \
			(void) channel; \
			code \
		}

	DEFINE_TEST_GROUP(ServerKit_ChannelTest);

	TEST_METHOD(1) {
		set_test_name("It calls the callback upon being fed data");
		feedChannel("aaabbb");
		EVENTUALLY(5,
			LOCK();
			result = !log.empty();
		);
		{
			LOCK();
			ensure_equals(log, "Data: aaabbb\n");
		}
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
	}

	TEST_METHOD(2) {
		set_test_name("It emits EOF events after feeding EOF");
		feedChannel("");
		EVENTUALLY(5,
			LOCK();
			result = !log.empty();
		);
		{
			LOCK();
			ensure_equals(log, "EOF\n");
		}
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
	}

	TEST_METHOD(3) {
		set_test_name("It emits EOF events after all data has been consumed");

		feedChannel("aaabbb");
		EVENTUALLY(5,
			LOCK();
			result = !log.empty();
		);
		{
			LOCK();
			ensure_equals(log, "Data: aaabbb\n");
		}
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);

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

	TEST_METHOD(4) {
		set_test_name("It emits error events after feeding an error");
		feedChannelError(EIO);
		EVENTUALLY(5,
			LOCK();
			result = !log.empty();
		);
		{
			LOCK();
			ensure_equals(log, "Error: " + toString(EIO) + "\n");
		}
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);
	}

	TEST_METHOD(5) {
		set_test_name("It emits error events after all data has been consumed");
		feedChannel("aaabbb");
		EVENTUALLY(5,
			LOCK();
			result = !log.empty();
		);
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);

		feedChannelError(EIO);
		EVENTUALLY(5,
			LOCK();
			result = log.find("Error") != string::npos;
		);
		EVENTUALLY(5,
			result = getChannelState() == Channel::EOF_REACHED;
		);

		LOCK();
		ensure_equals(log,
			"Data: aaabbb\n"
			"Error: " + toString(EIO) + "\n");
	}

	TEST_METHOD(6) {
		set_test_name("If the callback partially consumes the buffer, "
			"the Channel calls the callback again with the remaining data");
		{
			LOCK();
			toConsume = 1;
		}
		feedChannel("aabb");

		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: aabb\n"
				"Data: abb\n"
				"Data: bb\n"
				"Data: b\n";
		);
	}

	DEFINE_ON_DATA_METHOD(on_data_8,
		channel->stop();
		LOCK();
		log.append("stopped\n");
		return 3;
	)

	TEST_METHOD(8) {
		set_test_name("If the callback consumes everything and stops the "
			"Channel, then the Channel is left in the STOPPED state");
		
		setChannelCallback(on_data_8);
		feedChannel("abc");
		EVENTUALLY(5,
			LOCK();
			result = log == "stopped\n";
		);
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED;
		);
	}

	DEFINE_ON_DATA_METHOD(on_data_9,
		channel->start();
		LOCK();
		log.append("started\n");
		return 3;
	)

	TEST_METHOD(9) {
		set_test_name("If the callback consumes everything and starts the "
			"Channel, then the Channel is left in the IDLE state");
		setChannelCallback(on_data_9);
		feedChannel("abc");
		EVENTUALLY(5,
			LOCK();
			result = log == "started\n";
		);
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
	}

	DEFINE_ON_DATA_METHOD(on_data_10,
		channel->stop();
		LOCK();
		log.append("stopped\n");
		return 1;
	)

	TEST_METHOD(10) {
		set_test_name("If the callback consumes partially and stops the "
			"Channel, then the Channel is left in the STOPPED state");
		setChannelCallback(on_data_10);
		feedChannel("abc");
		EVENTUALLY(5,
			LOCK();
			result = log == "stopped\n";
		);
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED;
		);
	}

	DEFINE_ON_DATA_METHOD(on_data_11,
		channel->start();
		LOCK();
		log.append("Data: " + StaticString(buffer.start, buffer.size()) + "\n");
		return 1;
	)

	TEST_METHOD(11) {
		set_test_name("If the callback consumes partially and starts the "
			"Channel, then the Channel continues calling the callback "
			"until the entire buffer is conusmed");
		setChannelCallback(on_data_11);
		feedChannel("ab");
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
		LOCK();
		ensure_equals(log,
			"Data: ab\n"
			"Data: b\n");
	}

	DEFINE_ON_DATA_METHOD(on_data_12,
		LOCK();
		self->counter++;
		log.append("Data: " + StaticString(buffer.start, buffer.size()) + "\n");
		if (self->counter == 2) {
			channel->stop();
			log.append("stopped\n");
		}
		return 2;
	)

	TEST_METHOD(12) {
		set_test_name("If the callback first consumes the buffer partially, then "
			"consumes the buffer fully and stops the Channel, then the "
			"Channel is left at the STOPPED state");
		setChannelCallback(on_data_12);
		feedChannel("aabb");
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: aabb\n"
				"Data: bb\n"
				"stopped\n";
		);
		EVENTUALLY(5,
			result = getChannelState() == Channel::STOPPED;
		);
	}

	DEFINE_ON_DATA_METHOD(on_data_13,
		LOCK();
		self->counter++;
		log.append("Data: " + StaticString(buffer.start, buffer.size()) + "\n");
		if (self->counter == 2) {
			channel->start();
			log.append("started\n");
		}
		return 2;
	)

	TEST_METHOD(13) {
		set_test_name("If the callback first consumes the buffer partially, then "
			"consumes the buffer fully and starts the Channel, then the "
			"Channel is left at the IDLE state");
		setChannelCallback(on_data_13);
		feedChannel("aabb");
		EVENTUALLY(5,
			LOCK();
			result = log ==
				"Data: aabb\n"
				"Data: bb\n"
				"started\n";
		);
		EVENTUALLY(5,
			result = getChannelState() == Channel::IDLE;
		);
	}

	/***** If the callback didn't consume everything... *****/

		/***** If stop() is called outside the callback... *****/
#if 0
			static void on_after_processing_buffer_21(ServerKit_ChannelTest *self) {
				if (self->getCounter() == 1) {
					self->channel.stop();
					boost::lock_guard<boost::mutex> l(self->syncher);
					self->log.append("isSocketStarted: " +
						toString(self->channel.isSocketStarted()) + "\n");
				}
			}

			TEST_METHOD(21) {
				set_test_name("It doesn't call");
				toConsume = 1;
				channel.onAfterProcessingBuffer = boost::bind(on_after_processing_buffer_21, this);
				startChannel();
				writeExact(p.second, "aaabbb");
				EVENTUALLY(5,
					LOCK();
					result = log.find("isSocketStarted") != string::npos;
				);
				LOCK();
				ensure_equals(log,
					"Data: aaabbb\n"
					"isSocketStarted: 0\n");
			}

			static void on_after_processing_buffer_22(ServerKit_ChannelTest *self) {
				if (self->getCounter() == 1) {
					self->channel.stop();
					{
						boost::lock_guard<boost::mutex> l(self->syncher);
						self->log.append("Paused; isSocketStarted: " +
							toString(self->channel.isSocketStarted()) + "\n");
					}
					self->channel.start();
					{
						boost::lock_guard<boost::mutex> l(self->syncher);
						self->log.append("Resumed; isSocketStarted: " +
							toString(self->channel.isSocketStarted()) + "\n");
					}
				}
			}

			DEFINE_FINISH_METHOD(finish_22,
				LOCK();
				log.append("Done; isSocketStarted: " +
					toString(channel.isStarted()) + "\n");
			);

			TEST_METHOD(22) {
				set_test_name("It resumes the socket and re-emits remaining "
					"data one tick after start() is called");
				toConsume = 3;
				channel.onAfterProcessingBuffer = boost::bind(on_after_processing_buffer_22, this);
				startChannel();
				writeExact(p.second, "aaabbb");
				bg.safe->runAfterTS(10, boost::bind(finish_22, this));
				EVENTUALLY(5,
					LOCK();
					result = log.find("Done") != string::npos;
				);
				{
					LOCK();
					ensure_equals(log,
						"Data: aaabbb\n"
						"Paused; isSocketStarted: 0\n"
						"Resumed; isSocketStarted: 0\n"
						"Data: bbb\n"
						"Done; isSocketStarted: 1\n");
				}

				bg.safe->runAfterTS(10, boost::bind(finish_22, this));
				startChannel();
				EVENTUALLY(5,
					LOCK();
					result = log.find("Done") != string::npos;
				);
			}

			static void on_after_processing_buffer_23(ServerKit_ChannelTest *self) {
				if (self->getCounter() == 1) {
					self->channel.stop();
					{
						boost::lock_guard<boost::mutex> l(self->syncher);
						self->log.append("Paused; isSocketStarted: " +
							toString(self->channel.isSocketStarted()) + "\n");
					}
					self->channel.start();
					{
						boost::lock_guard<boost::mutex> l(self->syncher);
						self->log.append("Resumed; isSocketStarted: " +
							toString(self->channel.isSocketStarted()) + "\n");
					}
					self->channel.stop();
					{
						boost::lock_guard<boost::mutex> l(self->syncher);
						self->log.append("Paused again; isSocketStarted: " +
							toString(self->channel.isSocketStarted()) + "\n");
					}
				}
			}

			DEFINE_FINISH_METHOD(finish_23,
				LOCK();
				log.append("Timeout; isSocketStarted: " +
					toString(channel.isStarted()) + "\n");
			);

			TEST_METHOD(23) {
				set_test_name("It doesn't re-emit remaining data if start() "
					"is called, then stop() again");
				toConsume = 3;
				channel.onAfterProcessingBuffer = boost::bind(on_after_processing_buffer_23, this);
				startChannel();
				writeExact(p.second, "aaabbb");
				bg.safe->runAfterTS(10, boost::bind(finish_23, this));
				EVENTUALLY(5,
					LOCK();
					result = log.find("Timeout") != string::npos;
				);
				LOCK();
				ensure_equals(log,
					"Data: aaabbb\n"
					"Paused; isSocketStarted: 0\n"
					"Resumed; isSocketStarted: 0\n"
					"Paused again; isSocketStarted: 0\n"
					"Timeout; isSocketStarted: 0\n");
			}

		/***** If pause() is called during the handler *****/

			DEFINE_ON_DATA_METHOD(on_data_24,
				{
					LOCK();
					self->counter++;
					self->log.append("Data: " + cEscapeString(data) + "\n");
				}
				if (self->getCounter() == 1) {
					input->stop();
				}
				return 1;
			)

			DEFINE_FINISH_METHOD(finish_24,
				LOCK();
				log.append("Timeout; isSocketStarted: " +
					toString(channel.isSocketStarted()) + "\n");
			);

			TEST_METHOD(24) {
				set_test_name("It pauses the socket and doesn't re-emit remaining data");
				channel.onData = on_data_24;
				startChannel();
				writeExact(p.second, "aaabbb");
				bg.safe->runAfterTS(10, boost::bind(finish_24, this));
				EVENTUALLY(5,
					LOCK();
					result = log.find("Timeout") != string::npos;
				);
				LOCK();
				ensure_equals(log,
					"Data: aaabbb\n"
					"Timeout; isSocketStarted: 0\n");
			}

			DEFINE_ON_DATA_METHOD(on_data_25,
				{
					LOCK();
					self->counter++;
					self->log.append("Data: " + cEscapeString(data) + "\n");
				}
				if (self->getCounter() == 1) {
					input->stop();
					input->start();
				}
				return 3;
			)

			static void on_after_processing_buffer_25(ServerKit_ChannelTest *self) {
				boost::lock_guard<boost::mutex> l(self->syncher);
				if (self->counter == 1) {
					self->log.append("Handler done; isSocketStarted: " +
						toString(self->channel.isSocketStarted()) + "\n");
				}
			}

			DEFINE_FINISH_METHOD(finish_25,
				LOCK();
				log.append("Timeout; isSocketStarted: " +
					toString(channel.isStarted()) + "\n");
			);

			TEST_METHOD(25) {
				set_test_name("It re-emits remaining data one tick after start() is called");
				channel.onData = on_data_25;
				channel.onAfterProcessingBuffer = boost::bind(on_after_processing_buffer_25, this);
				startChannel();
				writeExact(p.second, "aaabbb");
				bg.safe->runAfterTS(10, boost::bind(finish_25, this));
				EVENTUALLY(5,
					LOCK();
					result = log.find("Timeout") != string::npos;
				);
				LOCK();
				ensure_equals(log,
					"Data: aaabbb\n"
					"Handler done; isSocketStarted: 0\n"
					"Data: bbb\n"
					"Timeout; isSocketStarted: 1\n");
			}

			DEFINE_ON_DATA_METHOD(on_data_26,
				{
					LOCK();
					self->counter++;
					self->log.append("Data: " + cEscapeString(data) + "\n");
				}
				if (self->getCounter() == 1) {
					input->stop();
					input->start();
					input->stop();
				}
				return 3;
			)

			static void on_after_processing_buffer_26(ServerKit_ChannelTest *self) {
				boost::lock_guard<boost::mutex> l(self->syncher);
				if (self->counter == 1) {
					self->log.append("Handler done; isSocketStarted: " +
						toString(self->channel.isSocketStarted()) + "\n");
				}
			}

			DEFINE_FINISH_METHOD(finish_26,
				LOCK();
				log.append("Timeout; isSocketStarted: " +
					toString(channel.isSocketStarted()) + "\n");
			);

			TEST_METHOD(26) {
				set_test_name("It doesn't re-emit remaining data if start() is called, then stop() again");
				channel.onData = on_data_26;
				channel.onAfterProcessingBuffer = boost::bind(on_after_processing_buffer_26, this);
				startChannel();
				writeExact(p.second, "aaabbb");
				bg.safe->runAfterTS(10, boost::bind(finish_26, this));
				EVENTUALLY(5,
					LOCK();
					result = log.find("Timeout") != string::npos;
				);
				LOCK();
				ensure_equals(log,
					"Data: aaabbb\n"
					"Handler done; isSocketStarted: 0\n"
					"Timeout; isSocketStarted: 0\n");
			}

		/***** If the socket was disconnected *****/

			TEST_METHOD(27) {
				set_test_name("It doesn't re-emit the remaining data");
				// TODO
			}

	TEST_METHOD(31) {
		set_test_name("It doesn't emit data events if it's paused, but re-emits "
			"previously unemitted data events after resume");
		// TODO
	}
#endif
}
