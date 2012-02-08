#ifndef _EVENTED_BUFFERED_INPUT_H_
#define _EVENTED_BUFFERED_INPUT_H_

#include <cstddef>
#include <cassert>

#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <oxt/system_calls.hpp>
#include <ev++.h>

#include <SafeLibev.h>
#include <FileDescriptor.h>
#include <StaticString.h>

namespace Passenger {

using namespace boost;
using namespace oxt;

template<size_t bufferSize = 1024 * 8>
class EventedBufferedInput: public enable_shared_from_this< EventedBufferedInput<bufferSize> > {
private:
	enum State {
		LIVE,
		END_OF_STREAM,
		READ_ERROR,
		CLOSED
	};

	SafeLibev *libev;
	FileDescriptor fd;
	ev::io watcher;
	StaticString buffer;
	State state;
	bool paused;
	bool socketPaused;
	bool nextTickInstalled;
	int error;
	char bufferData[bufferSize];

	void onReadable(ev::io &watcher, int revents) {
		ssize_t ret = syscalls::read(fd, bufferData, bufferSize);
		if (ret == -1) {
			if (errno != EAGAIN) {
				error = errno;
				assert(state == LIVE);
				assert(!socketPaused);
				assert(buffer.empty());
				assert(!paused);

				watcher.stop();
				state = READ_ERROR;
				if (onError) {
					onError("Cannot read from socket", error);
				}
			}

		} else if (ret == 0) {
			assert(state == LIVE);
			assert(!socketPaused);
			assert(buffer.empty());
			assert(!paused);

			watcher.stop();
			state = END_OF_STREAM;
			onData(StaticString());

		} else {
			assert(state == LIVE);
			assert(!socketPaused);
			assert(buffer.empty());
			assert(!paused);

			buffer = StaticString(bufferData, ret);
			processBuffer();
		}
	}

	void processBufferInNextTick() {
		if (!nextTickInstalled) {
			nextTickInstalled = true;
			libev->runAsync(boost::bind(
				realProcessBufferInNextTick,
				weak_ptr< EventedBufferedInput<bufferSize> >(this->shared_from_this())
			));
		}
	}

	static void realProcessBufferInNextTick(weak_ptr< EventedBufferedInput<bufferSize> > wself) {
		shared_ptr< EventedBufferedInput<bufferSize> > self = wself.lock();
		if (self != NULL) {
			self->nextTickInstalled = false;
			self->processBuffer();
		}
	}

	void processBuffer() {
		if (state == CLOSED) {
			return;
		}
		assert(state == LIVE);
		if (paused || buffer.empty() || fd == -1) {
			return;
		}

		assert(buffer.size() > 0);

		size_t consumed = onData(buffer);
		if (state == CLOSED) {
			return;
		}
		if (consumed == buffer.size()) {
			buffer = StaticString();
			if (!paused && socketPaused) {
				socketPaused = false;
				watcher.start();
			}
		} else {
			buffer = buffer.substr(consumed);
			if (!socketPaused) {
				socketPaused = true;
				watcher.stop();
			}
			if (!paused) {
				// Consume rest of the data in the next tick.
				processBufferInNextTick();
			}
		}
	}

public:
	function<size_t (const StaticString &data)> onData;
	function<void (const char *message, int errnoCode)> onError;

	EventedBufferedInput() {
		reset(NULL, FileDescriptor());
		watcher.set<EventedBufferedInput<bufferSize>,
			&EventedBufferedInput<bufferSize>::onReadable>(this);
	}

	EventedBufferedInput(SafeLibev *libev, const FileDescriptor &fd) {
		reset(libev, fd);
		watcher.set<EventedBufferedInput<bufferSize>,
			&EventedBufferedInput<bufferSize>::onReadable>(this);
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
		buffer = StaticString();
		state = LIVE;
		paused = true;
		socketPaused = true;
		nextTickInstalled = false;
		error = 0;
		if (watcher.is_active()) {
			watcher.stop();
		}
		if (libev != NULL) {
			watcher.set(libev->getLoop());
		}
		if (fd != -1) {
			watcher.set(fd, ev::READ);
		}
	}

	void stop() {
		if (state == LIVE && !paused) {
			paused = true;
			if (!socketPaused) {
				socketPaused = true;
				watcher.stop();
			}
		}
	}

	void start() {
		if (state == LIVE && paused) {
			assert(socketPaused);
			
			paused = false;
			if (!buffer.empty()) {
				processBufferInNextTick();
			} else {
				socketPaused = false;
				watcher.start();
			}
		}
	}

	bool started() const {
		return !paused;
	}
};

typedef shared_ptr< EventedBufferedInput<> > EventedBufferedInputPtr;

} // namespace Passenger

#endif /* _EVENTED_BUFFERED_INPUT_H_ */
