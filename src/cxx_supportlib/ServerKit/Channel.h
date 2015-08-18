/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_SERVER_KIT_CHANNEL_H_
#define _PASSENGER_SERVER_KIT_CHANNEL_H_

#include <oxt/backtrace.hpp>
#include <oxt/macros.hpp>
#include <boost/noncopyable.hpp>
#include <boost/move/core.hpp>
#include <algorithm>
#include <cassert>
#include <ServerKit/Context.h>
#include <ServerKit/Hooks.h>
#include <MemoryKit/mbuf.h>
#include <Logging.h>
#include <Utils/StrIntUtils.h>
#include <Utils/json.h>

namespace Passenger {
namespace ServerKit {

using namespace boost;


/**
 * A building block for consuming buffers partially and asynchronously. When writing
 * evented servers, handling incoming data poses many problems. You might not be immediately
 * able to handle all data that you receive over a single `read()` call. For example,
 * after parsing request headers, you might want to create a temp file for storing the
 * request body, and you can't parse the request body until the temp file is created. If
 * you received the headers and (a part of) the request body in the same `read()` call
 * then you have to buffer the partially received request body. You might not even want
 * to consume all data, because some data might belong to the next request, so you have
 * to pass the remainder of the buffer to the next parser iteration.
 *
 * Writing all this code is complicated, error-prone, its flow is hard to test (because
 * it depends on network conditions), and it's ridden with boilerplate. The Channel class
 * solves this problem with a nice abstraction. First, you attach a data callback to a
 * Channel. Whatever is written to the Channel, will be forwarded to the data callback.
 *
 * The data callback can consume the buffer immediately, and tell Channel how many bytes
 * it has consumed, and whether it accepts any further data, by returning a Channel::Result.
 * If the buffer was not fully consumed by the data callback, and the callback is still
 * willing to accept further data (by not transitioning to the end state or an error state),
 * then Channel will call the data callback again with the remainder of the buffer. This
 * repeats until:
 *
 *  * the buffer is fully consumed,
 *  * or until the callback indicates that it's no longer accepting further data,
 *  * or (if proper hooks are provided) until the client is disconnected.
 *
 * Typical usage of Channel goes like this:
 *
 *     // Initialization. Set data callback.
 *     Channel channel;
 *     channel.dataCallback = channelDataReceived;
 *
 *     // Begin feeding data.
 *     feedMoreData();
 *
 *     void feedMoreData() {
 *         channel.feed("hello");
 *         // or channel.feed("") for EOF
 *         // or channel.feedError(...)
 *
 *         if (channel.acceptingInput()) {
 *             // The data callback has immediately consumed the data,
 *             // and is ready to consume more. You can feed more data now.
 *             ...call feedMoreData() some time later...
 *         } else if (channel.mayAcceptInputLater()) {
 *             // The data isn't consumed yet. We install a notification
 *             // callback, and we try again later.
 *             channel.consumedCallback = channelConsumed;
 *         } else if (channel->ended()) {
 *             // The data callback has immediately consumed the data,
 *             // but no longer accepts further data.
 *             ...
 *         } else {
 *             // The data callback signaled an error.
 *             ...
 *         }
 *     }
 *
 *     void channelConsumed(Channel *channel, unsigned int size) {
 *         // The data callback is now done consuming, but it may have
 *         // transitioned to the end or error state, so here we check
 *         // whether that is the case and whether we can feed more data.
 *         //
 *         // There is no need to check for mayAcceptInputLater() here.
 *
 *         channel->consumedCallback = NULL;
 *
 *         if (channel->acceptingInput()) {
 *             // The channel is now able to accept more data.
 *             // Feed some more data...
 *             ...call feedMoreData() some time later...
 *         } else if (channel->ended()) {
 *             // The data callback no longer accepts further data.
 *             ...
 *         } else {
 *             // The data callback signaled an error.
 *             ...
 *         }
 *     }
 *
 *     Channel::Result channelDataReceived(Channel *channel, mbuf &buffer,
 *         int errcode)
 *     {
 *         if (buffer.size() > 0) {
 *             int bytesProcessed;
 *             bool acceptFurtherData;
 *
 *             ...process buffer....
 *
 *             return Channel::Result(bytesProcessed, !acceptFurtherData);
 *         } else if (errcode == 0) {
 *             // EOF reached. Result doesn't matter in this case.
 *             return Channel::Result(0, false);
 *         } else {
 *             // An error occurred! Result doesn't matter in this case.
 *             fprintf(stderr, "An error occurred! errno=%d\n", errcode);
 *             return Channel::Result(0, false);
 *         }
 *     }
 *
 * A good example of this is FdInputChannel. It reads data from a file descriptor using
 * `read()`, then writes them to a Channel. It stops reading from the file descriptor
 * when the Channel is not accepting reads, and it starts reading from the file
 * descriptor when the channel is accepting reads again.
 *
 * ## Asynchronous consumption
 *
 * The data callback can also tell Channel that it wants to consume the buffer
 * *asynchronously*, by returning a Channel::Result with a negative consumption size.
 * At some later point, something must notify Channel that the buffer is consumed,
 * by calling `channel.consumed()`. Until that happens, the Channel will tell the
 * writer that it is not accepting any new data, so that the writer can stop writing
 * temporarily. When the buffer is consumed, the Channel notifies the writer about
 * this so that it can continue writing.
 */
class Channel: public boost::noncopyable {
public:
	struct Result {
		int consumed;
		bool end;

		Result() { }

		Result(int _consumed, bool _end)
			: consumed(_consumed),
			  end(_end)
			{ }
	};

	typedef Result (*DataCallback)(Channel *channel, const MemoryKit::mbuf &buffer, int errcode);
	typedef   void (*ConsumedCallback)(Channel *channel, unsigned int size);

	enum State {
		/**
		 * No data is available. We're waiting for data to be fed.
		 */
		IDLE,

		/**
		 * Fed data has been passed to the callback, and we're now
		 * waiting for the callback to return.
		 */
		CALLING,

		/**
		 * The callback indicated that it will call `consumed()` later.
		 * We're now waiting for that call.
		 */
		WAITING_FOR_CALLBACK,

		/**
		 * `stop()` was called while we were in the IDLE state.
		 * No data will be passed to the callback.
		 */
		STOPPED,

		/**
		 * `stop()` was called while we were in the CALLING state.
		 * When the callback completes, we will transition to STOPPED,
		 * and no further data will be passed to the callback until
		 * `start()` is called.
		 */
		STOPPED_WHILE_CALLING,

		/**
		 * `stop()` was called while we were in the WAITING_FOR_CALLBACK state.
		 * When the callback completes, we will transition to `STOPPED`,
		 * and no further data will be passed to the callback until
		 * `start()` is called.
		 */
		STOPPED_WHILE_WAITING,

		/**
		 * `start()` was called while we were in the STOPPED state,
		 * or `consumed()` was called while we were in the WAITING_FOR_CALLBACK.
		 *
		 * On the next event loop tick, we will either transition to CALLING
		 * and call the callback, or transition to IDLE, depending on whether
		 * there is data to pass to the callback.
		 */
		PLANNING_TO_CALL,

		/**
		 * An end-of-file or error has been passed to the callback, and we're
		 * now waiting for the callback to return.
		 */
		CALLING_WITH_EOF,

		/**
		 * An end-of-file or error has been passed to the callback, but the
		 * callback hasn't called `consumed()` yet.
		 */
		EOF_WAITING,

		/**
		 * An end-of-file or error has been passed to the callback, and the
		 * callback has returned and completed.
		 */
		EOF_REACHED
	};

protected:
	State state: 4;
	/** ID of the next event loop tick callback. */
	unsigned int planId: 28;
	/** If an error occurred, the errno code is stored here. 0 means no error. */
	int errcode;
	unsigned int generation;
	unsigned int bytesConsumed;
	/** Buffer that will be (or is being) passed to the callback. */
	MemoryKit::mbuf buffer;
	Context *ctx;

	int callDataCallback() {
		RefGuard guard(hooks, this, __FILE__, __LINE__);
		return callDataCallbackWithoutRefGuard();
	}

	int callDataCallbackWithoutRefGuard() {
		unsigned int generation = this->generation;
		Result cbResult;

		begin:

		assert(state == CALLING || state == CALLING_WITH_EOF);
		assert(state != CALLING || !buffer.empty());
		assert(state != CALLING_WITH_EOF || buffer.empty());

		{
			// Make a copy of the buffer so that if the callback calls
			// deinitialize(), it won't suddenly reset the buffer argument.
			MemoryKit::mbuf copy(buffer);
			cbResult = dataCallback(this, copy, errcode);
		}
		if (generation != this->generation) {
			// Callback deinitialized this object.
			return bytesConsumed;
		}
		cbResult.consumed = std::min<int>(cbResult.consumed, buffer.size());

		assert(state != IDLE);
		assert(state != WAITING_FOR_CALLBACK);
		assert(state != STOPPED);
		assert(state != STOPPED_WHILE_WAITING);
		assert(state != PLANNING_TO_CALL);
		assert(state != EOF_WAITING);

		if (cbResult.consumed >= 0) {
			bytesConsumed += cbResult.consumed;
			if ((unsigned int) cbResult.consumed == buffer.size()) {
				// Unref mbuf_block
				buffer = MemoryKit::mbuf();
			} else {
				buffer = MemoryKit::mbuf(buffer, cbResult.consumed);
			}

			switch (state) {
			case CALLING:
				if (cbResult.end) {
					state = EOF_REACHED;
					callConsumedCallback();
					return bytesConsumed;
				} else if (buffer.empty()) {
					state = IDLE;
					callConsumedCallback();
					return bytesConsumed;
				} else {
					if (hooks == NULL
					 || hooks->impl == NULL
					 || hooks->impl->hook_isConnected(hooks, this))
					{
						goto begin;
					} else {
						callConsumedCallback();
						return bytesConsumed;
					}
				}
			case STOPPED_WHILE_CALLING:
				if (cbResult.end) {
					state = EOF_REACHED;
					callConsumedCallback();
					return bytesConsumed;
				} else {
					state = STOPPED;
					return -1;
				}
			case CALLING_WITH_EOF:
				state = EOF_REACHED;
				callConsumedCallback();
				return bytesConsumed;
			case EOF_REACHED:
				// feedError() called inside callback, so we
				// don't callConsumedCallback() here.
				state = EOF_REACHED;
				return bytesConsumed;
			default:
				P_BUG("Unknown state" << toString((int) state));
				return 0;
			}

		} else {
			switch (state) {
			case CALLING:
				state = WAITING_FOR_CALLBACK;
				break;
			case STOPPED_WHILE_CALLING:
				state = STOPPED_WHILE_WAITING;
				break;
			case CALLING_WITH_EOF:
			case EOF_REACHED:
				state = EOF_WAITING;
				break;
			default:
				P_BUG("Unknown state" << toString((int) state));
				break;
			}
			return -1;
		}
	}

	void planNextActivity() {
		if (buffer.empty()) {
			state = IDLE;
			callConsumedCallback();
		} else {
			state = PLANNING_TO_CALL;
			planId = ctx->libev->runLater(boost::bind(
				&Channel::executeCall, this));
		}
	}

	void executeCall() {
		P_ASSERT_EQ(state, PLANNING_TO_CALL);
		planId = 0;
		state = CALLING;
		callDataCallback();
	}

	void callConsumedCallback() {
		unsigned int bytesConsumed = this->bytesConsumed;
		this->bytesConsumed = 0;
		if (consumedCallback != NULL) {
			consumedCallback(this, bytesConsumed);
		}
	}

public:
	DataCallback dataCallback;
	/**
	 * Called whenever fed data has been fully consumed, or when it has become idle.
	 * The latter is triggered by calling `stop()` on an idle channel, and then
	 * `start()` again. In this case, `size` will be 0.
	 */
	ConsumedCallback consumedCallback;
	Hooks *hooks;

	/**
	 * Creates a Channel without a context. It doesn't work properly yet until
	 * you call `setContext()`.
	 */
	Channel()
		: state(EOF_REACHED),
		  planId(0),
		  errcode(0),
		  generation(0),
		  bytesConsumed(0),
		  ctx(NULL),
		  dataCallback(NULL),
		  consumedCallback(NULL),
		  hooks(NULL)
		{ }

	/**
	 * Creates a Channel with the given context, which must be non-NULL.
	 */
	Channel(Context *context)
		: state(IDLE),
		  planId(0),
		  errcode(0),
		  generation(0),
		  bytesConsumed(0),
		  ctx(context),
		  dataCallback(NULL),
		  consumedCallback(NULL),
		  hooks(NULL)
		{ }

	~Channel() {
		if (ctx != NULL) {
			ctx->libev->cancelCommand(planId);
		}
	}

	/**
	 * Sets the context in case you constructed a Channel without one.
	 * The Channel object doesn't work until you've set a context.
	 * May only be called right after construction.
	 */
	void setContext(Context *context) {
		ctx = context;
	}

	/**
	 * Reinitialize the Channel to its starting state so that you can reuse the
	 * object. You may only call this after calling `deinitialize()`.
	 */
	void reinitialize() {
		state   = IDLE;
		errcode = 0;
		bytesConsumed = 0;
	}

	/**
	 * Deinitialize the channel and reset it into a terminal state.
	 * Whatever operations it was doing in the background will be canceled.
	 * After deinitializing, you may reinitialize it and reuse the Channel.
	 */
	void deinitialize() {
		if (ctx != NULL) {
			ctx->libev->cancelCommand(planId);
		}
		planId = 0;
		buffer = MemoryKit::mbuf();
		generation++;
	}

	/**
	 * Feed data to the Channel. The data will be passed to the callback.
	 *
	 * @pre acceptingInput()
	 */
	int feed(const MemoryKit::mbuf &mbuf) {
		MemoryKit::mbuf mbuf_copy(mbuf);
		return feed(boost::move(mbuf_copy));
	}

	int feed(BOOST_RV_REF(MemoryKit::mbuf) mbuf) {
		RefGuard guard(hooks, this, __FILE__, __LINE__);
		return feedWithoutRefGuard(mbuf);
	}

	/**
	 * A special version of `feed()` which does not call `hooks->hook_ref()`
	 * and `hooks->hook_unref()`. Use it in certain optimization scenarios,
	 * where you are sure that extra reference counts are not needed.
	 *
	 * @pre acceptingInput()
	 */
	int feedWithoutRefGuard(const MemoryKit::mbuf &mbuf) {
		MemoryKit::mbuf mbuf_copy(mbuf);
		return feedWithoutRefGuard(boost::move(mbuf_copy));
	}

	int feedWithoutRefGuard(BOOST_RV_REF(MemoryKit::mbuf) mbuf) {
		P_ASSERT_EQ(state, IDLE);
		P_ASSERT_EQ(bytesConsumed, 0);
		if (mbuf.empty()) {
			state = CALLING_WITH_EOF;
		} else {
			state = CALLING;
		}
		buffer = mbuf;
		return callDataCallbackWithoutRefGuard();
	}

	/**
	 * Tell the Channel that an error has occurred.
	 *
	 * If this method is called while the data callback is not active, and the
	 * channel is idle, then the error will be passed to the callback immediately.
	 * Otherwise (if the channel isn't idle), it will be passed to the callback
	 * when the channel becomes idle.
	 *
	 * If this method is called inside the data callback, or (if the data callback returned -1)
	 * when the data callback hasn't called `consumed()` yet, then the channel transitions
	 * to the end state immediately, and stops calling the data callback even when the
	 * current invocation of the data callback doesn't fully consume the buffer.
	 *
	 * Once an error has been fed, no more data will be accepted by `feed()`.
	 */
	void feedError(int errcode) {
		assert(errcode != 0);
		switch (state) {
		case IDLE:
			this->errcode = errcode;
			state = CALLING_WITH_EOF;
			callDataCallback();
			break;
		case CALLING:
		case WAITING_FOR_CALLBACK:
		case CALLING_WITH_EOF:
		case EOF_WAITING:
			this->errcode = errcode;
			state = EOF_REACHED;
			callConsumedCallback();
			break;
		case EOF_REACHED:
			this->errcode = errcode;
			state = EOF_REACHED;
			break;
		case STOPPED:
		case STOPPED_WHILE_CALLING:
		case STOPPED_WHILE_WAITING:
			P_BUG("May not call feedError() while in the STOPPED, STOPPED_WHILE_CALLING "
				"or STOPPED_WHILE_WAITING state");
			break;
		case PLANNING_TO_CALL:
			ctx->libev->cancelCommand(planId);
			planId = 0;
			this->errcode = errcode;
			state = EOF_REACHED;
			callConsumedCallback();
			break;
		default:
			P_BUG("Unknown state" << toString((int) state));
			break;
		}
	}

	/**
	 * Resume a stopped Channel.
	 */
	void start() {
		switch (state) {
		case IDLE:
		case CALLING:
		case PLANNING_TO_CALL:
		case WAITING_FOR_CALLBACK:
		case CALLING_WITH_EOF:
		case EOF_WAITING:
		case EOF_REACHED:
			break;
		case STOPPED:
			planNextActivity();
			break;
		case STOPPED_WHILE_CALLING:
			state = CALLING;
			break;
		case STOPPED_WHILE_WAITING:
			state = WAITING_FOR_CALLBACK;
			break;
		default:
			P_BUG("Unknown state" << toString((int) state));
			break;
		}
	}

	/**
	 * Stops a Channel. That is, do not call the callback even when there
	 * is data available. This continues until you call `start()`.
	 */
	void stop() {
		switch (state) {
		case STOPPED:
		case STOPPED_WHILE_CALLING:
		case STOPPED_WHILE_WAITING:
		case CALLING_WITH_EOF:
		case EOF_WAITING:
		case EOF_REACHED:
			break;
		case IDLE:
		case PLANNING_TO_CALL:
			state = STOPPED;
			if (state == PLANNING_TO_CALL) {
				ctx->libev->cancelCommand(planId);
				planId = 0;
			}
			break;
		case CALLING:
			state = STOPPED_WHILE_CALLING;
			break;
		case WAITING_FOR_CALLBACK:
			state = STOPPED_WHILE_WAITING;
			break;
		default:
			P_BUG("Unknown state" << toString((int) state));
			break;
		}
	}

	/**
	 * If the callback returned -1, then at some later point it must call this method
	 * to notify Channel how many bytes have been consumed.
	 */
	void consumed(unsigned int size, bool end) {
		assert(state != IDLE);
		assert(state != CALLING);
		assert(state != STOPPED);
		assert(state != STOPPED_WHILE_CALLING);
		assert(state != PLANNING_TO_CALL);
		assert(state != CALLING_WITH_EOF);
		assert(state != EOF_REACHED);

		size = std::min<unsigned int>(size, buffer.size());
		bytesConsumed += size;
		if (size == buffer.size()) {
			// Unref mbuf_block
			buffer = MemoryKit::mbuf();
		} else {
			buffer = MemoryKit::mbuf(buffer, size);
		}

		switch (state) {
		case WAITING_FOR_CALLBACK:
			if (end) {
				state = EOF_REACHED;
				callConsumedCallback();
			} else {
				planNextActivity();
			}
			break;
		case STOPPED_WHILE_WAITING:
			if (end) {
				state = EOF_REACHED;
				callConsumedCallback();
			} else {
				state = STOPPED;
			}
			break;
		case EOF_WAITING:
			state = EOF_REACHED;
			callConsumedCallback();
			break;
		default:
			P_BUG("Unknown state" << toString((int) state));
			break;
		}
	}

	OXT_FORCE_INLINE
	State getState() const {
		return state;
	}

	OXT_FORCE_INLINE
	bool isIdle() const {
		return acceptingInput();
	}

	bool isStarted() const {
		return state != STOPPED && state != STOPPED_WHILE_CALLING && state != STOPPED_WHILE_WAITING;
	}

	/**
	 * Returns whether this Channel accepts more input right now.
	 * There are three reasons why this might not be the case:
	 *
	 * 1. The callback isn't done yet.
	 * 2. EOF has been fed, or the data callback has ended consumption.
	 *    Use `ended()` to check for this.
	 * 3. An error had been fed. Use `hasError()` to check for this.
	 */
	OXT_FORCE_INLINE
	bool acceptingInput() const {
		return state == IDLE;
	}

	/**
	 * Returns whether this Channel's callback is currently processing the
	 * fed data, and is not accepting any more input now, but may accept
	 * more input later. You should wait for that event by setting
	 * `consumedCallback`.
	 */
	bool mayAcceptInputLater() const {
		// Branchless code
		return (state >= CALLING) & (state <= PLANNING_TO_CALL);
	}

	/**
	 * Returns whether an error flag has been set. Note that this does not
	 * necessarily mean that the callback has consumed the error yet.
	 * Use `hasError() && endAcked()` to check for that.
	 */
	OXT_FORCE_INLINE
	bool hasError() const {
		return errcode != 0;
	}

	OXT_FORCE_INLINE
	int getErrcode() const {
		return errcode;
	}

	/**
	 * Returns whether the EOF flag has been set. Note that this does not
	 * necessarily mean that the callback has consumed the EOF yet.
	 * Use `endAcked()` to check for that.
	 */
	bool ended() const {
		return state == CALLING_WITH_EOF || state == EOF_WAITING || state == EOF_REACHED;
	}

	OXT_FORCE_INLINE
	bool endAcked() const {
		return state == EOF_REACHED;
	}

	Json::Value inspectAsJson() const {
		Json::Value doc;

		doc["callback_in_progress"] = !acceptingInput();
		if (hasError()) {
			doc["error"] = errcode;
			doc["error_acked"] = endAcked();
		} else if (ended()) {
			doc["ended"] = true;
			doc["end_acked"] = endAcked();
		}

		return doc;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_CHANNEL_H_ */
