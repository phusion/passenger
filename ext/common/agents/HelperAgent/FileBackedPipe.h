#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_array.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <string>
#include <sstream>
#include <memory>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <unistd.h>
#include <pthread.h>

#include <oxt/macros.hpp>
#include <SafeLibev.h>
#include <MultiLibeio.h>
#include <StaticString.h>
#include <Exceptions.h>
#include <FileDescriptor.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {

using namespace std;
using namespace boost;


/**
 * A pipe which buffers data in memory, or if the data becomes too large, to disk.
 * If you write some data to the pipe then the pipe will push some data to the
 * 'onData' callback. This callback is allowed to take an arbitrary amount of time
 * to consume the data. The pipe guarantees that, while the callback is busy
 * consuming data, any newly written data will be buffered, either to memory
 * or to disk. Thus, one can write a virtually unlimited amount of data into
 * the pipe without filling up the system's RAM, even when the data is slowly
 * consumed. FileBackedPipe is highly optimized: in case the 'onData' callback
 * is fast enough, FileBackedPipe operates in an entirely zero-copy manner and
 * without any kinds of heap allocations.
 *
 * FileBackedPipe assumes the usage of an event loop. It is *not* thread-safe!
 * All FileBackedPipe methods may only be called from the event loop on which it
 * is installed.
 *
 * FileBackedPipe *must* be dynamically allocated and assigned to a shared_ptr.
 */
class FileBackedPipe: public enable_shared_from_this<FileBackedPipe> {
public:
	class ConsumeCallback {
	private:
		mutable weak_ptr<FileBackedPipe> wself;

	public:
		ConsumeCallback() { }

		ConsumeCallback(const shared_ptr<FileBackedPipe> &self)
			: wself(self)
			{ }

		void operator()(size_t consumed, bool done) const {
			shared_ptr<FileBackedPipe> self = wself.lock();
			if (self != NULL) {
				wself.reset();
				self->dataConsumed(consumed, done);
			}
		}

		function<void (size_t, bool)> toFunction() const {
			shared_ptr<FileBackedPipe> self = wself.lock();
			if (self != NULL) {
				return boost::bind(&ConsumeCallback::operator(), this, _1, _2);
			} else {
				return function<void (size_t, bool)>();
			}
		}
	};

	typedef function<void (const char *data, size_t size, const ConsumeCallback &consumed)> DataCallback;
	//typedef function<void (const ExceptionPtr &exception)> ErrorCallback;
	typedef function<void ()> Callback;

	enum DataState {
		IN_MEMORY,
		OPENING_FILE,
		IN_FILE
	};

private:
	typedef function<void (int err, const char *data, size_t size)> EioReadCallback;

	// We already have a shared_ptr reference to libev through MultiLibeio.
	SafeLibev *libev;
	const string dir;
	size_t threshold;

	const char *currentData;
	size_t currentDataSize;
	MultiLibeio libeio;
	unsigned int consumedCallCount;

	bool started;
	bool ended;
	bool endReached;

	enum {
		/* No data event handler is currently being called. */
		NOT_CALLING_EVENT,
		/* The data event handler is currently being called and it hasn't returned yet. */
		CALLING_EVENT_NOW,
		/* The data event handler was called and it returned, but it hasn't called its finish callback yet. */
		WAITING_FOR_EVENT_FINISH,
		/* The data event handler finish callback has been called and is
		 * fetching more buffered data so that it can call the data event
		 * handler again.
		 */
		PREPARING_NEXT_EVENT_CALL
	} dataEventState;

	DataState dataState;

	struct {
		char *data;
		size_t size;
	} memory;
	struct {
		FileDescriptor fd;
		/* Whether there's currently an operation in progress to write the contents
		 * of the buffer to the file. */
		bool writingToFile;
		/* Number of bytes written to the file so far. This number is incremented
		 * *after* the file write operation has finished, not before. */
		off_t writtenSize;
		/* Offset in the file at which data should be read. This can be
		 * temporarily larger than 'writtenSize'. If this is the case then
		 * the data with offset past 'writtenSize' should be obtained from
		 * the writeBuffer.
		 */
		off_t readOffset;
		/* Data buffered in memory, to be written to the file ASAP. Data is
		 * removed from the buffer *after* the file write operation has
		 * finished, not before.
		 */
		string writeBuffer;
	} file;

	bool callOnData(const char *data, size_t size, bool passDataToConsumedCallback) {
		unsigned int oldConsumedCallCount = consumedCallCount;
		dataEventState = CALLING_EVENT_NOW;

		assert(currentData == NULL);
		assert(currentDataSize == 0);
		if (passDataToConsumedCallback) {
			currentData = data;
		}
		currentDataSize = size;

		if (OXT_LIKELY(onData != NULL)) {
			onData(data, size, ConsumeCallback(shared_from_this()));
		} else {
			real_dataConsumed(0, true);
		}

		if (consumedCallCount == oldConsumedCallCount) {
			// 'consumed' callback not called.
			dataEventState = WAITING_FOR_EVENT_FINISH;
			return false;
		} else {
			// 'consumed' callback called.
			assert(dataEventState == NOT_CALLING_EVENT
				|| dataEventState == PREPARING_NEXT_EVENT_CALL);
			return true;
		}
	}

	void callOnEnd() {
		endReached = true;
		if (onEnd) {
			onEnd();
		}
	}

	SafeLibev *getLibev() const {
		return libeio.getLibev().get();
	}

	void addToBuffer(const char *data, size_t size) {
		size_t bytesToCopy;

		switch (dataState) {
		case IN_MEMORY:
			bytesToCopy = std::min(size, threshold - memory.size);
			if (bytesToCopy == size) {
				if (memory.data == NULL) {
					assert(memory.size == 0);
					memory.data = new char[threshold];
				}
				memcpy(memory.data + memory.size, data, bytesToCopy);
				memory.size += size;
			} else {
				dataState = OPENING_FILE;
				assert(file.fd == -1);
				assert(file.writtenSize == 0);
				assert(file.readOffset == 0);
				file.writeBuffer.reserve(memory.size + size);
				file.writeBuffer.append(memory.data, memory.size);
				file.writeBuffer.append(data, size);
				delete[] memory.data;
				memory.data = NULL;
				memory.size = 0;

				stringstream filename;
				filename << dir;
				filename << "/buffer.";
				filename << getpid();
				filename << ".";
				filename << pointerToIntString(this);
				libeio.open(filename.str().c_str(), O_CREAT | O_RDWR | O_TRUNC, 0, 0,
					boost::bind(&FileBackedPipe::openCallback, this,
						_1, filename.str(),
						weak_ptr<FileBackedPipe>(shared_from_this())
					)
				);
			}
			break;
		
		case OPENING_FILE:
			file.writeBuffer.append(data, size);
			break;

		case IN_FILE:
			file.writeBuffer.append(data, size);
			writeBufferToFile();
			break;
		
		default:
			abort();
		}
	}

	void writeBufferToFile() {
		assert(dataState == IN_FILE);
		if (!file.writingToFile) {
			shared_array<char> buffer(new char[file.writeBuffer.size()]);
			memcpy(buffer.get(), file.writeBuffer.data(), file.writeBuffer.size());
			file.writingToFile = true;
			libeio.write(file.fd, buffer.get(), file.writeBuffer.size(),
				file.writtenSize, 0, boost::bind(
					&FileBackedPipe::writeBufferToFileCallback, this,
					_1, file.fd, buffer, file.writeBuffer.size(),
					weak_ptr<FileBackedPipe>(shared_from_this())
				)
			);
		}
	}

	void writeBufferToFileCallback(eio_req req, FileDescriptor fd,
		shared_array<char> buffer, size_t size, weak_ptr<FileBackedPipe> wself)
	{
		shared_ptr<FileBackedPipe> self = wself.lock();
		if (self == NULL || EIO_CANCELLED(&req)) {
			return;
		}

		if (req.result < 0) {
			// TODO: set error
		} else {
			assert(dataState == IN_FILE);
			file.writeBuffer.erase(0, size);
			file.writtenSize += size;
			file.writingToFile = false;
			if (!file.writeBuffer.empty()) {
				writeBufferToFile();
			}
		}
	}

	void openCallback(eio_req req, string filename, weak_ptr<FileBackedPipe> &wself) {
		shared_ptr<FileBackedPipe> self = wself.lock();
		if (self == NULL || EIO_CANCELLED(&req)) {
			if (req.result != -1 || EIO_CANCELLED(&req)) {
				eio_close(req.result, 0, successCallback, NULL);
				eio_unlink(filename.c_str(), 0, successCallback, NULL);
			}
			return;
		}

		assert(dataState = OPENING_FILE);
		if (req.result < 0) {
			// TODO: set error
		} else {
			eio_unlink(filename.c_str(), 0, successCallback, NULL);
			if (openTimeout == 0) {
				finalizeOpenFile(req.result);
			} else {
				getLibev()->runAfter(openTimeout,
					boost::bind(&FileBackedPipe::finalizeOpenFileAfterTimeout, this,
						weak_ptr<FileBackedPipe>(shared_from_this()), FileDescriptor(req.result)));
			}
		}
	}

	void finalizeOpenFile(const FileDescriptor &fd) {
		dataState = IN_FILE;
		file.fd = fd;
		writeBufferToFile();
	}

	void finalizeOpenFileAfterTimeout(weak_ptr<FileBackedPipe> wself, FileDescriptor fd) {
		shared_ptr<FileBackedPipe> self = wself.lock();
		if (self != NULL) {
			self->finalizeOpenFile(fd);
		}
	}

	static int successCallback(eio_req *req) {
		return 0;
	}

	void readBlockFromFileOrWriteBuffer(const EioReadCallback &callback) {
		if (file.readOffset >= file.writtenSize) {
			StaticString data = StaticString(file.writeBuffer).substr(
				file.readOffset - file.writtenSize, 1024 * 16);
			callback(0, data.data(), data.size());
		} else {
			shared_array<char> buffer(new char[1024 * 16]);
			eio_req *req = libeio.read(file.fd, buffer.get(), 1024 * 16, file.readOffset, 0,
				boost::bind(
					&FileBackedPipe::readCallback, this,
					_1, file.fd, buffer, callback,
					weak_ptr<FileBackedPipe>(shared_from_this())
				)
			);
			if (req == NULL) {
				throw RuntimeException("eio_read() failed!");
			}
		}
	}

	void readCallback(eio_req req, FileDescriptor fd, shared_array<char> buffer,
		EioReadCallback callback, weak_ptr<FileBackedPipe> wself)
	{
		shared_ptr<FileBackedPipe> self = wself.lock();
		if (self == NULL || EIO_CANCELLED(&req)) {
			return;
		}

		if (req.result < 0) {
			callback(req.errorno, NULL, 0);
		} else {
			callback(0, buffer.get(), req.result);
		}
	}

	void dataConsumed(size_t consumed, bool done) {
		if (pthread_equal(pthread_self(), getLibev()->getCurrentThread())) {
			real_dataConsumed(consumed, done);
		} else {
			getLibev()->runAsync(boost::bind(
				&FileBackedPipe::real_dataConsumed, this,
				consumed, done));
		}
	}

	void real_dataConsumed(size_t consumed, bool done) {
		const char *data = currentData;
		size_t size = currentDataSize;
		currentData = NULL;
		currentDataSize = 0;

		assert(consumed <= size);
		consumedCallCount++;
		if (done) {
			started = false;
		}

		if (getBufferSize() == 0) {
			// Data passed to write() was immediately consumed.
			assert(dataEventState == CALLING_EVENT_NOW);
			assert(data != NULL);
			if (started) {
				if (consumed < size) {
					bool immediatelyConsumed = callOnData(
						data + consumed,
						size - consumed,
						true);
					if (!immediatelyConsumed) {
						addToBuffer(data + consumed, size - consumed);
					}
				} else {
					dataEventState = NOT_CALLING_EVENT;
					if (ended) {
						callOnEnd();
					}
				}
			} else {
				dataEventState = NOT_CALLING_EVENT;
				addToBuffer(data + consumed, size - consumed);
			}

		} else {
			/* Data passed to write() was either immediately consumed or was consumed later,
			 * but we don't care which of those situations occurred: the consumed data is
			 * in the buffer and we have to erase it.
			 */
			processBuffer(consumed);
		}
	}

	void processBuffer(size_t consumed) {
		assert(getBufferSize() > 0);

		dataEventState = NOT_CALLING_EVENT;

		switch (dataState) {
		case IN_MEMORY:
			memmove(memory.data, memory.data + consumed, memory.size - consumed);
			memory.size -= consumed;
			if (started) {
				if (memory.size == 0) {
					if (onBufferDrained) {
						onBufferDrained();
					}
					if (ended) {
						callOnEnd();
					}
				} else {
					callOnData(memory.data, memory.size, false);
				}
			}
			break;

		case OPENING_FILE:
		case IN_FILE:
			file.readOffset += consumed;
			if (started) {
				if (getBufferSize() == 0) {
					if (onBufferDrained) {
						onBufferDrained();
					}
					if (ended) {
						callOnEnd();
					}
				} else {
					dataEventState = PREPARING_NEXT_EVENT_CALL;
					readBlockFromFileOrWriteBuffer(boost::bind(
						&FileBackedPipe::processBuffer_readCallback, this,
						_1, _2, _3));
				}
			}
			break;
		
		default:
			abort();
		}
	}

	void processBuffer_readCallback(int err, const char *data, size_t size) {
		if (err != 0) {
			// TODO: set error
		} else {
			callOnData(data, size, false);
		}
	}

public:
	DataCallback onData;
	Callback onEnd;
	//ErrorCallback onError;
	Callback onBufferDrained;

	// The amount of time, in milliseconds, that the open() operation
	// should at least take before it finishes. For unit testing purposes.
	unsigned int openTimeout;

	FileBackedPipe(const SafeLibevPtr &_libev, const string &_dir, size_t _threshold = 1024 * 8)
		: dir(_dir),
		  threshold(_threshold),
		  libeio(_libev),
		  openTimeout(0)
	{
		consumedCallCount = 0;
		currentData = NULL;
		currentDataSize = 0;
		started = false;
		ended = false;
		endReached = false;
		dataEventState = NOT_CALLING_EVENT;
		dataState = IN_MEMORY;
		memory.data = NULL;
		memory.size = 0;
		file.writingToFile = false;
		file.readOffset = 0;
		file.writtenSize = 0;
	}

	~FileBackedPipe() {
		delete[] memory.data;
	}
	
	bool resetable() const {
		return dataState == IN_MEMORY;
	}

	void reset(const SafeLibevPtr &_libev) {
		if (OXT_UNLIKELY(dataEventState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");
		}
		assert(resetable());
		libeio = MultiLibeio(_libev);
		currentData = NULL;
		currentDataSize = 0;
		started = false;
		ended = false;
		endReached = false;
		dataEventState = NOT_CALLING_EVENT;
		dataState = IN_MEMORY;
		delete[] memory.data;
		memory.data = NULL;
		memory.size = 0;
		file.fd = FileDescriptor();
		file.writingToFile = false;
		file.readOffset = 0;
		file.writtenSize = 0;
	}

	void setThreshold(size_t value) {
		threshold = value;
	}

	size_t getBufferSize() const {
		switch (dataState) {
		case IN_MEMORY:
			return memory.size;

		case OPENING_FILE:
		case IN_FILE:
			return (ssize_t) file.writtenSize
				- file.readOffset
				+ file.writeBuffer.size();
		
		default:
			abort();
		}
	}

	DataState getDataState() const {
		return dataState;
	}

	bool write(const char *data, size_t size) {
		assert(!ended);

		if (OXT_UNLIKELY(dataEventState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");

		} else if (!started || dataEventState != NOT_CALLING_EVENT) {
			assert(!started || getBufferSize() > 0);
			addToBuffer(data, size);
			return false;

		} else {
			assert(started);
			assert(dataEventState == NOT_CALLING_EVENT);
			assert(getBufferSize() == 0);

			bool immediatelyConsumed = callOnData(data, size, true);
			assert(dataEventState != CALLING_EVENT_NOW);
			if (!immediatelyConsumed) {
				addToBuffer(data, size);
			}
			return immediatelyConsumed;
		}
	}

	void end() {
		assert(!ended);

		if (OXT_UNLIKELY(dataEventState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");

		} else if (!started || dataEventState != NOT_CALLING_EVENT) {
			assert(!started || getBufferSize() > 0);
			ended = true;

		} else {
			assert(started);
			assert(dataEventState == NOT_CALLING_EVENT);
			assert(getBufferSize() == 0);

			ended = true;
			callOnEnd();
		}
	}

	bool isStarted() const {
		return started;
	}

	bool reachedEnd() const {
		return endReached;
	}

	void start() {
		if (OXT_UNLIKELY(dataEventState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");
		}
		if (!started && !endReached) {
			started = true;
			if (dataEventState == NOT_CALLING_EVENT) {
				if (getBufferSize() > 0) {
					processBuffer(0);
				} else if (ended) {
					callOnEnd();
				}
			}
		}
	}

	void stop() {
		if (OXT_UNLIKELY(dataEventState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");
		}
		started = false;
	}
};

typedef shared_ptr<FileBackedPipe> FileBackedPipePtr;


} // namespace Passenger
