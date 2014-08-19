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
#include <oxt/system_calls.hpp>
#include <MemoryKit/mbuf.h>
#include <ServerKit/FileBufferedChannel.h>

namespace Passenger {
namespace ServerKit {


class FileBufferedFdOutputChannel: protected FileBufferedChannel {
public:
	typedef void (*ErrorCallback)(FileBufferedFdOutputChannel *channel, int errcode);
	typedef void (*BelowThresholdCallback)(FileBufferedFdOutputChannel *channel);
	typedef void (*FlushedCallback)(FileBufferedFdOutputChannel *channel);

private:
	ev_io watcher;

	static int onCallback(FileBufferedChannel *channel, const MemoryKit::mbuf &buffer, int errcode) {
		FileBufferedFdOutputChannel *self = static_cast<FileBufferedFdOutputChannel *>(channel);

		if (errcode != 0) {
			self->callOnError(errcode);
			return 0;
		} else {
			ssize_t ret = syscalls::write(self->watcher.fd, buffer.start, buffer.size());
			if (ret != -1) {
				return ret;
			} else if (errno == EAGAIN) {
				self->FileBufferedChannel::stop();
				ev_io_start(self->ctx->libev->getLoop(), &self->watcher);
				return 0;
			} else {
				errcode = errno;
				self->feedError(errcode);
				self->callOnError(errcode);
				return 0;
			}
		}
	}

	static void onWritable(EV_P_ ev_io *io, int revents) {
		FileBufferedFdOutputChannel *self = static_cast<FileBufferedFdOutputChannel *>(io->data);
		ev_io_stop(self->ctx->libev->getLoop(), &self->watcher);
		self->start();
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
		FileBufferedChannel::callback = onCallback;
		watcher.fd = -1;
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

	void reinitialize(int fd) {
		FileBufferedChannel::reinitialize();
		ev_io_init(&watcher, onWritable, fd, EV_WRITE);
	}

	void deinitialize() {
		ev_io_stop(ctx->libev->getLoop(), &watcher);
		watcher.fd = -1;
		FileBufferedChannel::deinitialize();
	}

	bool passedThreshold() const {
		return FileBufferedChannel::passedThreshold();
	}

	bool writing() const {
		return FileBufferedChannel::writing();
	}

	int getFd() const {
		return watcher.fd;
	}

	OXT_FORCE_INLINE
	Hooks *getHooks() const {
		return FileBufferedChannel::getHooks();
	}

	void setHooks(Hooks *hooks) {
		FileBufferedChannel::setHooks(hooks);
	}

	void setBelowThresholdCallback(BelowThresholdCallback callback) {
		FileBufferedChannel::belowThresholdCallback = (FileBufferedChannel::BelowThresholdCallback) callback;
	}

	FlushedCallback getFlushedCallback() const {
		return (FlushedCallback) FileBufferedChannel::flushedCallback;
	}

	void setFlushedCallback(FlushedCallback callback) {
		FileBufferedChannel::flushedCallback = (FileBufferedChannel::FlushedCallback) callback;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_FILE_BUFFERED_FD_OUTPUT_CHANNEL_H_ */
