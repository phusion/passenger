#ifndef _EVENTED_BUFFERED_INPUT_H_
#define _EVENTED_BUFFERED_INPUT_H_

#include <cstddef>
#include <cassert>

#include <boost/bind.hpp>
#include <boost/shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <oxt/system_calls.hpp>
#include <ev++.h>

#include <FileDescriptor.h>
#include <StaticString.h>

namespace Passenger {

using namespace boost;
using namespace oxt;

template<size_t bufferSize = 1024 * 8>
class EventedBufferedInput: public enable_shared_from_this<EventedBufferedInput> {
private:
	SafeLibev *libev;
	FileDescriptor fd;
	ev::io watcher;
	StaticString unconsumed;
	bool stopped;
	bool resumeAfterProcessingEvents;
	bool nextTickInstalled;
	bool eof;
	int  error;
	char buffer[bufferSize];

	void onReadable(ev::io &watcher, int revents) {
		assert(!nextTickInstalled);
		assert(unconsumed.empty());
		ssize_t ret = syscalls::read(fd, buffer, sizeof(buffer));
		if (ret == -1) {
			if (errno != EAGAIN) {
				error = errno;
				if (unconsumed.empty()) {
					onError("Cannot read from socket", error);
				}
			}
		} else if (ret == 0) {
			eof = true;
			if (unconsumed.empty()) {
				onData(StaticString());
			}
		} else {
			unconsumed = StaticString(buffer, ret);
			processEvents();
		}
	}

	void processEventsInNextTick() {
		if (!nextTickInstalled) {
			nextTickInstalled = true;
			libev->runAsync(boost::bind(
				realProcessEventsInNextTick,
				weak_ptr< EventedBufferedInput<bufferSize> >(shared_from_this())
			));
		}
	}

	static void realProcessEventsInNextTick(weak_ptr< EventedBufferedInput<bufferSize> > wself) {
		shared_ptr< EventedBufferedInput<bufferSize> > self = wself.lock();
		if (self != NULL) {
			self->nextTickInstalled = false;
			self->processEvents();
		}
	}

	void processEvents() {
		if (stopped && !resumeAfterProcessingEvents) {
			return;
		}

		if (!unconsumed.empty()) {
			assert(!unconsumed.empty());

			size_t consumed = onData(unconsumed);
			if (consumed == unconsumed.size()) {
				unconsumed = StaticString();
				if (eof) {
					onEnd();
				} else if (error != 0) {
					onError("Cannot read from socket", error);
				} else if (resumeAfterProcessingEvents) {
					stopped = false;
					resumeAfterProcessingEvents = false;
					watcher.start();
				}
			} else {
				unconsumed = unconsumed.substr(consumed);
				if (!stopped) {
					stopped = true;
					resumeAfterProcessingEvents = true;
					watcher.stop();
					processEventsInNextTick();
				} else if (resumeAfterProcessingEvents) {
					processEventsInNextTick();
				}
			}
		} else if (eof) {
			this.onEnd();
		} else if (error != 0) {
			onError("Cannot read from socket", error);
		}
	}

public:
	function<size_t (const StaticString &data)> onData;
	function<void (const char *message, int errnoCode)> onError;

	EventedBufferedInput() {
		reset(NULL, FileDescriptor());
		watcher.set<EventedBufferedInput<bufferSize>,
			EventedBufferedInput<bufferSize>::onReadable>(this);
	}

	EventedBufferedInput(SafeLibev *libev, const FileDescriptor &fd) {
		reset(libev, fd);
		watcher.set<EventedBufferedInput<bufferSize>,
			EventedBufferedInput<bufferSize>::onReadable>(this);
	}

	~EventedBufferedInput() {
		watcher.stop();
	}

	bool resetable() const {
		return !nextTickInstalled;
	}

	void reset(SafeLibev *libev, const FileDescriptor &fd) {
		this->libev = libev;
		this->fd = fd;
		unconsumed = StaticString();
		stopped = true;
		resumeAfterProcessingEvents = false;
		nextTickInstalled = false;
		eof = false;
		error = 0;
		if (watcher.started()) {
			watcher.stop();
		}
		if (libev != NULL) {
			watcher.set(libev->getLoop());
		}
	}

	void stop() {
		stopped = true;
		resumeAfterProcessingEvents = false;
		watcher.stop();
	}

	void start() {
		if (!unconsumed.empty()) {
			resumeAfterProcessingEvents = true;
			processEventsInNextTick();
		} else {
			stopped = false;
			watcher.start();
		}
	}

	bool started() const {
		return !stopped;
	}
};

typedef shared_ptr< EventedBufferedInput<> > EventedBufferedInputPtr;

} // namespace Passenger

#endif /* _EVENTED_BUFFERED_INPUT_H_ */
