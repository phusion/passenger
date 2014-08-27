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
#ifndef _PASSENGER_SERVER_KIT_FILE_BUFFERED_FD_OUTPUT_CHANNEL_H_
#define _PASSENGER_SERVER_KIT_FILE_BUFFERED_FD_OUTPUT_CHANNEL_H_

#include <oxt/macros.hpp>
#include <sys/types.h>
#include <unistd.h>
#include <MemoryKit/mbuf.h>
#include <ServerKit/FileBufferedChannel.h>

namespace Passenger {
namespace ServerKit {


class FileBufferedFdOutputChannel: protected FileBufferedChannel {
public:
	typedef void (*ErrorCallback)(FileBufferedFdOutputChannel *channel, int errcode);

private:
	ev_io watcher;

	static Channel::Result onDataCallback(Channel *channel, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		FileBufferedFdOutputChannel *self = static_cast<FileBufferedFdOutputChannel *>(channel);
		// A RefGuard is not necessary here. Both Channel and FileBufferedChannel
		// install a RefGuard before calling this callback.

		if (buffer.size() > 0) {
			ssize_t ret;
			do {
				ret = ::write(self->watcher.fd, buffer.start, buffer.size());
			} while (OXT_UNLIKELY(ret == -1 && errno == EINTR));
			if (ret != -1) {
				return Channel::Result(ret, false);
			} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
				ev_io_start(self->ctx->libev->getLoop(), &self->watcher);
				return Channel::Result(-1, false);
			} else {
				errcode = errno;
				unsigned int generation = self->generation;
				self->feedError(errcode);
				if (generation != self->generation) {
					return Channel::Result(0, true);
				}
				self->callOnError(errcode);
				return Channel::Result(0, true);
			}
		} else if (errcode == 0) {
			return Channel::Result(0, false);
		} else {
			self->callOnError(errcode);
			return Channel::Result(0, false);
		}
	}

	static void onWritable(EV_P_ ev_io *io, int revents) {
		FileBufferedFdOutputChannel *self = static_cast<FileBufferedFdOutputChannel *>(io->data);
		ev_io_stop(self->ctx->libev->getLoop(), &self->watcher);
		self->consumed(0, false);
	}

	void callOnError(int errcode) {
		if (errorCallback != NULL) {
			errorCallback(this, errcode);
		}
	}

public:
	ErrorCallback errorCallback;

	FileBufferedFdOutputChannel()
		: errorCallback(NULL)
	{
		FileBufferedChannel::setDataCallback(onDataCallback);
		watcher.fd = -1;
		watcher.data = this;
	}

	~FileBufferedFdOutputChannel() {
		ev_io_stop(ctx->libev->getLoop(), &watcher);
	}

	// May only be called right after construction.
	void setContext(Context *context) {
		FileBufferedChannel::setContext(context);
	}

	void feed(const MemoryKit::mbuf &buffer) {
		FileBufferedChannel::feed(buffer);
	}

	void feed(const char *data, unsigned int size) {
		FileBufferedChannel::feed(data, size);
	}

	void feed(const char *data) {
		FileBufferedChannel::feed(data);
	}

	void feedError(int errcode) {
		FileBufferedChannel::feedError(errcode);
	}

	void reinitialize(int fd) {
		FileBufferedChannel::reinitialize();
		ev_io_init(&watcher, onWritable, fd, EV_WRITE);
	}

	void deinitialize() {
		ev_io_stop(ctx->libev->getLoop(), &watcher);
		watcher.fd = -1;
		FileBufferedChannel::deinitialize();
	}

	Channel::State getState() const {
		return FileBufferedChannel::getState();
	}

	bool passedThreshold() const {
		return FileBufferedChannel::passedThreshold();
	}

	int getFd() const {
		return watcher.fd;
	}

	OXT_FORCE_INLINE
	bool ended() const {
		return FileBufferedChannel::ended();
	}

	OXT_FORCE_INLINE
	bool endAcked() const {
		return FileBufferedChannel::endAcked();
	}

	OXT_FORCE_INLINE
	Hooks *getHooks() const {
		return FileBufferedChannel::getHooks();
	}

	void setHooks(Hooks *hooks) {
		FileBufferedChannel::setHooks(hooks);
	}

	void setBuffersFlushedCallback(Callback callback) {
		FileBufferedChannel::setBuffersFlushedCallback(callback);
	}

	void setDataFlushedCallback(Callback callback) {
		FileBufferedChannel::setDataFlushedCallback(callback);
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_FILE_BUFFERED_FD_OUTPUT_CHANNEL_H_ */
