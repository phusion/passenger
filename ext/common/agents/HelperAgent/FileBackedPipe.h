/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011, 2012 Phusion
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
#ifndef _PASSENGER_FILE_BACKED_PIPE_H_
#define _PASSENGER_FILE_BACKED_PIPE_H_

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
 * By default, FileBackedPipe is stopped, meaning that when you write to it,
 * the data will be buffered and the 'onData' callback will not be called.
 * You must start it by calling start().
 *
 * When you're done writing data to the pipe, call end() to signal end-of-stream.
 * Once all buffered data has been consumed, the 'onEnd' callback will be called.
 *
 * FileBackedPipe assumes the usage of an event loop. It is *not* thread-safe!
 * All FileBackedPipe methods may only be called from the event loop on which it
 * is installed.
 *
 * FileBackedPipe *must* be dynamically allocated and assigned to a boost::shared_ptr.
 */
class FileBackedPipe: public boost::enable_shared_from_this<FileBackedPipe> {
public:
	class ConsumeCallback {
	private:
		mutable boost::weak_ptr<FileBackedPipe> wself;
		unsigned int generation;

	public:
		ConsumeCallback() { }

		ConsumeCallback(const boost::shared_ptr<FileBackedPipe> &self, unsigned int _generation)
			: wself(self),
			  generation(_generation)
			{ }

		void operator()(size_t consumed, bool done) const {
			boost::shared_ptr<FileBackedPipe> self = wself.lock();
			if (self != NULL) {
				wself.reset();
				self->dataConsumed(consumed, done, generation);
			}
		}

		boost::function<void (size_t, bool)> toFunction() const {
			boost::shared_ptr<FileBackedPipe> self = wself.lock();
			if (self != NULL) {
				return boost::bind(&ConsumeCallback::operator(), this, _1, _2);
			} else {
				return boost::function<void (size_t, bool)>();
			}
		}
	};

	typedef void (*DataCallback)(const boost::shared_ptr<FileBackedPipe> &source, const char *data,
		size_t size, const ConsumeCallback &consumed);
	typedef void (*ErrorCallback)(const boost::shared_ptr<FileBackedPipe> &source, int errorCode);
	typedef void (*Callback)(const boost::shared_ptr<FileBackedPipe> &source);

	enum DataState {
		IN_MEMORY,
		OPENING_FILE,
		IN_FILE
	};

private:
	typedef boost::function<void (int err, const char *data, size_t size)> EioReadCallback;

	// We already have a boost::shared_ptr reference to libev through MultiLibeio.
	const string dir;
	size_t threshold;

	const char *currentData;
	size_t currentDataSize;
	MultiLibeio libeio;
	unsigned int consumedCallCount;
	unsigned int generation;

	bool started;
	bool ended;
	bool endReached;
	bool hasError;

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
			onData(shared_from_this(), data, size, ConsumeCallback(shared_from_this(),
				generation));
		} else {
			real_dataConsumed(0, true, generation);
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
		assert(!endReached);
		endReached = true;
		if (onEnd != NULL) {
			onEnd(shared_from_this());
		}
	}

	void callOnCommit() {
		if (onCommit != NULL) {
			onCommit(shared_from_this());
		}
	}

	void setError(int errorCode) {
		hasError = true;
		if (onError != NULL) {
			onError(shared_from_this(), errorCode);
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
						_1, filename.str(), generation,
						boost::weak_ptr<FileBackedPipe>(shared_from_this())
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
					generation,
					boost::weak_ptr<FileBackedPipe>(shared_from_this())
				)
			);
		}
	}

	void writeBufferToFileCallback(eio_req req, FileDescriptor fd,
		shared_array<char> buffer, size_t size,
		unsigned int generation, boost::weak_ptr<FileBackedPipe> wself)
	{
		boost::shared_ptr<FileBackedPipe> self = wself.lock();
		if (self == NULL || EIO_CANCELLED(&req) || generation != self->generation) {
			return;
		}

		if (req.result < 0) {
			setError(req.errorno);
		} else {
			assert(dataState == IN_FILE);
			file.writeBuffer.erase(0, size);
			file.writtenSize += size;
			file.writingToFile = false;
			if (file.writeBuffer.empty()) {
				callOnCommit();
			} else {
				writeBufferToFile();
			}
		}
	}

	void openCallback(eio_req req, string filename, unsigned int generation,
		boost::weak_ptr<FileBackedPipe> &wself)
	{
		boost::shared_ptr<FileBackedPipe> self = wself.lock();
		if (self == NULL || EIO_CANCELLED(&req) || generation != self->generation) {
			if (req.result != -1 || EIO_CANCELLED(&req)) {
				eio_close(req.result, 0, successCallback, NULL);
				eio_unlink(filename.c_str(), 0, successCallback, NULL);
			}
			return;
		}

		assert(dataState == OPENING_FILE);
		if (req.result < 0) {
			setError(req.errorno);
		} else {
			eio_unlink(filename.c_str(), 0, successCallback, NULL);
			if (openTimeout == 0) {
				finalizeOpenFile(FileDescriptor(req.result));
			} else {
				getLibev()->runAfter(openTimeout,
					boost::bind(&FileBackedPipe::finalizeOpenFileAfterTimeout, this,
						boost::weak_ptr<FileBackedPipe>(shared_from_this()),
						generation, FileDescriptor(req.result)));
			}
		}
	}

	void finalizeOpenFile(const FileDescriptor &fd) {
		dataState = IN_FILE;
		file.fd = fd;
		writeBufferToFile();
	}

	void finalizeOpenFileAfterTimeout(boost::weak_ptr<FileBackedPipe> wself,
		unsigned int generation, FileDescriptor fd)
	{
		boost::shared_ptr<FileBackedPipe> self = wself.lock();
		if (self != NULL || generation != self->generation) {
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
					_1, file.fd, buffer, callback, generation,
					boost::weak_ptr<FileBackedPipe>(shared_from_this())
				)
			);
			if (req == NULL) {
				throw RuntimeException("eio_read() failed!");
			}
		}
	}

	void readCallback(eio_req req, FileDescriptor fd, shared_array<char> buffer,
		EioReadCallback callback, unsigned int generation, boost::weak_ptr<FileBackedPipe> wself)
	{
		boost::shared_ptr<FileBackedPipe> self = wself.lock();
		if (self == NULL || EIO_CANCELLED(&req) || generation != self->generation) {
			return;
		}

		if (req.result < 0) {
			callback(req.errorno, NULL, 0);
		} else {
			callback(0, buffer.get(), req.result);
		}
	}

	void dataConsumed(size_t consumed, bool done, unsigned int oldGeneration) {
		if (OXT_UNLIKELY(oldGeneration != generation)) {
			throw RuntimeException("Don't call the consumed callback after you've reset the FileBackedPipe!");
		}

		if (pthread_equal(pthread_self(), getLibev()->getCurrentThread())) {
			real_dataConsumed(consumed, done, oldGeneration);
		} else {
			getLibev()->runLater(boost::bind(
				&FileBackedPipe::real_dataConsumed, this,
				consumed, done, oldGeneration));
		}
	}

	void real_dataConsumed(size_t consumed, bool done, unsigned int oldGeneration) {
		if (OXT_UNLIKELY(oldGeneration != generation)) {
			throw RuntimeException("Don't call the consumed callback after you've reset the FileBackedPipe!");
		}

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
					unsigned int oldGeneration = generation;
					bool immediatelyConsumed = callOnData(
						data + consumed,
						size - consumed,
						true);
					if (generation == oldGeneration && !immediatelyConsumed) {
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
					//callOnConsumed();
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
					//callOnConsumed();
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
			setError(err);
		} else {
			callOnData(data, size, false);
		}
	}

public:
	DataCallback onData;
	Callback onEnd;
	ErrorCallback onError;
	Callback onCommit;
	void *userData;

	// The amount of time, in milliseconds, that the open() operation
	// should at least take before it finishes. For unit testing purposes.
	unsigned int openTimeout;

	FileBackedPipe(const string &_dir, size_t _threshold = 1024 * 8)
		: dir(_dir),
		  threshold(_threshold),
		  openTimeout(0)
	{
		onData = NULL;
		onEnd = NULL;
		onError = NULL;
		onCommit = NULL;

		consumedCallCount = 0;
		generation = 0;
		currentData = NULL;
		currentDataSize = 0;
		started = false;
		ended = false;
		endReached = false;
		hasError = false;
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
		//return dataState == IN_MEMORY;
		return true;
	}

	void reset(const SafeLibevPtr &libev = SafeLibevPtr()) {
		generation++;
		libeio = MultiLibeio(libev);
		currentData = NULL;
		currentDataSize = 0;
		started = false;
		ended = false;
		endReached = false;
		hasError = false;
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

	/**
	 * Returns the amount of data that has been buffered, both in memory and on disk.
	 */
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

	/**
	 * Writes the given data to the pipe. Returns whether all data is immediately
	 * consumed by the 'onData' callback or whether the data buffered into a memory buffer.
	 * That is, if the data is not immediately consumed and it is queued to be written
	 * to disk, then false is returned. In the latter case, the 'onCommit' callback is
	 * called when all buffered data has been written to disk.
	 *
	 * Note that this method may invoke the 'onData' callback immediately.
	 */
	bool write(const char *data, size_t size) {
		assert(!ended);
		assert(!hasError);

		if (OXT_UNLIKELY(dataEventState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");

		} else if (!started || dataEventState != NOT_CALLING_EVENT) {
			assert(!started || getBufferSize() > 0);
			addToBuffer(data, size);
			return dataState == IN_MEMORY;

		} else {
			assert(started);
			assert(dataEventState == NOT_CALLING_EVENT);
			assert(getBufferSize() == 0);

			unsigned int oldGeneration = generation;
			bool immediatelyConsumed = callOnData(data, size, true);
			if (generation == oldGeneration) {
				assert(dataEventState != CALLING_EVENT_NOW);
				if (!immediatelyConsumed) {
					addToBuffer(data, size);
					return dataState == IN_MEMORY;
				} else {
					return true;
				}
			} else {
				return true;
			}
		}
	}

	void end() {
		assert(!ended);
		assert(!hasError);

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

	bool isCommittingToDisk() const {
		return (dataState == OPENING_FILE || dataState == IN_FILE) && !file.writeBuffer.empty();
	}

	void start() {
		assert(!hasError);
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

typedef boost::shared_ptr<FileBackedPipe> FileBackedPipePtr;


} // namespace Passenger

#endif /* _PASSENGER_FILE_BACKED_PIPE_H_ */
