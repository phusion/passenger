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
#ifndef _PASSENGER_SERVER_KIT_FD_CHANNEL_H_
#define _PASSENGER_SERVER_KIT_FD_CHANNEL_H_

#include <oxt/macros.hpp>
#include <oxt/system_calls.hpp>
#include <boost/move/move.hpp>
#include <ev.h>
#include <MemoryKit/mbuf.h>
#include <ServerKit/Context.h>
#include <ServerKit/Channel.h>

namespace Passenger {
namespace ServerKit {

using namespace oxt;


class FdChannel: protected Channel {
public:
	typedef int (*Callback)(FdChannel *channel, const MemoryKit::mbuf &buffer, int errcode);

private:
	ev_io watcher;
	MemoryKit::mbuf buffer;

	static void _onReadable(EV_P_ ev_io *io, int revents) {
		static_cast<FdChannel *>(io->data)->onReadable(io, revents);
	}

	void onReadable(ev_io *io, int revents) {
		RefGuard guard(hooks, this);
		unsigned int i;
		bool done = false;
		ssize_t ret;
		int e;

		for (i = 0; i < burstReadCount && !done; i++) {
			if (buffer.empty()) {
				buffer = MemoryKit::mbuf_get(&ctx->mbuf_pool);
			}

			ret = syscalls::read(io->fd, buffer.start, buffer.size());
			if (ret == -1) {
				e = errno;
				done = true;
				if (e != EAGAIN) {
					ev_io_stop(ctx->libev->getLoop(), &watcher);
					feedError(e);
				}
			} else if (ret == 0) {
				ev_io_stop(ctx->libev->getLoop(), &watcher);
				done = true;
				feed(MemoryKit::mbuf());
			} else {
				unsigned int generation = this->generation;
				MemoryKit::mbuf buffer2(buffer, 0, ret);
				buffer = MemoryKit::mbuf(buffer, ret);
				feed(boost::move(buffer2));
				if (generation != this->generation) {
					// Callback deinitialized this object.
					return;
				}
				if (!acceptingInput()) {
					ev_io_stop(ctx->libev->getLoop(), &watcher);
					idleCallback = onChannelIdle;
				}
				done = !acceptingInput()
					|| (size_t) ret < ctx->mbuf_pool.mbuf_block_chunk_size;
			}
		}
	}

	static int onChannelCallback(Channel *source, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		FdChannel *self = static_cast<FdChannel *>(source);
		return self->callback(self, buffer, errcode);
	}

	static void onChannelIdle(Channel *source) {
		FdChannel *self = static_cast<FdChannel *>(source);
		ev_io_start(self->ctx->libev->getLoop(), &self->watcher);
		self->idleCallback = NULL;
	}

	void initialize() {
		Channel::callback = onChannelCallback;
		burstReadCount = 1;
		watcher.data = this;
	}

public:
	Callback callback;
	unsigned int burstReadCount;

	FdChannel()
		: callback(NULL)
	{
		initialize();
	}

	FdChannel(Context *context)
		: Channel(context),
		  callback(NULL)
	{
		initialize();
	}

	~FdChannel() {
		ev_io_stop(ctx->libev->getLoop(), &watcher);
	}

	// May only be called right after construction.
	void setContext(Context *context) {
		Channel::setContext(context);
	}

	void reinitialize(int fd) {
		Channel::reinitialize();
		ev_io_init(&watcher, _onReadable, fd, EV_READ);
	}

	void deinitialize() {
		buffer = MemoryKit::mbuf();
		ev_io_stop(ctx->libev->getLoop(), &watcher);
		watcher.fd = -1;
		idleCallback = NULL;
		Channel::deinitialize();
	}

	// May only be called right after reinitialize().
	void startReading() {
		ev_io_start(ctx->libev->getLoop(), &watcher);
		onReadable(&watcher, EV_READ);
	}

	void start() {
		Channel::start();
	}

	void stop() {
		Channel::stop();
	}

	OXT_FORCE_INLINE
	int getFd() const {
		return watcher.fd;
	}

	OXT_FORCE_INLINE
	Hooks *getHooks() const {
		return hooks;
	}

	void setHooks(Hooks *hooks) {
		this->hooks = hooks;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_FD_CHANNEL_H_ */
