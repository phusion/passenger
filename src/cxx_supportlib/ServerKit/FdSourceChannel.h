/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#ifndef _PASSENGER_SERVER_KIT_FD_SOURCE_CHANNEL_H_
#define _PASSENGER_SERVER_KIT_FD_SOURCE_CHANNEL_H_

#include <oxt/macros.hpp>
#include <boost/move/move.hpp>
#include <sys/types.h>
#include <unistd.h>
#include <ev.h>
#include <jsoncpp/json.h>
#include <MemoryKit/mbuf.h>
#include <ServerKit/Context.h>
#include <ServerKit/Channel.h>

namespace Passenger {
namespace ServerKit {

using namespace oxt;


class FdSourceChannel: protected Channel {
private:
	ev_io watcher;
	MemoryKit::mbuf buffer;

	static void _onReadable(EV_P_ ev_io *io, int revents) {
		static_cast<FdSourceChannel *>(io->data)->onReadable(io, revents);
	}

	void onReadable(ev_io *io, int revents) {
		RefGuard guard(hooks, this, __FILE__, __LINE__);
		onReadableWithoutRefGuard();
	}

	void onReadableWithoutRefGuard() {
		unsigned int generation = this->generation;
		unsigned int i, origBufferSize;
		bool done = false;
		ssize_t ret;
		int e;

		if (!acceptingInput()) {
			ev_io_stop(ctx->libev->getLoop(), &watcher);
			if (mayAcceptInputLater()) {
				consumedCallback = onChannelConsumed;
			}
			return;
		}

		for (i = 0; i < burstReadCount && !done; i++) {
			if (buffer.empty()) {
				buffer = MemoryKit::mbuf_get(&ctx->mbuf_pool);
			}

			origBufferSize = buffer.size();
			do {
				ret = ::read(watcher.fd, buffer.start, buffer.size());
			} while (OXT_UNLIKELY(ret == -1 && errno == EINTR));
			if (ret > 0) {
				MemoryKit::mbuf buffer2(buffer, 0, ret);
				if (size_t(ret) == size_t(buffer.size())) {
					// Unref mbuf_block
					buffer = MemoryKit::mbuf();
				} else {
					buffer = MemoryKit::mbuf(buffer, ret);
				}
				feedWithoutRefGuard(boost::move(buffer2));
				if (generation != this->generation) {
					// Callback deinitialized this object.
					return;
				}

				if (!acceptingInput()) {
					done = true;
					ev_io_stop(ctx->libev->getLoop(), &watcher);
					if (mayAcceptInputLater()) {
						consumedCallback = onChannelConsumed;
					}
				} else {
					// If we were unable to fill the entire buffer, then it's likely that
					// the client is slow and that the next read() will fail with
					// EAGAIN, so we stop looping and return to the event loop poller.
					done = (size_t) ret < origBufferSize;
				}

			} else if (ret == 0) {
				done = true;
				ev_io_stop(ctx->libev->getLoop(), &watcher);
				buffer = MemoryKit::mbuf();
				feedWithoutRefGuard(MemoryKit::mbuf());

			} else {
				e = errno;
				done = true;
				buffer = MemoryKit::mbuf();
				if (e != EAGAIN && e != EWOULDBLOCK) {
					ev_io_stop(ctx->libev->getLoop(), &watcher);
					feedError(e);
				}
			}
		}
	}

	static void onChannelConsumed(Channel *channel, unsigned int size) {
		FdSourceChannel *self = static_cast<FdSourceChannel *>(channel);
		self->consumedCallback = NULL;
		if (self->acceptingInput()) {
			ev_io_start(self->ctx->libev->getLoop(), &self->watcher);
		}
	}

	void initialize() {
		burstReadCount = 1;
		watcher.active = false;
		watcher.fd = -1;
		watcher.data = this;
	}

public:
	unsigned int burstReadCount;

	FdSourceChannel() {
		initialize();
	}

	FdSourceChannel(Context *context)
		: Channel(context)
	{
		initialize();
	}

	~FdSourceChannel() {
		if (ctx != NULL && ev_is_active(&watcher)) {
			ev_io_stop(ctx->libev->getLoop(), &watcher);
		}
	}

	// May only be called right after construction.
	OXT_FORCE_INLINE
	void setContext(Context *context) {
		Channel::setContext(context);
	}

	void reinitialize(int fd) {
		Channel::reinitialize();
		ev_io_init(&watcher, _onReadable, fd, EV_READ);
	}

	void deinitialize() {
		buffer = MemoryKit::mbuf();
		if (ev_is_active(&watcher)) {
			ev_io_stop(ctx->libev->getLoop(), &watcher);
		}
		watcher.fd = -1;
		consumedCallback = NULL;
		Channel::deinitialize();
	}

	// May only be called right after the constructor or reinitialize().
	void startReading() {
		startReadingInNextTick();
		onReadableWithoutRefGuard();
	}

	// May only be called right after the constructor or reinitialize().
	void startReadingInNextTick() {
		assert(Channel::acceptingInput());
		ev_io_start(ctx->libev->getLoop(), &watcher);
	}

	OXT_FORCE_INLINE
	void start() {
		Channel::start();
	}

	OXT_FORCE_INLINE
	void stop() {
		Channel::stop();
	}

	OXT_FORCE_INLINE
	void consumed(unsigned int size, bool end) {
		Channel::consumed(size, end);
	}

	OXT_FORCE_INLINE
	int getFd() const {
		return watcher.fd;
	}

	OXT_FORCE_INLINE
	State getState() const {
		return Channel::getState();
	}

	OXT_FORCE_INLINE
	bool isStarted() const {
		return Channel::isStarted();
	}

	OXT_FORCE_INLINE
	void setDataCallback(DataCallback callback) {
		Channel::dataCallback = callback;
	}

	OXT_FORCE_INLINE
	Hooks *getHooks() const {
		return hooks;
	}

	OXT_FORCE_INLINE
	void setHooks(Hooks *hooks) {
		this->hooks = hooks;
	}

	Json::Value inspectAsJson() const {
		Json::Value doc = Channel::inspectAsJson();
		doc["initialized"] = watcher.fd != -1;
		doc["io_watcher_active"] = (bool) watcher.active;
		return doc;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_FD_SOURCE_CHANNEL_H_ */
