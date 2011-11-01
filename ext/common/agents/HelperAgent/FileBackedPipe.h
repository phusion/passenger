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

#include <eio.h>

#include <oxt/macros.hpp>
#include <SafeLibev.h>
#include <Exceptions.h>
#include <FileDescriptor.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {

using namespace std;
using namespace boost;


class FileBackedPipe: public enable_shared_from_this<FileBackedPipe> {
public:
	typedef function<void (size_t size, bool done)> ConsumeCallback;
	typedef function<void (const char *data, size_t size, const ConsumeCallback &consumed)> DataCallback;
	//typedef function<void (const ExceptionPtr &exception)> ErrorCallback;
	typedef function<void ()> Callback;

private:
	typedef function<void (int err, const char *data, size_t size)> EioReadCallback;

	struct EioUserData {
		weak_ptr<FileBackedPipe> wself;

		EioUserData(FileBackedPipe *self)
			: wself(self->shared_from_this())
			{ }
		
		shared_ptr<FileBackedPipe> lock() {
			return wself.lock();
		}
	};

	struct EioOpenUserData: public EioUserData {
		string filename;

		EioOpenUserData(FileBackedPipe *self, const string &_filename)
			: EioUserData(self),
			  filename(_filename)
			{ }
	};

	struct EioReadUserData: public EioUserData {
		FileDescriptor fd;

		EioReadUserData(FileBackedPipe *self)
			: EioUserData(self),
			  fd(self->file.fd)
			{ }
	};

	struct EioWriteUserData: public EioUserData {
		FileDescriptor fd;
		char *buffer;
		size_t size;

		EioWriteUserData(FileBackedPipe *self, const string &_buffer)
			: EioUserData(self)
		{
			buffer = new char[_buffer.size()];
			memcpy(buffer, _buffer.data(), _buffer.size());
			size = _buffer.size();
		}

		~EioWriteUserData() {
			delete[] buffer;
		}
	};

	const string dir;
	const size_t threshold;

	unsigned int consumedCallCount;

	bool started;
	bool ended;
	string eioOpenFilename;
	shared_array<char> eioReadBuffer;
	EioReadCallback eioReadCallback;

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

	enum {
		IN_MEMORY,
		OPENING_FILE,
		IN_FILE
	} dataState;

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

	bool callOnData(const char *data, size_t size, const ConsumeCallback &consumed) {
		unsigned int oldConsumedCallCount = consumedCallCount;
		dataEventState = CALLING_EVENT_NOW;
		if (OXT_LIKELY(onData != NULL)) {
			onData(data, size, consumed);
		} else {
			consumed(0, true);
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
				eio_open(filename.str().c_str(), O_CREAT | O_RDWR | O_TRUNC,
					0, 0, eioOpenCallback, new EioOpenUserData(this, filename.str()));
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
			size_t size = file.writeBuffer.size();
			file.writingToFile = true;

			EioWriteUserData *userData = new EioWriteUserData(this, file.writeBuffer);
			eio_write(file.fd, userData->buffer, userData->size, file.writtenSize,
				0, writeBufferToFileCallback, userData);
		}
	}

	static int writeBufferToFileCallback(eio_req *req) {
		auto_ptr<EioWriteUserData> userData((EioWriteUserData *) req->data);
		shared_ptr<FileBackedPipe> self = userData->lock();
		if (self == NULL) {
			return 0;
		}

		if (req->result < 0) {
			// TODO: set error
		} else {
			assert(self->dataState == IN_FILE);
			self->file.writeBuffer.erase(0, userData->size);
			self->file.writtenSize += userData->size;
			self->file.writingToFile = false;
			if (!self->file.writeBuffer.empty()) {
				self->writeBufferToFile();
			}
		}
		return 0;
	}

	static int eioOpenCallback(eio_req *req) {
		auto_ptr<EioOpenUserData> userData((EioOpenUserData *) req->data);
		shared_ptr<FileBackedPipe> self = userData->lock();
		if (self == NULL) {
			if (req->result != -1) {
				eio_close(req->result, 0, successCallback, NULL);
				eio_unlink(userData->filename.c_str(), 0, successCallback, NULL);
			}
			return 0;
		}

		assert(self->dataState = OPENING_FILE);
		if (req->result < 0) {
			// TODO: set error
		} else {
			eio_unlink(userData->filename.c_str(), 0, successCallback, NULL);
			self->dataState = IN_FILE;
			self->file.fd = req->result;
			self->writeBufferToFile();
		}
		return 0;
	}

	static int successCallback(eio_req *req) {
		return 0;
	}

	void readBlockFromFileOrWriteBuffer(const EioReadCallback &callback) {
		assert(eioReadBuffer == NULL);
		if (file.readOffset >= file.writtenSize) {
			StaticString data = StaticString(file.writeBuffer).substr(
				file.readOffset - file.writtenSize, 1024 * 16);
			callback(0, data.data(), data.size());
		} else {
			eio_req *req;

			eioReadBuffer.reset(new char[1024 * 16]);
			eioReadCallback = callback;
			req = eio_read(file.fd, eioReadBuffer.get(),
				1024 * 16, file.readOffset, 0,
				eioReadCallback_wrapper, new EioReadUserData(this));
			if (req == NULL) {
				throw RuntimeException("eio_read() failed!");
			}
		}
	}

	static int eioReadCallback_wrapper(eio_req *req) {
		auto_ptr<EioReadUserData> userData((EioReadUserData *) req->data);
		shared_ptr<FileBackedPipe> self = userData->lock();
		if (self == NULL) {
			return 0;
		}

		EioReadCallback callback = self->eioReadCallback;
		shared_array<char> buffer = self->eioReadBuffer;
		self->eioReadCallback = EioReadCallback();
		self->eioReadBuffer.reset();
		if (req->result < 0) {
			callback(req->errorno, NULL, 0);
		} else {
			callback(0, buffer.get(), req->result);
		}
		return 0;
	}

	void dataConsumed(const char *data, size_t size, size_t consumed, bool done) {
		if (pthread_equal(pthread_self(), libev->getCurrentThread())) {
			real_dataConsumed(data, size, consumed, done);
		} else {
			libev->runAsync(boost::bind(
				&FileBackedPipe::real_dataConsumed, this,
				data, size, consumed, done));
		}
	}

	void real_dataConsumed(const char *data, size_t size, size_t consumed, bool done) {
		assert(consumed <= size);
		consumedCallCount++;
		if (done) {
			started = false;
		}

		if (getBufferSize() == 0) {
			// Data passed to write() was immediately consumed.
			assert(dataEventState == CALLING_EVENT_NOW);
			if (started) {
				if (consumed < size) {
					bool immediatelyConsumed = callOnData(data + consumed, size - consumed,
						boost::bind(&FileBackedPipe::dataConsumed, this,
							data + consumed, size - consumed, _1, _2));
					if (!immediatelyConsumed) {
						addToBuffer(data + consumed, size - consumed);
					}
				} else {
					dataEventState = NOT_CALLING_EVENT;
					if (ended && onEnd) {
						onEnd();
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
					if (ended && onEnd) {
						onEnd();
					}
				} else {
					callOnData(memory.data, memory.size, boost::bind(
						&FileBackedPipe::dataConsumed, this,
						NULL, memory.size, _1, _2));
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
					if (ended && onEnd) {
						onEnd();
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
			callOnData(data, size, boost::bind(
				&FileBackedPipe::dataConsumed, this,
				NULL, size, _1, _2));
		}
	}

public:
	DataCallback onData;
	Callback onEnd;
	//ErrorCallback onError;
	Callback onBufferDrained;

	FileBackedPipe(const string &_dir, size_t _threshold = 1024 * 8)
		: dir(_dir),
		  threshold(_threshold)
	{
		consumedCallCount = 0;
		started = false;
		ended = false;
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
		return dataSTate == IN_MEMORY;
	}

	void reset() {
		if (OXT_UNLIKELY(eventCallState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");
		}
		assert(resetable());
		started = false;
		ended = false;
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

	bool write(const char *data, size_t size) {
		assert(!ended);

		if (OXT_UNLIKELY(eventCallState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");

		} else if (!started || dataEventState != NOT_CALLING_EVENT) {
			assert(!started || getBufferSize() > 0);
			addToBuffer(data, size);

		} else {
			assert(started);
			assert(dataEventState == NOT_CALLING_EVENT);
			assert(getBufferSize() == 0);

			bool immediatelyConsumed = callOnData(data, size, boost::bind(
				&FileBackedPipe::dataConsumed, this,
				data, size, _1, _2));
			assert(eventCallState != CALLING_EVENT_NOW);
			if (!immediatelyConsumed) {
				addToBuffer(data, size);
			}
		}
	}

	void end() {
		assert(!ended);

		if (OXT_UNLIKELY(eventCallState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");

		} else if (!started || dataEventState != NOT_CALLING_EVENT) {
			assert(!started || getBufferSize() > 0);
			ended = true;

		} else {
			assert(started);
			assert(dataEventState == NOT_CALLING_EVENT);
			assert(getBufferSize() == 0);

			ended = true;
			if (onEnd) {
				onEnd();
			}
		}
	}

	void start() {
		if (OXT_UNLIKELY(eventCallState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");
		}
		if (!started) {
			started = true;
			if (getBufferSize() > 0 && eventCallState == NOT_CALLING_EVENT) {
				processBuffer(0);
			}
		}
	}

	void stop() {
		if (OXT_UNLIKELY(eventCallState == CALLING_EVENT_NOW)) {
			throw RuntimeException("This function may not be called within a FileBackedPipe event handler.");
		}
		started = false;
	}
};


} // namespace Passenger
