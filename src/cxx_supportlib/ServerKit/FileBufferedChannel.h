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
#ifndef _PASSENGER_SERVER_KIT_FILE_BUFFERED_CHANNEL_H_
#define _PASSENGER_SERVER_KIT_FILE_BUFFERED_CHANNEL_H_

#include <boost/cstdint.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/move/move.hpp>
#include <boost/atomic.hpp>
#include <sys/types.h>
#include <uv.h>
#include <jsoncpp/json.h>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <utility>
#include <string>
#include <deque>
#include <LoggingKit/LoggingKit.h>
#include <ServerKit/Context.h>
#include <ServerKit/Config.h>
#include <ServerKit/Errors.h>
#include <ServerKit/Channel.h>
#include <JsonTools/JsonUtils.h>

namespace Passenger {
namespace ServerKit {

using namespace std;

#define FBC_DEBUG(expr) \
	P_TRACE(3, "[FBC " << (void *) this << "] " << expr)
#define FBC_DEBUG_WITH_POS(file, line, expr) \
	P_TRACE_WITH_POS(3, file, line, "[FBC " << (void *) this << "] " << expr)
#define FBC_DEBUG_FROM_STATIC(expr) \
	P_TRACE(3, "[FBC " << (void *) self << "] " << expr)

#define FBC_DEBUG_FROM_CALLBACK(context, expr) \
	P_TRACE(3, "[FBC " << (void *) context->logbase << "] " << expr)
#define FBC_ERROR_FROM_CALLBACK(context, expr) \
	P_ERROR("[FBC " << (void *) context->logbase << "] " << expr)
#define FBC_CRITICAL_FROM_CALLBACK(context, expr) \
	P_CRITICAL("[FBC " << (void *) context->logbase << "] " << expr)


/**
 * Adds "unlimited" buffering capability to a Channel. A Channel has a buffer size
 * of 1, which is why you can't write to a Channel until the previously written
 * data is consumed. But with FileBufferedChannel, everything you write to it
 * is either buffered to memory, or to disk. If the total amount of buffered data is
 * below a threshold, everything is buffered in memory. Beyond the threshold, buffered
 * data will be written to disk and freed from memory. This allows you to buffer
 * a virtually unlimited amount of data, without using a lot of memory.
 *
 * FileBufferedChannel operates by default in the in-memory mode. All data is buffered
 * in memory. Beyond a threshold (determined by `passedThreshold()`), it switches
 * to in-file mode.
 */
class FileBufferedChannel: protected Channel {
public:
	/***** Types and constants *****/

	enum Mode {
		/**
		 * The default mode. The reader is responsible for switching from
		 * in-file mode to in-memory mode.
		 */
		IN_MEMORY_MODE,

		/**
		 * The `feed()` method is responsible for switching to
		 * in-file mode.
		 */
		IN_FILE_MODE,

		/**
		 * If either the reader or writer encountered an error, it will
		 * cancel everything and switch to the error mode.
		 *
		 * @invariant
		 *     readerState == RS_TERMINATED
		 *     inFileMode == NULL
		 */
		ERROR,

		/**
		 * When switching to the error made, an attempt is made to pass the
		 * error to the data callback. If the previous data callback isn't
		 * finsihed yet, then we'll switch to this state, wait until it
		 * becomes idle, then feed the error and switch to ERROR.
		 *
		 * @invariant
		 *     readerState == RS_TERMINATED
		 *     inFileMode == NULL
		 */
		ERROR_WAITING
	};

	enum ReaderState {
		/** The reader isn't active. It will be activated next time a buffer
		 * is pushed to the queue.
		 */
		RS_INACTIVE,

		/**
		 * The reader is feeding a buffer to the underlying channel.
		 */
		RS_FEEDING,

		/**
		 * The reader is feeding an empty buffer to the underlying channel.
		 */
		RS_FEEDING_EOF,

		/**
		 * The reader has just fed a buffer to the underlying channel,
		 * and is waiting for it to become idle.
		 *
		 * Invariant:
		 *
		 *     mode < ERROR
		 */
		RS_WAITING_FOR_CHANNEL_IDLE,

		/** The reader is reading from the file.
		 *
		 * Invariant:
		 *
		 *     mode == IN_FILE_MODE
		 *     inFileMode->readRequest != NULL
		 *     inFileMode->written > 0
		 */
		RS_READING_FROM_FILE,

		/**
		 * The reader has encountered EOF or an error. It cannot be reactivated
		 * until the FileBufferedChannel is deinitialized and reinitialized.
		 */
		RS_TERMINATED
	};

	enum WriterState {
		/**
		 * The writer isn't active. It will be activated next time
		 * `feed()` notices that the threshold has passed.
		 *
		 * @invariant !passedThreshold()
		 */
		WS_INACTIVE,

		/**
		 * The writer is creating a file.
		 *
		 * @invariant passedThreshold()
		 */
		WS_CREATING_FILE,

		/**
		 * The writer is moving buffers to the file. It transitions to WS_INACTIVE
		 * when there are no more buffers to move.
		 *
		 * @invariant nbuffers > 0
		 */
		WS_MOVING,

		/**
		 * The writer has encountered EOF or an error. It cannot be reactivated
		 * until the FileBufferedChannel is deinitialized and reinitialized.
		 */
		WS_TERMINATED
	};

	typedef Channel::DataCallback DataCallback;
	typedef void (*Callback)(FileBufferedChannel *channel);

	// 2^32-1 bytes.
	static const unsigned int MAX_MEMORY_BUFFERING = 4294967295u;
	// `nbuffers` is 27-bit. This is 2^27-1.
	static const unsigned int MAX_BUFFERS = 134217727;


private:
	/**
	 * A structure containing the details of a libuv asynchronous
	 * filesystem I/O request.
	 *
	 * The I/O callback is responsible for destroying its corresponding
	 * FileIOContext object.
	 */
	struct FileIOContext {
		/**
		 * A back pointer to the FileBufferedChannel that created this
		 * IOContext.
		 *
		 * This pointer is set to NULL when this I/O operation is
		 * canceled (through the `cancel()` method). Cancelation
		 * occurs when the FileBufferedChannel is about to be deinitialized.
		 * So be sure to check for cancellation (using `isCanceled`)
		 * before using the backpointer.
		 */
		FileBufferedChannel *self;
		/**
		 * Pointers to the libev and libuv loops that this FileBufferedChannel
		 * used. We keep the pointers here so that callbacks can perform
		 * asynchronous I/O operations as part of their cleanup, even in the
		 * event the original I/O operation is canceled.
		 *
		 * I/O callbacks do not have to worry about whether these pointers are
		 * stale, because callbacks are run inside the event loop, and we stop the
		 * event loop before destryoing it.
		 */
		SafeLibevPtr libev;
		uv_loop_t *libuv;
		/* req.data always refers back to the FileIOContext object itself. */
		uv_fs_t req;

		/**
		 * Also a pointer to the FileBufferedChannel, but this is used for
		 * logging purposes inside callbacks (see FBC_DEBUG_FROM_CALLBACK).
		 * This pointer is never set to NULL, may be still, and is never
		 * followed.
		 */
		void *logbase;

		FileIOContext(FileBufferedChannel *_self)
			: self(_self),
			  libev(_self->ctx->libev),
			  libuv(_self->ctx->libuv),
			  logbase(_self)
		{
			req.type = UV_UNKNOWN_REQ;
			req.result = -1;
			req.data = this;
		}

		virtual ~FileIOContext() { }

		void cancel() {
			if (!isCanceled()) {
				// uv_cancel() fails if the work is already in progress
				// or completed, so we set self to NULL as an extra
				// indicator that this I/O operation is canceled.
				uv_cancel((uv_req_t *) &req);
				self = NULL;
			}
		}

		/**
		 * Checks whether this I/O operation has been canceled.
		 * Note that the libuv request may not have been canceled
		 * because it was already executing at the time `cancel()`
		 * was called. So after you've checked that `isCanceled()`
		 * returns true, you must also cleanup any potential finished
		 * work in `req`.
		 */
		bool isCanceled() const {
			return self == NULL || req.result == UV_ECANCELED;
		}
	};

	struct ReadContext;

	/**
	 * Holds all states for the in-file mode. Reasons why this is a separate
	 * structure:
	 *
	 * - We can keep the size of the FileBufferedChannel small for the common,
	 *   fast case where the consumer can keep up with the writes.
	 * - We improve the clarity of the code by clearly grouping variables
	 *   that are only used in the in-file mode.
	 * - While libuv operations are in progress, they hold a smart pointer to the
	 *   InFileMode structure, which ensures that the file descriptor that they
	 *   operate on stays open until all libuv operations have finished (or until
	 *   their cancellation have been acknowledged by their callbacks).
	 *
	 * The variables inside this structure point to different places in the file:
	 *
	 *     +------------------------+
	 *     |                        |
	 *     |      already read      |
	 *     |                        |
	 *     +------------------------+  <------ readOffset
	 *     |                        |  \
	 *     |  written but not read  |   |----- written
	 *     |                        |  /
	 *     +------------------------+  <------ readOffset + written
	 *     |  buffer being written  |  --+
	 *     +------------------------+    |
	 *     |   unwritten buffer 1   |    |
	 *     +------------------------+    |
	 *     |   unwritten buffer 2   |    |---- nbuffers,
	 *     +------------------------+    |     bytesBuffered
	 *     |          ....          |  --+
	 *     +------------------------+
	 */
	struct InFileMode {
		/***** Common state *****/

		/**
		 * The libuv loop associated with the FileBufferedChannel.
		 */
		uv_loop_t *libuv;

		/**
		 * The file descriptor of the temp file. It's -1 if the file is being
		 * created.
		 */
		int fd;


		/***** Reader state *****/

		/**
		 * The read operation that the reader is currently performing.
		 *
		 * @invariant
		 *     (readRequest != NULL) == (readerState == RS_READING_FROM_FILE)
		 */
		ReadContext *readRequest;


		/***** Writer state *****/

		WriterState writerState;

		/**
		 * The write operation that the writer is currently performing. Might be
		 * an `uv_fs_open()`, `uv_fs_write()`, or whatever.
		 *
		 * @invariant
		 *     (writerRequest != NULL) == (writerState == WS_CREATING_FILE || writerState == WS_MOVING)
		 */
		FileIOContext *writerRequest;

		/**
		 * Number of bytes already read from the file by the reader.
		 */
		off_t readOffset;
		/**
		 * Number of bytes written to the file by the writer (relative to `readOffset`),
		 * but not yet read by the reader.
		 *
		 * `written` can be _negative_, which means that the writer is still writing buffers to
		 * the file, but the reader has already fed one or more of those still-being-written
		 * buffers to the underlying channel.
		 *
		 * @invariant
		 *     if written < 0:
		 *         nbuffers > 0
		 */
		boost::int64_t written;

		InFileMode(uv_loop_t *_libuv)
			: libuv(_libuv),
			  fd(-1),
			  readRequest(NULL),
			  writerState(WS_INACTIVE),
			  writerRequest(NULL),
			  readOffset(0),
			  written(0)
			{ }

		~InFileMode() {
			P_ASSERT_EQ(readRequest, 0);
			P_ASSERT_EQ(writerRequest, 0);
			if (fd != -1) {
				closeFdInBackground();
			}
		}

		void closeFdInBackground() {
			uv_fs_t *req = (uv_fs_t *) malloc(sizeof(uv_fs_t));
			if (req == NULL) {
				P_CRITICAL("Cannot close file descriptor for FileBufferedChannel temp file: "
					"cannot allocate memory for necessary temporary data structure");
				abort();
			}

			int result = uv_fs_close(libuv, req, fd, fileClosed);
			if (result != 0) {
				P_CRITICAL("Cannot close file descriptor for FileBufferedChannel temp file: "
					"cannot initiate I/O operation: "
					<< uv_strerror(result) << " (errno=" << -result << ")");
				abort();
			}
		}

		static void fileClosed(uv_fs_t *req) {
			P_LOG_FILE_DESCRIPTOR_CLOSE(req->file);
			uv_fs_req_cleanup(req);
			free(req);
		}
	};

	FileBufferedChannelConfig *config;
	Mode mode: 2;
	ReaderState readerState: 3;
	/** Number of buffers in `firstBuffer` + `moreBuffers`. */
	unsigned int nbuffers: 27;

	/**
	 * If an error is encountered, its details are stored here.
	 *
	 * @invariant
	 *     (errcode == 0) == (mode < ERROR)
	 */
	int errcode;

	/**
	 * `firstBuffer` and `moreBuffers` together form a queue of buffers for the reader
	 * and the writer to process.
	 *
	 * A deque allocates memory on the heap. In the common case where the channel callback
	 * can keep up with the writes, we don't want to have any dynamic memory allocation
	 * at all. That's why we store the first buffer in an instance variable. Only when
	 * there is more than 1 buffer do we use the deque.
	 *
	 * Buffers are pushed to end of the queue, and popped from the beginning. In the in-memory
	 * mode, the reader is responsible for popping buffers. In the in-file mode, the writer
	 * is responsible for popping buffers (and writing them to the file).
	 */
	boost::uint32_t bytesBuffered; // number of bytes buffered in memory
	MemoryKit::mbuf firstBuffer;
	deque<MemoryKit::mbuf> moreBuffers;

	/**
	 * @invariant
	 *     (inFileMode != NULL) == (mode == IN_FILE_MODE)
	 */
	boost::shared_ptr<InFileMode> inFileMode;


	/***** Buffer manipulation *****/

	void clearBuffers(bool mayCallCallbacks) {
		unsigned int oldNbuffers = nbuffers;
		nbuffers = 0;
		bytesBuffered = 0;
		firstBuffer = MemoryKit::mbuf();
		if (!moreBuffers.empty()) {
			// Some STL implementations, like OS X's, iterate through
			// the deque in its clear() implementation, so adding
			// a conditional here improves performance slightly.
			moreBuffers.clear();
		}
		if (mayCallCallbacks && oldNbuffers != 0) {
			callBuffersFlushedCallback();
		}
	}

	void pushBuffer(const MemoryKit::mbuf &buffer) {
		assert(bytesBuffered + buffer.size() <= MAX_MEMORY_BUFFERING);
		assert(nbuffers < MAX_BUFFERS);
		if (nbuffers == 0) {
			firstBuffer = buffer;
		} else {
			moreBuffers.push_back(buffer);
		}
		nbuffers++;
		bytesBuffered += buffer.size();
		FBC_DEBUG("pushBuffer() completed: nbuffers = " << nbuffers << ", bytesBuffered = " << bytesBuffered);
	}

	void popBuffer() {
		assert(bytesBuffered >= firstBuffer.size());
		bytesBuffered -= firstBuffer.size();
		nbuffers--;
		FBC_DEBUG("popBuffer() completed: nbuffers = " << nbuffers << ", bytesBuffered = " << bytesBuffered);
		if (moreBuffers.empty()) {
			firstBuffer = MemoryKit::mbuf();
			P_ASSERT_EQ(nbuffers, 0);
			callBuffersFlushedCallback();
		} else {
			firstBuffer = moreBuffers.front();
			moreBuffers.pop_front();
		}
	}

	OXT_FORCE_INLINE
	bool hasBuffers() const {
		return nbuffers > 0;
	}

	OXT_FORCE_INLINE
	MemoryKit::mbuf &peekBuffer() {
		return firstBuffer;
	}

	MemoryKit::mbuf &peekLastBuffer() {
		if (nbuffers <= 1) {
			return firstBuffer;
		} else {
			return moreBuffers.back();
		}
	}

	const MemoryKit::mbuf &peekLastBuffer() const {
		if (nbuffers <= 1) {
			return firstBuffer;
		} else {
			return moreBuffers.back();
		}
	}

	void callBuffersFlushedCallback() {
		if (buffersFlushedCallback) {
			FBC_DEBUG("Calling buffersFlushedCallback");
			buffersFlushedCallback(this);
		}
	}

	void callDataFlushedCallback() {
		if (dataFlushedCallback) {
			FBC_DEBUG("Calling dataFlushedCallback");
			dataFlushedCallback(this);
		}
	}


	/***** Reader *****/

	void readNext() {
		RefGuard guard(hooks, this, __FILE__, __LINE__);
		readNextWithoutRefGuard();
	}

	void readNextWithoutRefGuard() {
		begin:
		FBC_DEBUG("Reader: reading next");
		P_ASSERT_EQ(Channel::state, IDLE);
		unsigned int generation = this->generation;

		switch (mode) {
		case IN_MEMORY_MODE:
			if (!hasBuffers()) {
				FBC_DEBUG("Reader: no more buffers. Transitioning to RS_INACTIVE");
				readerState = RS_INACTIVE;
				verifyInvariants();
				callDataFlushedCallback();
			} else if (peekBuffer().empty()) {
				FBC_DEBUG("Reader: EOF encountered. Feeding EOF");
				readerState = RS_FEEDING_EOF;
				verifyInvariants();
				{
					// Make a copy of the buffer so that if the callback calls
					// deinitialize(), it won't suddenly reset the buffer argument.
					MemoryKit::mbuf buffer(peekBuffer());
					Channel::feedWithoutRefGuard(buffer);
				}
				if (generation != this->generation || mode >= ERROR) {
					// Callback deinitialized this object, or callback
					// called a method that encountered an error.
					return;
				}
				P_ASSERT_EQ(readerState, RS_FEEDING_EOF);
				verifyInvariants();
				FBC_DEBUG("Reader: EOF fed. Transitioning to RS_TERMINATED");
				terminateReaderBecauseOfEOF();
			} else {
				MemoryKit::mbuf buffer(peekBuffer());
				FBC_DEBUG("Reader: found buffer, " << buffer.size() << " bytes");
				popBuffer();
				if (generation != this->generation || mode >= ERROR) {
					// buffersFlushedCallback deinitialized this object, or callback
					// called a method that encountered an error.
					return;
				}
				readerState = RS_FEEDING;
				FBC_DEBUG("Reader: feeding buffer, " << buffer.size() << " bytes");
				Channel::feedWithoutRefGuard(buffer);
				if (generation != this->generation || mode >= ERROR) {
					// Callback deinitialized this object, or callback
					// called a method that encountered an error.
					return;
				}
				P_ASSERT_EQ(readerState, RS_FEEDING);
				verifyInvariants();
				if (acceptingInput()) {
					goto begin;
				} else if (mayAcceptInputLater()) {
					readNextWhenChannelIdle();
				} else {
					FBC_DEBUG("Reader: data callback no longer accepts further data");
					terminateReaderBecauseOfEOF();
				}
			}
			break;
		case IN_FILE_MODE:
			if (inFileMode->written > 0) {
				// The file contains unread data. Read from
				// file and feed to underlying channel.
				readNextChunkFromFile();
			} else {
				// The file contains no unread data. Read next buffer
				// from memory.
				pair<MemoryKit::mbuf, bool> result = findBufferForReadProcessing();

				if (!result.second) {
					readerState = RS_INACTIVE;
					if (config->autoTruncateFile) {
						FBC_DEBUG("Reader: no more buffers. Transitioning to RS_INACTIVE, truncating file");
						switchToInMemoryMode();
						if (generation != this->generation || mode >= ERROR) {
							// Callback deinitialized this object, or callback
							// called a method that encountered an error.
							return;
						}
					} else {
						FBC_DEBUG("Reader: no more buffers. Transitioning to RS_INACTIVE, "
							"not truncating file because config->autoTruncateFile is turned off");
					}
					verifyInvariants();
					callDataFlushedCallback();
				} else if (result.first.empty()) {
					FBC_DEBUG("Reader: EOF encountered. Feeding EOF");
					readerState = RS_FEEDING_EOF;
					verifyInvariants();
					Channel::feedWithoutRefGuard(result.first);
					if (generation != this->generation || mode >= ERROR) {
						// Callback deinitialized this object, or callback
						// called a method that encountered an error.
						return;
					}
					P_ASSERT_EQ(readerState, RS_FEEDING_EOF);
					verifyInvariants();
					FBC_DEBUG("Reader: EOF fed. Transitioning to RS_TERMINATED");
					terminateReaderBecauseOfEOF();
				} else {
					FBC_DEBUG("Reader: found buffer, " << result.first.size() << " bytes");
					inFileMode->readOffset += result.first.size();
					inFileMode->written -= result.first.size();
					readerState = RS_FEEDING;
					FBC_DEBUG("Reader: feeding buffer, " << result.first.size() << " bytes");
					Channel::feedWithoutRefGuard(result.first);
					if (generation != this->generation || mode >= ERROR) {
						// Callback deinitialized this object, or callback
						// called a method that encountered an error.
						return;
					}
					P_ASSERT_EQ(readerState, RS_FEEDING);
					verifyInvariants();
					if (acceptingInput()) {
						goto begin;
					} else if (mayAcceptInputLater()) {
						readNextWhenChannelIdle();
					} else {
						FBC_DEBUG("Reader: data callback no longer accepts further data");
						terminateReaderBecauseOfEOF();
					}
				}
			}
			break;
		case ERROR:
		case ERROR_WAITING:
			P_BUG("Should never be reached");
			break;
		}
	}

	void terminateReaderBecauseOfEOF() {
		readerState = RS_TERMINATED;
		verifyInvariants();
		callDataFlushedCallback();
	}

	void readNextWhenChannelIdle() {
		FBC_DEBUG("Reader: waiting for underlying channel to become idle");
		readerState = RS_WAITING_FOR_CHANNEL_IDLE;
		verifyInvariants();
	}

	void channelHasBecomeIdle() {
		FBC_DEBUG("Reader: underlying channel has become idle");
		verifyInvariants();
		readNext();
	}

	void channelEndedWhileWaitingForItToBecomeIdle() {
		if (hasError()) {
			FBC_DEBUG("Reader: error encountered while waiting for underlying channel to become idle");
		} else {
			FBC_DEBUG("Reader: underlying channel ended while waiting for it to become idle");
		}
		terminateReaderBecauseOfEOF();
	}

	struct ReadContext: public FileIOContext {
		MemoryKit::mbuf buffer;
		uv_buf_t uvBuffer;
		// Smart pointer to keep fd open until libuv operation
		// is finished.
		boost::shared_ptr<InFileMode> inFileMode;

		ReadContext(FileBufferedChannel *self)
			: FileIOContext(self)
			{ }
	};

	void readNextChunkFromFile() {
		assert(inFileMode->written > 0);
		size_t size = std::min<size_t>(inFileMode->written,
			mbuf_pool_data_size(&ctx->mbuf_pool));
		if (config->maxDiskChunkReadSize > 0 && size > config->maxDiskChunkReadSize) {
			size = config->maxDiskChunkReadSize;
		}
		FBC_DEBUG("Reader: reading next chunk from file, " << size << " bytes");
		verifyInvariants();
		ReadContext *readContext = new ReadContext(this);
		readContext->buffer = MemoryKit::mbuf_get(&ctx->mbuf_pool);
		readContext->inFileMode = inFileMode;
		readContext->uvBuffer = uv_buf_init(readContext->buffer.start, size);
		readerState = RS_READING_FROM_FILE;
		inFileMode->readRequest = readContext;

		uv_fs_read(ctx->libuv, &readContext->req, inFileMode->fd,
			&readContext->uvBuffer, 1, inFileMode->readOffset,
			_nextChunkDoneReading);
		verifyInvariants();
	}

	static void _nextChunkDoneReading(uv_fs_t *req) {
		ReadContext *readContext = (ReadContext *) req->data;
		uv_fs_req_cleanup(req);
		if (readContext->isCanceled()) {
			delete readContext;
			return;
		}

		readContext->self->nextChunkDoneReading(readContext);
	}

	void nextChunkDoneReading(ReadContext *readContext) {
		RefGuard guard(hooks, this, __FILE__, __LINE__);

		FBC_DEBUG("Reader: done reading chunk");
		P_ASSERT_EQ(readerState, RS_READING_FROM_FILE);
		verifyInvariants();
		MemoryKit::mbuf buffer(boost::move(readContext->buffer));
		ssize_t result = readContext->req.result;
		delete readContext;
		inFileMode->readRequest = NULL;

		if (result >= 0) {
			int fd = result;
			unsigned int generation = this->generation;

			assert(fd <= inFileMode->written);
			buffer = MemoryKit::mbuf(buffer, 0, fd);
			inFileMode->readOffset += buffer.size();
			inFileMode->written -= buffer.size();

			FBC_DEBUG("Reader: feeding buffer, " << buffer.size() << " bytes");
			readerState = RS_FEEDING;
			Channel::feedWithoutRefGuard(buffer);
			if (generation != this->generation || mode >= ERROR) {
				// Callback deinitialized this object, or callback
				// called a method that encountered an error.
				return;
			}
			P_ASSERT_EQ(readerState, RS_FEEDING);
			verifyInvariants();
			if (acceptingInput()) {
				readNext();
			} else if (mayAcceptInputLater()) {
				readNextWhenChannelIdle();
			} else {
				FBC_DEBUG("Reader: data callback no longer accepts further data");
				terminateReaderBecauseOfEOF();
			}
		} else {
			int errcode = -result;
			setError(errcode, __FILE__, __LINE__);
		}
	}

	// Returns (mbuf, found).
	pair<MemoryKit::mbuf, bool> findBufferForReadProcessing() {
		P_ASSERT_EQ(mode, IN_FILE_MODE);

		if (nbuffers == 0) {
			return make_pair(MemoryKit::mbuf(), false);
		}

		boost::int32_t target = -inFileMode->written;
		boost::int32_t offset = 0;
		deque<MemoryKit::mbuf>::iterator it, end = moreBuffers.end();

		if (offset == target) {
			return make_pair(firstBuffer, true);
		}

		it = moreBuffers.begin();
		offset += firstBuffer.size();
		while (it != end) {
			if (offset == target || it->empty()) {
				return make_pair(*it, true);
			} else {
				offset += it->size();
				it++;
			}
		}

		return make_pair(MemoryKit::mbuf(), false);
	}


	/***** Switching to or resetting in-file mode *****/

	void switchToInFileMode() {
		P_ASSERT_EQ(mode, IN_MEMORY_MODE);
		P_ASSERT_EQ(inFileMode, 0);

		FBC_DEBUG("Switching to in-file mode");
		mode = IN_FILE_MODE;
		inFileMode = boost::make_shared<InFileMode>(ctx->libuv);
		createBufferFile();
	}

	/**
	 * "Truncates" the the temp file by closing it and creating
	 * a new one, instead of calling `ftruncate()` or something.
	 * This way, any pending I/O operations in the background won't
	 * affect correctness.
	 *
	 * This method may call callbacks.
	 */
	void switchToInMemoryMode() {
		P_ASSERT_EQ(mode, IN_FILE_MODE);
		assert(inFileMode->written <= 0);

		FBC_DEBUG("Recreating file, switching to in-memory mode");
		cancelWriter();
		mode = IN_MEMORY_MODE;
		inFileMode.reset();
		clearBuffers(true);
	}


	/***** File creator *****/

	struct FileCreationContext: public FileIOContext {
		string path;

		FileCreationContext(FileBufferedChannel *self)
			: FileIOContext(self)
			{ }
	};

	void createBufferFile() {
		P_ASSERT_EQ(mode, IN_FILE_MODE);
		P_ASSERT_EQ(inFileMode->writerState, WS_INACTIVE);
		P_ASSERT_EQ(inFileMode->fd, -1);

		FileCreationContext *fcContext = new FileCreationContext(this);
		fcContext->path = config->bufferDir;
		fcContext->path.append("/buffer.");
		fcContext->path.append(toString(rand()));

		inFileMode->writerState = WS_CREATING_FILE;
		inFileMode->writerRequest = fcContext;

		if (config->delayInFileModeSwitching == 0) {
			FBC_DEBUG("Writer: creating file " << fcContext->path);
			int result = uv_fs_open(ctx->libuv, &fcContext->req,
				fcContext->path.c_str(), O_RDWR | O_CREAT | O_EXCL,
				0600, _bufferFileCreated);
			if (result != 0) {
				fcContext->req.result = result;
				ctx->libev->runLater(boost::bind(_bufferFileCreated,
					&fcContext->req));
			}
		} else {
			FBC_DEBUG("Writer: delaying in-file mode switching for " <<
				config->delayInFileModeSwitching << "ms");
			ctx->libev->runAfter(config->delayInFileModeSwitching,
				boost::bind(_bufferFileDoneDelaying, fcContext));
		}
	}

	static void _bufferFileDoneDelaying(FileCreationContext *fcContext) {
		if (fcContext->isCanceled()) {
			// We don't cleanup fcContext->req here because we didn't
			// start a libuv request.
			delete fcContext;
			return;
		}

		FileBufferedChannel *self = fcContext->self;
		self->bufferFileDoneDelaying(fcContext);
	}

	void bufferFileDoneDelaying(FileCreationContext *fcContext) {
		FBC_DEBUG("Writer: done delaying in-file mode switching. "
			"Creating file: " << fcContext->path);
		int result = uv_fs_open(ctx->libuv, &fcContext->req,
			fcContext->path.c_str(), O_RDWR | O_CREAT | O_EXCL,
			0600, _bufferFileCreated);
		if (result != 0) {
			fcContext->req.result = result;
			_bufferFileCreated(&fcContext->req);
		}
	}

	static void _bufferFileCreated(uv_fs_t *req) {
		FileCreationContext *fcContext = static_cast<FileCreationContext *>(req->data);
		uv_fs_req_cleanup(req);
		if (fcContext->isCanceled()) {
			if (req->result >= 0) {
				FBC_DEBUG_FROM_CALLBACK(fcContext,
					"Writer: creation of file " << fcContext->path <<
					"canceled. Deleting file in the background");
				closeBufferFileInBackground(fcContext);
				// Will take care of deleting fcContext
				unlinkBufferFileInBackground(fcContext);
			} else {
				delete fcContext;
			}
			return;
		}

		fcContext->self->bufferFileCreated(fcContext);
	}

	void bufferFileCreated(FileCreationContext *fcContext) {
		P_ASSERT_EQ(inFileMode->writerState, WS_CREATING_FILE);
		verifyInvariants();
		inFileMode->writerRequest = NULL;

		if (fcContext->req.result >= 0) {
			FBC_DEBUG("Writer: file created. Deleting file in the background");
			P_LOG_FILE_DESCRIPTOR_OPEN4(fcContext->req.result, __FILE__, __LINE__,
				"FileBufferedChannel buffer file");
			inFileMode->fd = fcContext->req.result;
			// Will take care of deleting fcContext
			unlinkBufferFileInBackground(fcContext);
			moveNextBufferToFile();
		} else {
			int errcode = -fcContext->req.result;
			delete fcContext;
			if (errcode == EEXIST) {
				FBC_DEBUG("Writer: file already exists, retrying");
				inFileMode->writerState = WS_INACTIVE;
				createBufferFile();
				verifyInvariants();
			} else {
				setError(errcode, __FILE__, __LINE__);
			}
		}
	}

	static void closeBufferFileInBackground(FileCreationContext *fcContext) {
		// Do not use fcContext->self in here. This method may be called
		// when the I/O operation is already canceled.

		assert(fcContext->req.result >= 0);

		uv_fs_t *closeReq = (uv_fs_t *) malloc(sizeof(uv_fs_t));
		if (closeReq == NULL) {
			FBC_CRITICAL_FROM_CALLBACK(fcContext,
				"Cannot close file descriptor for " << fcContext->path
				<< ": cannot allocate memory for necessary temporary data structure");
			abort();
		}

		int result = uv_fs_close(fcContext->libuv, closeReq, fcContext->req.result,
			bufferFileClosed);
		if (result != 0) {
			FBC_CRITICAL_FROM_CALLBACK(fcContext,
				"Cannot close file descriptor for " << fcContext->path
				<< ": cannot initiate I/O operation: "
				<< uv_strerror(result) << " (errno=" << -result << ")");
			abort();
		}
	}

	static void unlinkBufferFileInBackground(FileCreationContext *fcContext) {
		// Nobody will cancel this unlink operation. We set self to NULL
		// here as a warning that we should not use the backpointer.
		fcContext->self = NULL;

		uv_fs_t *unlinkReq = (uv_fs_t *) malloc(sizeof(uv_fs_t));
		if (unlinkReq == NULL) {
			FBC_ERROR_FROM_CALLBACK(fcContext,
				"Cannot delete " << fcContext->path <<
				": cannot allocate memory for necessary temporary data structure");
			delete fcContext;
		} else {
			unlinkReq->data = fcContext;
			int result = uv_fs_unlink(fcContext->libuv, unlinkReq, fcContext->path.c_str(),
				bufferFileUnlinked);
			if (result != 0) {
				FBC_ERROR_FROM_CALLBACK(fcContext,
					"Cannot delete " << fcContext->path << ": cannot initiate I/O operation: "
					<< uv_strerror(result) << " (errno=" << -result << ")");
				free(unlinkReq);
				delete fcContext;
			}
		}
	}

	static void bufferFileUnlinked(uv_fs_t *req) {
		FileCreationContext *fcContext = static_cast<FileCreationContext *>(req->data);
		assert(fcContext->self == NULL);

		if (req->result == UV_ECANCELED) {
			uv_fs_req_cleanup(req);
			free(req);
			delete fcContext;
			return;
		}

		if (req->result >= 0) {
			FBC_DEBUG_FROM_CALLBACK(fcContext,
				"Writer: file " << fcContext->path << " deleted");
		} else {
			FBC_DEBUG_FROM_CALLBACK(fcContext,
				"Writer: failed to delete " << fcContext->path <<
				": " << uv_strerror(req->result) << " (errno=" << -req->result << ")");
		}

		uv_fs_req_cleanup(req);
		free(req);
		delete fcContext;
	}

	static void bufferFileClosed(uv_fs_t *req) {
		uv_fs_req_cleanup(req);
		free(req);
	}


	/***** Mover *****/

	struct MoveContext: public FileIOContext {
		// Smart pointer to keep fd open until libuv operation
		// is finished.
		boost::shared_ptr<InFileMode> inFileMode;
		MemoryKit::mbuf buffer;
		uv_buf_t uvBuffer;
		size_t written;

		MoveContext(FileBufferedChannel *self)
			: FileIOContext(self)
			{ }
	};

	void moveNextBufferToFile() {
		P_ASSERT_EQ(mode, IN_FILE_MODE);
		assert(inFileMode->fd != -1);
		verifyInvariants();

		if (nbuffers == 0) {
			FBC_DEBUG("Writer: no more buffers. Transitioning to WS_INACTIVE");
			inFileMode->writerState = WS_INACTIVE;
			return;
		} else if (peekBuffer().empty()) {
			FBC_DEBUG("Writer: EOF encountered. Transitioning to WS_TERMINATED");
			inFileMode->writerState = WS_TERMINATED;
			return;
		}

		FBC_DEBUG("Writer: moving next buffer to file: " <<
			peekBuffer().size() << " bytes");

		MoveContext *moveContext = new MoveContext(this);
		moveContext->inFileMode = inFileMode;
		moveContext->buffer = peekBuffer();
		moveContext->written = 0;
		moveContext->uvBuffer = uv_buf_init(moveContext->buffer.start,
			moveContext->buffer.size());

		inFileMode->writerState = WS_MOVING;
		inFileMode->writerRequest = moveContext;
		int result = uv_fs_write(ctx->libuv, &moveContext->req, inFileMode->fd,
			&moveContext->uvBuffer, 1,
			inFileMode->readOffset + inFileMode->written,
			_bufferWrittenToFile);
		if (result != 0) {
			moveContext->req.result = result;
			ctx->libev->runLater(boost::bind(_bufferWrittenToFile,
				&moveContext->req));
		}
		verifyInvariants();
	}

	static void _bufferWrittenToFile(uv_fs_t *req) {
		MoveContext *moveContext = static_cast<MoveContext *>(req->data);
		uv_fs_req_cleanup(req);
		if (moveContext->isCanceled()) {
			delete moveContext;
			return;
		}

		moveContext->self->bufferWrittenToFile(moveContext);
	}

	void bufferWrittenToFile(MoveContext *moveContext) {
		P_ASSERT_EQ(mode, IN_FILE_MODE);
		P_ASSERT_EQ(inFileMode->writerState, WS_MOVING);
		assert(!peekBuffer().empty());
		verifyInvariants();

		if (moveContext->req.result >= 0) {
			moveContext->written += moveContext->req.result;
			assert(moveContext->written <= moveContext->buffer.size());

			if (moveContext->written == moveContext->buffer.size()) {
				// Write completed. Proceed with next buffer.
				RefGuard guard(hooks, this, __FILE__, __LINE__);
				unsigned int generation = this->generation;

				FBC_DEBUG("Writer: move complete");
				assert(peekBuffer().size() == moveContext->buffer.size());
				inFileMode->written += moveContext->buffer.size();

				popBuffer();
				if (generation != this->generation || mode >= ERROR) {
					// buffersFlushedCallback deinitialized this object, or callback
					// called a method that encountered an error.
					delete moveContext;
					return;
				}

				inFileMode->writerRequest = NULL;
				delete moveContext;
				moveNextBufferToFile();
			} else {
				FBC_DEBUG("Writer: move incomplete, proceeding " <<
					"with writing rest of buffer");
				moveContext->uvBuffer = uv_buf_init(
					moveContext->buffer.start + moveContext->written,
					moveContext->buffer.size() - moveContext->written);
				int result = uv_fs_write(ctx->libuv, &moveContext->req,
					inFileMode->fd, &moveContext->uvBuffer, 1,
					inFileMode->readOffset + inFileMode->written,
					_bufferWrittenToFile);
				if (result != 0) {
					moveContext->req.result = result;
					ctx->libev->runLater(boost::bind(_bufferWrittenToFile,
						&moveContext->req));
				}
				verifyInvariants();
			}
		} else {
			FBC_DEBUG("Writer: file write failed");
			int errcode = -moveContext->req.result;
			delete moveContext;
			inFileMode->writerRequest = NULL;
			inFileMode->writerState = WS_TERMINATED;
			setError(errcode, __FILE__, __LINE__);
		}
	}


	/***** Misc *****/

	void setError(int errcode, const char *file, unsigned int line) {
		if (mode >= ERROR) {
			return;
		}

		FBC_DEBUG_WITH_POS(file, line, "Setting error: errno=" <<
			errcode << " (" << getErrorDesc(errcode) << ")");
		cancelReader();
		if (mode == IN_FILE_MODE) {
			cancelWriter();
		}
		readerState = RS_TERMINATED;
		this->errcode = errcode;
		inFileMode.reset();
		if (acceptingInput()) {
			FBC_DEBUG("Feeding error");
			mode = ERROR;
			Channel::feedError(errcode);
		} else {
			FBC_DEBUG("Waiting until underlying channel becomes idle for error feeding");
			mode = ERROR_WAITING;
		}
	}

	void feedErrorWhenChannelIdleOrEnded() {
		assert(errcode != 0);
		if (isIdle()) {
			FBC_DEBUG("Channel has become idle. Feeding error");
			Channel::feedError(errcode);
		} else {
			FBC_DEBUG("Channel ended while trying to feed an error");
		}
	}

	/**
	 * Must be used in combination with `setError()`, so that the reader will
	 * stop processing after returning from `Channel::feed()`.
	 */
	void cancelReader() {
		switch (readerState) {
		case RS_FEEDING:
		case RS_FEEDING_EOF:
		case RS_WAITING_FOR_CHANNEL_IDLE:
			break;
		case RS_READING_FROM_FILE:
			inFileMode->readRequest->cancel();
			inFileMode->readRequest = NULL;
			break;
		case RS_INACTIVE:
		case RS_TERMINATED:
			return;
		}
	}

	void cancelWriter() {
		P_ASSERT_EQ(mode, IN_FILE_MODE);

		switch (inFileMode->writerState) {
		case WS_INACTIVE:
			break;
		case WS_CREATING_FILE:
		case WS_MOVING:
			inFileMode->writerRequest->cancel();
			inFileMode->writerRequest = NULL;
			break;
		case WS_TERMINATED:
			return;
		}
		inFileMode->writerState = WS_INACTIVE;
	}

	const char *getReaderStateString() const {
		switch (readerState) {
		case RS_INACTIVE:
			return "RS_INACTIVE";
		case RS_FEEDING:
			return "RS_FEEDING";
		case RS_FEEDING_EOF:
			return "RS_FEEDING_EOF";
		case RS_WAITING_FOR_CHANNEL_IDLE:
			return "RS_WAITING_FOR_CHANNEL_IDLE";
		case RS_READING_FROM_FILE:
			return "RS_READING_FROM_FILE";
		case RS_TERMINATED:
			return "RS_TERMINATED";
		default:
			P_BUG("Unknown readerState");
			return NULL;
		}
	}

	const char *getWriterStateString() const {
		switch (inFileMode->writerState) {
		case WS_INACTIVE:
			return "WS_INACTIVE";
		case WS_CREATING_FILE:
			return "WS_CREATING_FILE";
		case WS_MOVING:
			return "WS_MOVING";
		case WS_TERMINATED:
			return "WS_TERMINATED";
		default:
			P_BUG("Unknown writerState");
			return NULL;
		}
	}

	void verifyInvariants() const {
		#ifndef NDEBUG
			if (mode >= ERROR) {
				P_ASSERT_EQ(readerState, RS_TERMINATED);
				P_ASSERT_EQ(inFileMode, 0);
			}

			switch (readerState) {
			case RS_INACTIVE:
			case RS_FEEDING:
			case RS_FEEDING_EOF:
				break;
			case RS_WAITING_FOR_CHANNEL_IDLE:
				assert(mode < ERROR);
				break;
			case RS_READING_FROM_FILE:
				P_ASSERT_EQ(mode, IN_FILE_MODE);
				assert(inFileMode->readRequest != NULL);
				assert(inFileMode->written > 0);
				break;
			case RS_TERMINATED:
				break;
			}

			assert((errcode == 0) == (mode < ERROR));
			assert((inFileMode != NULL) == (mode == IN_FILE_MODE));
		#endif
	}

	static void onChannelConsumed(Channel *channel, unsigned int size) {
		FileBufferedChannel *self = static_cast<FileBufferedChannel *>(channel);
		if (self->readerState == RS_WAITING_FOR_CHANNEL_IDLE) {
			if (self->acceptingInput()) {
				self->channelHasBecomeIdle();
			} else {
				assert(self->Channel::ended());
				self->channelEndedWhileWaitingForItToBecomeIdle();
			}
		} else if (self->mode == ERROR_WAITING) {
			self->feedErrorWhenChannelIdleOrEnded();
		}
	}

public:
	/**
	 * Called when all the in-memory buffers have been popped. This could happen
	 * (when we're in the in-memory mode) because the last in-memory buffer is being
	 * processed by the data callback. It could also happen (when we're in the in-file
	 * mode) when the last in-memory buffer has sucessfully been written to disk.
	 *
	 * This event does not imply that the data callback has consumed all memory
	 * buffers. For example, in case of FileBufferedFdSinkChannel, this event does
	 * not imply that all the in-memory buffers have been written to the sink FD.
	 * That's what `dataFlushedCallback` is for.
	 *
	 * N.B. this class intentionally doesn't manage buffersFlushedCallback in any way,
	 * the user is responsible for ensuring correctness of the variable's content.
	 */
	Callback buffersFlushedCallback;
	/**
	 * Called when all buffered data (whether in-memory or on-disk) has been consumed
	 * by the data callback. In case of FileBufferedFdSinkChannel, this means that all
	 * buffered data has been written out to the sink FD.
	 *
	 * N.B. this class intentionally doesn't manage dataFlushedCallback in any way,
	 * the user is responsible for ensuring correctness of the variable's content.
	 */
	Callback dataFlushedCallback;

	FileBufferedChannel()
		: config(NULL),
		  mode(IN_MEMORY_MODE),
		  readerState(RS_INACTIVE),
		  nbuffers(0),
		  errcode(0),
		  bytesBuffered(0),
		  inFileMode(),
		  buffersFlushedCallback(NULL),
		  dataFlushedCallback(NULL)
	{
		Channel::consumedCallback = onChannelConsumed;
	}

	FileBufferedChannel(Context *context)
		: Channel(context),
		  config(&context->config.fileBufferedChannelConfig),
		  mode(IN_MEMORY_MODE),
		  readerState(RS_INACTIVE),
		  nbuffers(0),
		  errcode(0),
		  bytesBuffered(0),
		  inFileMode(),
		  buffersFlushedCallback(NULL),
		  dataFlushedCallback(NULL)
	{
		Channel::consumedCallback = onChannelConsumed;
	}

	~FileBufferedChannel() {
		cancelReader();
		if (mode == IN_FILE_MODE) {
			cancelWriter();
		}
	}

	// May only be called right after construction.
	void setContext(Context *context) {
		Channel::setContext(context);
		if (config == NULL) {
			config = &context->config.fileBufferedChannelConfig;
		}
	}

	void feed(const MemoryKit::mbuf &buffer) {
		RefGuard guard(hooks, this, __FILE__, __LINE__);
		feedWithoutRefGuard(buffer);
	}

	void feed(const char *data, unsigned int size) {
		feed(MemoryKit::mbuf(data, size));
	}

	void feed(const char *data) {
		feed(MemoryKit::mbuf(data));
	}

	void feedWithoutRefGuard(const MemoryKit::mbuf &buffer) {
		FBC_DEBUG("Feeding " << buffer.size() << " bytes");
		verifyInvariants();
		if (ended()) {
			FBC_DEBUG("Feeding aborted: EOF or error detected");
			return;
		}
		pushBuffer(buffer);
		if (mode == IN_MEMORY_MODE && passedThreshold()) {
			switchToInFileMode();
		} else if (mode == IN_FILE_MODE
		        && inFileMode->writerState == WS_INACTIVE
		        && config->autoStartMover)
		{
			moveNextBufferToFile();
		}
		if (readerState == RS_INACTIVE) {
			if (acceptingInput()) {
				readNextWithoutRefGuard();
			} else {
				readNextWhenChannelIdle();
			}
		}
	}

	void feedWithoutRefGuard(const char *data, unsigned int size) {
		feedWithoutRefGuard(MemoryKit::mbuf(data, size));
	}

	void feedError(int errcode, const char *file = NULL, unsigned int line = 0) {
		if (file == NULL) {
			file = __FILE__;
		}
		if (line == 0) {
			line = __LINE__;
		}
		setError(errcode, file, line);
	}

	void reinitialize() {
		Channel::reinitialize();
		verifyInvariants();
	}

	void deinitialize() {
		FBC_DEBUG("Deinitialize");
		cancelReader();
		if (mode == IN_FILE_MODE) {
			cancelWriter();
		}
		clearBuffers(false);
		mode = IN_MEMORY_MODE;
		readerState = RS_INACTIVE;
		errcode = 0;
		if (OXT_UNLIKELY(inFileMode != NULL)) {
			inFileMode.reset();
		}
		Channel::deinitialize();
	}

	void start() {
		Channel::start();
	}

	void stop() {
		Channel::stop();
	}

	bool isStarted() const {
		return Channel::isStarted();
	}

	void consumed(unsigned int size, bool end) {
		Channel::consumed(size, end);
	}

	Channel::State getState() const {
		return state;
	}

	Mode getMode() const {
		return mode;
	}

	ReaderState getReaderState() const {
		return readerState;
	}

	WriterState getWriterState() const {
		return inFileMode->writerState;
	}

	/**
	 * Returns the number of bytes buffered in memory.
	 */
	unsigned int getBytesBuffered() const {
		return bytesBuffered;
	}

	/**
	 * Returns the number of bytes that are buffered on disk
	 * and have not yet been read.
	 */
	boost::uint64_t getBytesBufferedOnDisk() const {
		if (mode == IN_FILE_MODE && inFileMode->written >= 0) {
			return inFileMode->written;
		} else {
			return 0;
		}
	}

	/**
	 * Returns the total bytes buffered, both in-memory and on disk.
	 */
	boost::uint64_t getTotalBytesBuffered() const {
		return bytesBuffered + getBytesBufferedOnDisk();
	}

	bool ended() const {
		return (hasBuffers() && peekLastBuffer().empty())
			|| mode >= ERROR || Channel::ended();
	}

	bool endAcked() const {
		return Channel::endAcked();
	}

	bool passedThreshold() const {
		return bytesBuffered >= config->threshold;
	}

	OXT_FORCE_INLINE
	void setDataCallback(DataCallback callback) {
		Channel::dataCallback = callback;
	}

	OXT_FORCE_INLINE
	Callback getBuffersFlushedCallback() const {
		return buffersFlushedCallback;
	}

	OXT_FORCE_INLINE
	void clearBuffersFlushedCallback() {
		buffersFlushedCallback = NULL;
	}

	OXT_FORCE_INLINE
	void setBuffersFlushedCallback(Callback callback) {
		buffersFlushedCallback = callback;
	}

	OXT_FORCE_INLINE
	Callback getDataFlushedCallback() const {
		return dataFlushedCallback;
	}

	OXT_FORCE_INLINE
	void setDataFlushedCallback(Callback callback) {
		dataFlushedCallback = callback;
	}

	OXT_FORCE_INLINE
	Hooks *getHooks() const {
		return Channel::hooks;
	}

	OXT_FORCE_INLINE
	void setHooks(Hooks *hooks) {
		Channel::hooks = hooks;
	}

	Json::Value inspectAsJson() const {
		Json::Value doc = Channel::inspectAsJson();

		switch (mode) {
		case IN_MEMORY_MODE:
			doc["mode"] = "IN_MEMORY_MODE";
			break;
		case IN_FILE_MODE:
			doc["mode"] = "IN_FILE_MODE";
			doc["writer_state"] = getWriterStateString();
			doc["read_offset"] = byteSizeToJson(inFileMode->readOffset);
			doc["written"] = signedByteSizeToJson(inFileMode->written);
			break;
		case ERROR:
			doc["mode"] = "ERROR";
			break;
		case ERROR_WAITING:
			doc["mode"] = "ERROR_WAITING";
			break;
		default:
			break;
		}

		doc["reader_state"] = getReaderStateString();
		doc["nbuffers"] = nbuffers;
		doc["bytes_buffered"] = byteSizeToJson(getBytesBuffered());

		return doc;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_FILE_BUFFERED_CHANNEL_H_ */
