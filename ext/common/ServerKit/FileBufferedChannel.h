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
#ifndef _PASSENGER_SERVER_KIT_FILE_BUFFERED_CHANNEL_H_
#define _PASSENGER_SERVER_KIT_FILE_BUFFERED_CHANNEL_H_

#include <boost/cstdint.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/move/move.hpp>
#include <sys/types.h>
#include <eio.h>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <utility>
#include <string>
#include <deque>
#include <Logging.h>
#include <ServerKit/Context.h>
#include <ServerKit/Channel.h>

namespace Passenger {
namespace ServerKit {

using namespace std;

#define FBC_DEBUG(expr) \
	P_TRACE(3, "[FBC " << (void *) this << "] " << expr)
#define FBC_DEBUG_FROM_STATIC(expr) \
	P_TRACE(3, "[FBC " << (void *) self << "] " << expr)


/**
 * Adds "unlimited" buffering capability to a Channel. A Channel has a buffer size
 * of 1, which is why you can't write to a Channel until the previously written
 * data is consumed. But with FileBufferedChannel, everything you write to it
 * is either buffered to memory, or to disk. If the total amount of buffered data is
 * below a threshold, everything is buffered in memory. Beyond the threshold, buffered
 * data will be written to disk and freed from memory. This allows you to buffer
 * a virtually unlimited amount of data, without using a lot of memory.
 *
 * ## Implementation
 *
 * FileBufferedChannel operates by default in the in-memory mode. All data is buffered
 * in memory. Beyond a threshold (determined by `passedThreshold()`), it switches
 * to in-file mode.
 *
 * FileBufferedChannel is composed of 3 subsystems:
 *
 * - The `feed()` method puts commands on an internal queue for other subsystems
 *   to process.
 * - The writer writes the buffers associated with the commands to a temp file as
 *   quickly as it can, and frees these buffers from memory.
 * - The reader reads from the temp file and the internal queue as quickly as it
 *   can, and feeds the associated buffers to the underlying Channel.
 *   When the reader has consumed all data, it tells the writer to truncate the
 *   file.
 *
 * When data is buffered to a file, we keep the following states:
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
 *     |   unwritten buffer 2   |    |---- buffered
 *     +------------------------+    |     (sum of all unwritten buffers' sizes)
 *     |          ....          |  --+
 *     +------------------------+
 */
class FileBufferedChannel: protected Channel {
public:
	// `buffered` is 25-bit. This is 2^25-1, or 32 MB.
	static const unsigned int MAX_MEMORY_BUFFERING = 33554431;

	typedef Channel::Result (*DataCallback)(FileBufferedChannel *channel, const MemoryKit::mbuf &buffer, int errcode);
	typedef void (*Callback)(FileBufferedChannel *channel);

private:
	typedef void (*IdleCallback)(FileBufferedChannel *channel);

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

	/**
	 * Holds all states for the in-file mode. Reasons why this is a separate
	 * structure:
	 *
	 * - We can keep the size of the FileBufferedChannel small for the common,
	 *   fast case where the consumer can keep up with the writes.
	 * - We improve the clarity of the code by clearly grouping variables
	 *   that are only used in the in-file mode.
	 * - While eio operations are in progress, they hold a smart pointer to the
	 *   InFileMode structure, which ensures that the file descriptor that they
	 *   operate on stays open until all eio operations have finished (or until
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
		eio_req *readRequest;


		/***** Writer state *****/

		WriterState writerState;

		/**
		 * The write operation that the writer is currently performing. Might be
		 * an `eio_open()`, `eio_write()`, or whatever.
		 *
		 * @invariant
		 *     (writerRequest != NULL) == (writerState == WS_CREATING_FILE || writerState == WS_MOVING)
		 */
		eio_req *writerRequest;

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

		InFileMode()
			: fd(-1),
			  readRequest(NULL),
			  writerState(WS_INACTIVE),
			  writerRequest(NULL),
			  readOffset(0),
			  written(0)
			{ }

		~InFileMode() {
			assert(readRequest == NULL);
			assert(writerRequest == NULL);
			if (fd != -1) {
				eio_close(fd, 0, NULL, NULL);
			}
		}
	};

	enum {
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
		ERROR
	} mode;

	enum {
		/** The reader isn't active. It will be activated next time a buffer
		 * is pushed to the queue.
		 *
		 * Invariant 1:
		 * The buffer queue is empty.
		 *
		 *     nbuffers == 0
		 *
		 * Invariant 2:
		 * We must be in the in-memory mode. Being in the in-file mode means that there's
		 * still data available to read. It's not allowed for the reader to be inactive
		 * while there is actually data to be available. It's not possible to be inactive
		 * in the error mode, because in the error mode the reader state is RS_TERMINATED.
		 *
		 *     mode == IN_MEMORY_MODE
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
		 *     mode != ERROR
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
	} readerState;

	/** Number of buffers in `firstBuffer` + `moreBuffers`. */
	boost::uint16_t nbuffers;

	/**
	 * If an error is encountered, its details are stored here.
	 *
	 * @invariant
	 *     (errcode == 0) == (mode != ERROR)
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
	boost::uint32_t bytesBuffered;
	MemoryKit::mbuf firstBuffer;
	deque<MemoryKit::mbuf> moreBuffers;

	/**
	 * @invariant
	 *     (inFileMode != NULL) == (mode == IN_FILE_MODE)
	 */
	boost::shared_ptr<InFileMode> inFileMode;

	IdleCallback idleCallback;


	/***** Buffer manipulation *****/

	void clearBuffers() {
		nbuffers = 0;
		bytesBuffered = 0;
		firstBuffer = MemoryKit::mbuf();
		if (!moreBuffers.empty()) {
			// Some STL implementations, like OS X's, iterate through
			// the deque in its clear() implementation, so adding
			// a conditional here improves performance slightly.
			moreBuffers.clear();
		}
	}

	void pushBuffer(const MemoryKit::mbuf &buffer) {
		assert(bytesBuffered + buffer.size() <= MAX_MEMORY_BUFFERING);
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
		if (moreBuffers.empty()) {
			firstBuffer = MemoryKit::mbuf();
		} else {
			firstBuffer = moreBuffers.front();
			moreBuffers.pop_front();
		}
		FBC_DEBUG("popBuffer() completed: nbuffers = " << nbuffers << ", bytesBuffered = " << bytesBuffered);
		if (nbuffers == 0) {
			callBuffersFlushedCallback();
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
		if (buffersFlushedCallback != NULL) {
			FBC_DEBUG("Calling buffersFlushedCallback");
			buffersFlushedCallback(this);
		}
	}

	void callDataFlushedCallback() {
		if (dataFlushedCallback != NULL) {
			FBC_DEBUG("Calling dataFlushedCallback");
			dataFlushedCallback(this);
		}
	}


	/***** Reader *****/

	void readNext() {
		if (readerState != RS_INACTIVE) {
			return;
		}

		RefGuard guard(hooks, this);
		readNextWithoutRefGuard();
	}

	void readNextWithoutRefGuard() {
		begin:
		FBC_DEBUG("Reader: reading next");
		assert(Channel::state == IDLE);
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
				Channel::feedWithoutRefGuard(peekBuffer());
				if (generation != this->generation || mode == ERROR) {
					// Callback deinitialized this object, or callback
					// called a method that encountered an error.
					return;
				}
				assert(readerState == RS_FEEDING_EOF);
				verifyInvariants();
				FBC_DEBUG("Reader: EOF fed. Transitioning to RS_TERMINATED");
				terminateReaderBecauseOfEOF();
			} else {
				MemoryKit::mbuf buffer(peekBuffer());
				FBC_DEBUG("Reader: found buffer, " << buffer.size() << " bytes");
				popBuffer();
				if (generation != this->generation || mode == ERROR) {
					// buffersFlushedCallback deinitialized this object, or callback
					// called a method that encountered an error.
					return;
				}
				readerState = RS_FEEDING;
				FBC_DEBUG("Reader: feeding buffer, " << buffer.size() << " bytes");
				Channel::feedWithoutRefGuard(buffer);
				if (generation != this->generation || mode == ERROR) {
					// Callback deinitialized this object, or callback
					// called a method that encountered an error.
					return;
				}
				assert(readerState == RS_FEEDING);
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
					FBC_DEBUG("Reader: no more buffers. Transitioning to RS_INACTIVE, truncating file");
					readerState = RS_INACTIVE;
					if (nbuffers == 0 && inFileMode->written == 0) {
						// We've processed all memory buffers. Now is a good time
						// to truncate the file.
						cancelWriter();
						switchToInMemoryMode();
					}
					verifyInvariants();
					callDataFlushedCallback();
				} else if (result.first.empty()) {
					FBC_DEBUG("Reader: EOF encountered. Feeding EOF");
					readerState = RS_FEEDING_EOF;
					verifyInvariants();
					Channel::feedWithoutRefGuard(result.first);
					if (generation != this->generation || mode == ERROR) {
						// Callback deinitialized this object, or callback
						// called a method that encountered an error.
						return;
					}
					assert(readerState == RS_FEEDING_EOF);
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
					if (generation != this->generation || mode == ERROR) {
						// Callback deinitialized this object, or callback
						// called a method that encountered an error.
						return;
					}
					assert(readerState == RS_FEEDING);
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
		readerState = RS_INACTIVE;
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

	struct ReadContext {
		FileBufferedChannel *self;
		MemoryKit::mbuf buffer;
		// Smart pointer to keep fd open until eio operation
		// is finished.
		boost::shared_ptr<InFileMode> inFileMode;
	};

	void readNextChunkFromFile() {
		size_t size = std::min<size_t>(inFileMode->written, ctx->mbuf_pool.mbuf_block_chunk_size);
		FBC_DEBUG("Reader: reading next chunk from file");
		verifyInvariants();
		ReadContext *readContext = new ReadContext();
		readContext->self = this;
		readContext->buffer = MemoryKit::mbuf_get(&ctx->mbuf_pool);
		readContext->inFileMode = inFileMode;
		readerState = RS_READING_FROM_FILE;
		inFileMode->readRequest = eio_read(inFileMode->fd, readContext->buffer.start,
			size, inFileMode->readOffset, 0, _nextChunkDoneReading, readContext);
		verifyInvariants();
	}

	static int _nextChunkDoneReading(eio_req *req) {
		ReadContext *readContext = (ReadContext *) req->data;
		if (EIO_CANCELLED(req)) {
			delete readContext;
			return 0;
		}

		MemoryKit::mbuf buffer(boost::move(readContext->buffer));
		FileBufferedChannel *self = readContext->self;
		delete readContext;
		return self->nextChunkDoneReading(req, buffer);
	}

	int nextChunkDoneReading(eio_req *req, MemoryKit::mbuf &buffer) {
		RefGuard guard(hooks, this);

		FBC_DEBUG("Reader: done reading chunk: " << buffer.size() << " bytes");
		assert(readerState == RS_READING_FROM_FILE);
		verifyInvariants();
		inFileMode->readRequest = NULL;

		if (req->result != -1) {
			unsigned int generation = this->generation;

			assert(req->result <= inFileMode->written);
			buffer = MemoryKit::mbuf(buffer, 0, req->result);
			inFileMode->readOffset += buffer.size();
			inFileMode->written -= buffer.size();

			FBC_DEBUG("Reader: feeding buffer, " << buffer.size() << " bytes");
			readerState = RS_FEEDING;
			Channel::feedWithoutRefGuard(buffer);
			if (generation != this->generation || mode != ERROR) {
				// Callback deinitialized this object, or callback
				// called a method that encountered an error.
				return 0;
			}
			assert(readerState == RS_FEEDING);
			verifyInvariants();
			if (acceptingInput()) {
				readerState = RS_INACTIVE;
				readNext();
			} else if (mayAcceptInputLater()) {
				readNextWhenChannelIdle();
			} else {
				FBC_DEBUG("Reader: data callback no longer accepts further data");
				terminateReaderBecauseOfEOF();
			}
		} else {
			setError(req->errorno);
		}
		return 0;
	}

	// Returns (mbuf, found).
	pair<MemoryKit::mbuf, bool> findBufferForReadProcessing() {
		assert(mode == IN_FILE_MODE);

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
				it++;
				offset += it->size();
			}
		}

		return make_pair(MemoryKit::mbuf(), false);
	}


	/***** Switching to or resetting in-file mode *****/

	void switchToInFileMode() {
		assert(mode == IN_MEMORY_MODE);
		assert(inFileMode == NULL);

		FBC_DEBUG("Switching to in-file mode");
		mode = IN_FILE_MODE;
		inFileMode = make_shared<InFileMode>();
		createBufferFile();
	}

	/**
	 * "Truncates" the the temp file by closing it and creating
	 * a new one, instead of calling `ftruncate()` or something.
	 * This way, any pending I/O operations in the background won't
	 * affect correctness.
	 */
	void switchToInMemoryMode() {
		assert(mode == IN_FILE_MODE);
		assert(bytesBuffered == 0);
		assert(inFileMode->writerState == WS_INACTIVE);
		assert(inFileMode->written == 0);

		FBC_DEBUG("Recreating file, switching to in-memory mode");
		mode = IN_MEMORY_MODE;
		inFileMode.reset();
	}


	/***** File creator *****/

	struct FileCreationContext {
		FileBufferedChannel *self;
		string path;
	};

	void createBufferFile() {
		assert(mode == IN_FILE_MODE);
		assert(inFileMode->writerState == WS_INACTIVE);
		assert(inFileMode->fd == -1);
		verifyInvariants();

		FileCreationContext *fcContext = new FileCreationContext();
		fcContext->self = this;
		fcContext->path = "/tmp/buffer.";
		fcContext->path.append(toString(rand()));

		FBC_DEBUG("Writer: creating file " << fcContext->path);
		inFileMode->writerState = WS_CREATING_FILE;
		inFileMode->writerRequest = eio_open(fcContext->path.c_str(),
			O_RDWR | O_CREAT | O_EXCL, 0600, 0,
			bufferFileCreated, fcContext);
		verifyInvariants();
	}

	static int bufferFileCreated(eio_req *req) {
		FileCreationContext *fcContext = static_cast<FileCreationContext *>(req->data);
		FileBufferedChannel *self = fcContext->self;

		if (EIO_CANCELLED(req)) {
			if (req->result != -1) {
				FBC_DEBUG_FROM_STATIC("Writer: creation of file " << fcContext->path <<
					"canceled. Deleting file in the background");
				eio_unlink(fcContext->path.c_str(), 0, bufferFileUnlinked, fcContext);
				eio_close(req->result, 0, NULL, NULL);
			}
			return 0;
		}

		assert(self->inFileMode->writerState == WS_CREATING_FILE);
		self->verifyInvariants();
		self->inFileMode->writerRequest = NULL;

		if (req->result != -1) {
			FBC_DEBUG_FROM_STATIC("Writer: file created. Deleting file in the background");
			eio_unlink(fcContext->path.c_str(), 0, bufferFileUnlinked, fcContext);
			self->inFileMode->fd = req->result;
			self->moveNextBufferToFile();
		} else {
			delete fcContext;
			if (req->errorno == EEXIST) {
				FBC_DEBUG_FROM_STATIC("Writer: file already exists, retrying");
				self->inFileMode->writerState = WS_INACTIVE;
				self->createBufferFile();
			} else {
				self->setError(req->errorno);
			}
		}
		return 0;
	}

	static int bufferFileUnlinked(eio_req *req) {
		FileCreationContext *fcContext = static_cast<FileCreationContext *>(req->data);
		FileBufferedChannel *self = fcContext->self;

		if (EIO_CANCELLED(req)) {
			delete fcContext;
			return 0;
		}

		if (req->result != -1) {
			FBC_DEBUG_FROM_STATIC("Writer: file " << fcContext->path << " deleted");
		} else {
			FBC_DEBUG_FROM_STATIC("Writer: failed to delete " << fcContext->path <<
				": errno=" << req->errorno << " (" << strerror(req->errorno) << ")");
		}

		delete fcContext;

		return 0;
	}


	/***** Mover *****/

	struct MoveContext {
		FileBufferedChannel *self;
		// Smart pointer to keep fd open until eio operation
		// is finished.
		boost::shared_ptr<InFileMode> inFileMode;
		MemoryKit::mbuf buffer;
		size_t written;
	};

	void moveNextBufferToFile() {
		assert(mode == IN_FILE_MODE);
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

		MoveContext *moveContext = new MoveContext();
		moveContext->self = this;
		moveContext->inFileMode = inFileMode;
		moveContext->buffer = peekBuffer();
		moveContext->written = 0;

		inFileMode->writerState = WS_MOVING;
		inFileMode->writerRequest = eio_write(inFileMode->fd,
			moveContext->buffer.start,
			moveContext->buffer.size(),
			inFileMode->readOffset + inFileMode->written,
			0, bufferWrittenToFile, moveContext);
		verifyInvariants();
	}

	static int bufferWrittenToFile(eio_req *req) {
		MoveContext *moveContext = static_cast<MoveContext *>(req->data);
		FileBufferedChannel *self = moveContext->self;

		if (EIO_CANCELLED(req)) {
			delete moveContext;
			return 0;
		}

		assert(self->mode == IN_FILE_MODE);
		assert(!self->peekBuffer().empty());
		self->verifyInvariants();
		self->inFileMode->writerRequest = NULL;

		if (req->result != -1) {
			moveContext->written += req->result;
			assert(moveContext->written <= moveContext->buffer.size());

			if (moveContext->written == moveContext->buffer.size()) {
				// Write completed. Proceed with next buffer.
				RefGuard guard(self->hooks, self);
				unsigned int generation = self->generation;

				FBC_DEBUG_FROM_STATIC("Writer: move complete");
				delete moveContext;

				assert(self->peekBuffer().size() == moveContext->buffer.size());
				self->inFileMode->written += moveContext->buffer.size();
				self->popBuffer();
				if (generation != self->generation || self->mode == ERROR) {
					// buffersFlushedCallback deinitialized this object, or callback
					// called a method that encountered an error.
					return 0;
				}

				self->moveNextBufferToFile();
			} else {
				FBC_DEBUG_FROM_STATIC("Writer: move incomplete, proceeding " <<
					"with writing rest of buffer");
				self->inFileMode->writerRequest = eio_write(self->inFileMode->fd,
					moveContext->buffer.start + moveContext->written,
					moveContext->buffer.size() - moveContext->written,
					self->inFileMode->readOffset + self->inFileMode->written,
					0, bufferWrittenToFile, moveContext);
				self->verifyInvariants();
			}
		} else {
			FBC_DEBUG_FROM_STATIC("Writer: file write failed");
			delete moveContext;
			self->inFileMode->writerState = WS_TERMINATED;
			self->setError(req->errorno);
		}
		return 0;
	}


	/***** Misc *****/

	void setError(int errcode) {
		assert(errcode != 0);
		FBC_DEBUG("Reader: setting error: errno=" << errcode <<
			" (" << strerror(errcode) << ")");
		cancelReader();
		if (mode == IN_FILE_MODE) {
			cancelWriter();
		}
		mode = ERROR;
		readerState = RS_TERMINATED;
		this->errcode = errcode;
		inFileMode.reset();
		if (acceptingInput()) {
			FBC_DEBUG("Feeding error");
			feedError(errcode);
		} else {
			FBC_DEBUG("Waiting until underlying channel becomes idle for error feeding");
			idleCallback = feedErrorWhenIdle;
		}
	}

	static void feedErrorWhenIdle(FileBufferedChannel *self) {
		assert(self->errcode != 0);
		self->idleCallback = NULL;
		FBC_DEBUG_FROM_STATIC("Channel has become idle. Feeding error");
		self->feedError(self->errcode);
	}

	/**
	 * Must be used in combination with `setError()`, so that the reader will
	 * stop processing after returning from `Channel::feed()`.
	 */
	void cancelReader() {
		switch (readerState) {
		case RS_FEEDING:
		case RS_FEEDING_EOF:
			break;
		case RS_WAITING_FOR_CHANNEL_IDLE:
			idleCallback = NULL;
			break;
		case RS_READING_FROM_FILE:
			eio_cancel(inFileMode->readRequest);
			inFileMode->readRequest = NULL;
			break;
		case RS_INACTIVE:
		case RS_TERMINATED:
			return;
		}
	}

	void cancelWriter() {
		assert(mode == IN_FILE_MODE);

		switch (inFileMode->writerState) {
		case WS_INACTIVE:
			break;
		case WS_CREATING_FILE:
		case WS_MOVING:
			eio_cancel(inFileMode->writerRequest);
			inFileMode->writerRequest = NULL;
			break;
		case WS_TERMINATED:
			return;
		}
		inFileMode->writerState = WS_INACTIVE;
	}

	void verifyInvariants() const {
		#ifndef NDEBUG
			if (mode == ERROR) {
				assert(readerState == RS_TERMINATED);
				assert(inFileMode == NULL);
			}

			switch (readerState) {
			case RS_INACTIVE:
				assert(nbuffers == 0);
				assert(mode == IN_MEMORY_MODE);
				break;
			case RS_FEEDING:
			case RS_FEEDING_EOF:
				break;
			case RS_WAITING_FOR_CHANNEL_IDLE:
				assert(mode != ERROR);
				break;
			case RS_READING_FROM_FILE:
				assert(mode == IN_FILE_MODE);
				assert(inFileMode->readRequest != NULL);
				assert(inFileMode->written > 0);
				break;
			case RS_TERMINATED:
				break;
			}

			assert((errcode == 0) == (mode != ERROR));
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
		}
	}

public:
	Callback buffersFlushedCallback;
	Callback dataFlushedCallback;

	FileBufferedChannel()
		: mode(IN_MEMORY_MODE),
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
	}

	void feed(const MemoryKit::mbuf &buffer) {
		RefGuard guard(hooks, this);

		FBC_DEBUG("Feeding " << buffer.size() << " bytes");
		verifyInvariants();
		if (ended()) {
			FBC_DEBUG("Feeding aborted: EOF or error detected");
			return;
		}
		pushBuffer(buffer);
		if (mode == IN_MEMORY_MODE && passedThreshold()) {
			switchToInFileMode();
		}
		readNextWithoutRefGuard();
	}

	void feed(const char *data, unsigned int size) {
		feed(MemoryKit::mbuf(data, size));
	}

	void feed(const char *data) {
		feed(MemoryKit::mbuf(data));
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
		clearBuffers();
		mode = IN_MEMORY_MODE;
		readerState = RS_INACTIVE;
		errcode = 0;
		inFileMode.reset();
		Channel::deinitialize();
	}

	void start() {
		Channel::start();
	}

	void stop() {
		Channel::stop();
	}

	Channel::State getState() const {
		return state;
	}

	bool ended() const {
		return (hasBuffers() && peekLastBuffer().empty()) || mode == ERROR || Channel::ended();
	}

	bool endAcked() const {
		return Channel::endAcked();
	}

	bool passedThreshold() const {
		return bytesBuffered >= 1024 * 128;
	}

	void setDataCallback(DataCallback callback) {
		Channel::dataCallback = (Channel::DataCallback) callback;
	}

	OXT_FORCE_INLINE
	Hooks *getHooks() const {
		return Channel::hooks;
	}

	void setHooks(Hooks *hooks) {
		Channel::hooks = hooks;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_FILE_BUFFERED_CHANNEL_H_ */
