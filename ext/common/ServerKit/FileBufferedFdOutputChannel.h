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
	int fd;

	static int onCallback(FileBufferedChannel *channel, const MemoryKit::mbuf &buffer, int errcode) {
		FileBufferedFdOutputChannel *self = static_cast<FileBufferedFdOutputChannel *>(channel);

		if (errcode != 0) {
			self->callOnError(errcode);
			return 0;
		} else {
			ssize_t ret = syscalls::write(self->fd, buffer.start, buffer.size());
			if (ret != -1) {
				return ret;
			} else {
				errcode = errno;
				self->feedError(errcode);
				self->callOnError(errcode);
				return 0;
			}
		}
	}

	void callOnError(int errcode) {
		if (errorCallback != NULL) {
			errorCallback(this, errcode);
		}
	}

public:
	ErrorCallback errorCallback;

	FileBufferedFdOutputChannel()
		: fd(-1),
		  errorCallback(NULL)
	{
		FileBufferedChannel::callback = onCallback;
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
		this->fd = fd;
	}

	void deinitialize() {
		fd = -1;
		FileBufferedChannel::deinitialize();
	}

	void start() {
		FileBufferedChannel::start();
	}

	void stop() {
		FileBufferedChannel::stop();
	}

	bool passedThreshold() const {
		return FileBufferedChannel::passedThreshold();
	}

	bool writing() const {
		return FileBufferedChannel::writing();
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

	void setFlushedCallback(FlushedCallback callback) {
		FileBufferedChannel::flushedCallback = (FileBufferedChannel::FlushedCallback) callback;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_FILE_BUFFERED_FD_OUTPUT_CHANNEL_H_ */
