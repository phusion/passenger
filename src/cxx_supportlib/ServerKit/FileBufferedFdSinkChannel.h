/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SERVER_KIT_FILE_BUFFERED_FD_SINK_CHANNEL_H_
#define _PASSENGER_SERVER_KIT_FILE_BUFFERED_FD_SINK_CHANNEL_H_

#include <oxt/macros.hpp>
#include <sys/types.h>
#include <unistd.h>
#include <LoggingKit/LoggingKit.h>
#include <MemoryKit/mbuf.h>
#include <ServerKit/FileBufferedChannel.h>

namespace Passenger {
namespace ServerKit {


class FileBufferedFdSinkChannel: protected FileBufferedChannel {
public:
	typedef void (*ErrorCallback)(FileBufferedFdSinkChannel *channel, int errcode);

private:
	ev_io watcher;

	static Channel::Result onDataCallback(Channel *channel, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		FileBufferedFdSinkChannel *self = static_cast<FileBufferedFdSinkChannel *>(channel);
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
				self->feedError(errcode, __FILE__, __LINE__);
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
		FileBufferedFdSinkChannel *self = static_cast<FileBufferedFdSinkChannel *>(io->data);
		ev_io_stop(self->ctx->libev->getLoop(), &self->watcher);
		self->consumed(0, false);
	}

	void callOnError(int errcode) {
		if (errorCallback) {
			errorCallback(this, errcode);
		}
	}

public:
	ErrorCallback errorCallback;

	FileBufferedFdSinkChannel()
		: errorCallback(NULL)
	{
		FileBufferedChannel::setDataCallback(onDataCallback);
		watcher.active = false;
		watcher.fd = -1;
		watcher.data = this;
	}

	~FileBufferedFdSinkChannel() {
		if (ev_is_active(&watcher)) {
			ev_io_stop(ctx->libev->getLoop(), &watcher);
		}
	}

	// May only be called right after construction.
	void setContext(Context *context) {
		FileBufferedChannel::setContext(context);
	}

	OXT_FORCE_INLINE
	void feed(const MemoryKit::mbuf &buffer) {
		FileBufferedChannel::feed(buffer);
	}

	OXT_FORCE_INLINE
	void feed(const char *data, unsigned int size) {
		FileBufferedChannel::feed(data, size);
	}

	OXT_FORCE_INLINE
	void feed(const char *data) {
		FileBufferedChannel::feed(data);
	}

	OXT_FORCE_INLINE
	void feedWithoutRefGuard(const MemoryKit::mbuf &buffer) {
		FileBufferedChannel::feedWithoutRefGuard(buffer);
	}

	OXT_FORCE_INLINE
	void feedWithoutRefGuard(const char *data, unsigned int size) {
		FileBufferedChannel::feedWithoutRefGuard(data, size);
	}

	void feedError(int errcode, const char *file = NULL, unsigned int line = 0) {
		FileBufferedChannel::feedError(errcode, file, line);
	}

	/**
	 * Reinitialize the channel without a file descriptor. The channel
	 * will be reinitialized in a stopped state. To start it, you must
	 * first call `setFd()`, then `start()`.
	 *
	 * @post getFd() == fd
	 */
	void reinitialize() {
		FileBufferedChannel::reinitialize();
		stop();
	}

	/**
	 * Reinitialize the channel with a file descriptor. Unlike `reinitialize()`,
	 * this reinitializes the channel in the started state.
	 *
	 * @post getFd() == fd
	 */
	void reinitialize(int fd) {
		FileBufferedChannel::reinitialize();
		setFd(fd);
	}

	void deinitialize() {
		if (ev_is_active(&watcher)) {
			ev_io_stop(ctx->libev->getLoop(), &watcher);
		}
		watcher.fd = -1;
		FileBufferedChannel::deinitialize();
	}

	void start() {
		FileBufferedChannel::start();
	}

	void stop() {
		FileBufferedChannel::stop();
	}

	Channel::State getState() const {
		return FileBufferedChannel::getState();
	}

	bool passedThreshold() const {
		return FileBufferedChannel::passedThreshold();
	}

	void setFd(int fd) {
		P_ASSERT_EQ(watcher.fd, -1);
		ev_io_init(&watcher, onWritable, fd, EV_WRITE);
	}

	int getFd() const {
		return watcher.fd;
	}

	OXT_FORCE_INLINE
	unsigned int getBytesBuffered() const {
		return FileBufferedChannel::getBytesBuffered();
	}

	OXT_FORCE_INLINE
	boost::uint64_t getBytesBufferedOnDisk() const {
		return FileBufferedChannel::getBytesBufferedOnDisk();
	}

	OXT_FORCE_INLINE
	boost::uint64_t getTotalBytesBuffered() const {
		return FileBufferedChannel::getTotalBytesBuffered();
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

	OXT_FORCE_INLINE
	void setHooks(Hooks *hooks) {
		FileBufferedChannel::setHooks(hooks);
	}

	OXT_FORCE_INLINE
	Callback getBuffersFlushedCallback() {
		return FileBufferedChannel::getBuffersFlushedCallback();
	}

	OXT_FORCE_INLINE
	void clearBuffersFlushedCallback() {
		FileBufferedChannel::clearBuffersFlushedCallback();
	}

	OXT_FORCE_INLINE
	void setBuffersFlushedCallback(Callback callback) {
		FileBufferedChannel::setBuffersFlushedCallback(callback);
	}

	OXT_FORCE_INLINE
	Callback getDataFlushedCallback() const {
		return FileBufferedChannel::getDataFlushedCallback();
	}

	OXT_FORCE_INLINE
	void setDataFlushedCallback(Callback callback) {
		FileBufferedChannel::setDataFlushedCallback(callback);
	}

	Json::Value inspectAsJson() const {
		return FileBufferedChannel::inspectAsJson();
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_FILE_BUFFERED_FD_SINK_CHANNEL_H_ */
