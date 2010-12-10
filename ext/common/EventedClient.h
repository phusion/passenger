/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_EVENTED_CLIENT_H_
#define _PASSENGER_EVENTED_CLIENT_H_

#include <ev++.h>
#include <string>
#include <sys/types.h>
#include <cstdlib>
#include <cerrno>
#include <cassert>

#include <boost/function.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/thread.hpp>

#include "FileDescriptor.h"
#include "Utils/IOUtils.h"

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


/**
 * A utility class for making I/O handling in non-blocking libev evented servers
 * much easier.
 * - An EventedClient is associated with a reference counted file descriptor.
 * - It contains connection state information (i.e. whether the connection is
 *   established or closed). Callbacks are provided for watching connection
 *   state changes (e.g. <tt>onDisconnect</tt>).
 * - It provides reference counting features for simpler memory management
 *   (<tt>ref()</tt> and <tt>unref()</tt>).
 * - It installs input and output readiness watchers that are unregistered
 *   when the EventedClient is destroyed. One can hook into input readiness
 *   watcher with the <tt>onReadable</tt> callback.
 * - Makes zero-copy writes easy. The <tt>write()</tt> method accepts an array
 *   of buffers. Whenever possible, all of these buffers are written out in
 *   the given order, using a single system call, without copying them into a
 *   single temporary buffer.
 * - Makes non-blocking writes easy. Normally a write() system call on a
 *   non-blocking socket can fail with EAGAIN if the socket send buffer is
 *   full. EventedClient schedules the data to be sent later when the socket is
 *   writable again. It automatically integrates into the main loop in order
 *   to do this. This allows one to have write operations occur concurrently
 *   with read operations.
 *   In case too many scheduled writes are being piled up, EventedClient
 *   is smart enough to temporarily disable read notifications and wait until
 *   everything is written out before enabling read notifications again.
 *   The definition of "too many" is customizable (<tt>setOutboxLimit()</tt>).
 * - EventedClient's <tt>disconnect</tt> method respects pending writes. It
 *   will disconnect after all pending outgoing data have been written out.
 *
 * <h2>Basic usage</h2>
 * Construct an EventedClient with a libev loop and a file descriptor:
 *
 * @code
 * EventedClient *client = new EventedClient(loop, fd);
 * @endcode
 *
 * You are probably interested in read readiness notifications on <tt>fd</tt>.
 * However these notifications are disabled by default. You need to set the
 * <tt>onReadable</tt> callback (which is called every time the fd is
 * readable) and enable read notifications.
 *
 * @code
 * void onReadable(EventedClient *client) {
 *     // do whatever you want
 * }
 * 
 * ...
 * client->onReadable = onReadable;
 * client->notifyReads(true);
 * @endcode
 *
 * <h2>Error handling</h2>
 * EventedClient never raises exceptions, except when your callbacks do.
 * It reports errors with the <tt>onSystemError</tt> callback. That said,
 * EventedClient is exception-aware and will ensure that its internal
 * state stays consistent even when your callbacks throw exceptions.
 */
class EventedClient {
public:
	typedef void (*Callback)(EventedClient *client);
	typedef void (*SystemErrorCallback)(EventedClient *client, const string &message, int code);
	
private:
	enum {
		/**
		 * This is the initial state for a client. It means we're
		 * connected to the client, ready to receive data and
		 * there's no pending outgoing data. In this state we will
		 * only be watching for read events.
		 */
		EC_CONNECTED,
		
		/**
		 * This state is entered from EC_CONNECTED when the write()
		 * method fails to send all data immediately and EventedClient
		 * schedules some data to be sent later, when the socket becomes
		 * readable again. In here we will be watching for read
		 * and write events. Once all data has been sent out the system
		 * will transition back to EC_CONNECTED.
		 */
		EC_WRITES_PENDING,
		
		/**
		 * This state is entered from EC_WRITES_PENDING or from EC_CONNECTED
		 * when the write() method fails to send all data immediately, and
		 * the amount of data to be scheduled to be sent later is larger
		 * than the specified outbox limit. In this state, EventedClient
		 * will not watch for read events and will instead concentrate on
		 * sending out all pending data before watching read events again.
		 * When all pending data has been sent out the system will transition
		 * to EC_CONNECTED.
		 */
		EC_TOO_MANY_WRITES_PENDING,
		
		/**
		 * This state is like EC_CONNECTED, but indicates that the write
		 * side of the connection has been closed. In this state write()
		 * calls won't have any effect.
		 */
		EC_RO_CONNECTED,
		
		/**
		 * This state is entered from EC_WRITES_PENDING when
		 * closeWrite() has been called. The system will continue
		 * to send out pending data but write() calls won't append more
		 * data to the outbox. After pending data has been sent out,
		 * the system will transition to EC_RO_CONNECTED.
		 */
		EC_RO_CONNECTED_WITH_WRITES_PENDING,
		
		/**
		 * This state is entered from the EC_WRITES_PENDING,
		 * EC_TOO_MANY_WRITES_PENDING, EC_RO_CONNECTED_WITH_WIRTES_PENDING
		 * or EC_RO_CONNECTED_WITH_TOO_MANY_WRITES_PENDING state when
		 * disconnect() is called.
		 * It means that we want to close the connection as soon as all
		 * pending outgoing data has been sent. As soon as that happens
		 * it'll transition to EC_DISCONNECTED. In this state no further
		 * I/O should be allowed.
		 */
		EC_DISCONNECTING_WITH_WRITES_PENDING,
		
		/**
		 * Final state. Client connection has been closed. No
		 * I/O with the client is possible.
		 */
		EC_DISCONNECTED
	} state;
	
	/** A libev watcher on for watching read events on <tt>fd</tt>. */
	ev::io readWatcher;
	/** A libev watcher on for watching write events on <tt>fd</tt>. */
	ev::io writeWatcher;
	/** Storage for data that could not be sent out immediately. */
	string outbox;
	int refcount;
	unsigned int outboxLimit;
	bool m_notifyReads;
	
	void _onReadable(ev::io &w, int revents) {
		emitEvent(onReadable);
	}
	
	void onWritable(ev::io &w, int revents) {
		assert(state != EC_CONNECTED);
		assert(state != EC_RO_CONNECTED);
		assert(state != EC_DISCONNECTED);
		
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		size_t sent = 0;
		bool done = outbox.empty();
		
		while (!done) {
			ssize_t ret = syscalls::write(fd,
				outbox.data() + sent,
				outbox.size() - sent);
			if (ret == -1) {
				if (errno != EAGAIN) {
					int e = errno;
					if (writeErrorAction == DISCONNECT_FULL) {
						disconnect(true);
					} else {
						closeWrite();
					}
					emitSystemErrorEvent("Cannot write data to client", e);
					return;
				}
				done = true;
			} else {
				sent += ret;
				done = sent == outbox.size();
			}
		}
		if (sent > 0) {
			outbox.erase(0, sent);
		}
		
		updateWatcherStates();
		if (outbox.empty()) {
			emitEvent(onPendingDataFlushed);
		}
	}
	
	bool outboxTooLarge() {
		return outbox.size() > 0 && outbox.size() >= outboxLimit;
	}
	
	void updateWatcherStates() {
		if (outbox.empty()) {
			switch (state) {
			case EC_CONNECTED:
			case EC_RO_CONNECTED:
				watchReadEvents(m_notifyReads);
				watchWriteEvents(false);
				break;
			case EC_WRITES_PENDING:
			case EC_TOO_MANY_WRITES_PENDING:
				state = EC_CONNECTED;
				watchReadEvents(m_notifyReads);
				watchWriteEvents(false);
				break;
			case EC_RO_CONNECTED_WITH_WRITES_PENDING:
				state = EC_RO_CONNECTED;
				watchReadEvents(m_notifyReads);
				watchWriteEvents(false);
				break;
			case EC_DISCONNECTING_WITH_WRITES_PENDING:
				state = EC_DISCONNECTED;
				watchReadEvents(false);
				watchWriteEvents(false);
				try {
					fd.close();
				} catch (const SystemException &e) {
					emitSystemErrorEvent(e.brief(), e.code());
				}
				emitEvent(onDisconnect);
				break;
			default:
				// Should never be reached.
				abort();
			}
		} else {
			switch (state) {
			case EC_CONNECTED:
				if (outboxTooLarge()) {
					// If we have way too much stuff in the outbox then
					// suspend reading until we've sent out the entire outbox.
					state = EC_TOO_MANY_WRITES_PENDING;
					watchReadEvents(false);
					watchWriteEvents(true);
				} else {
					state = EC_WRITES_PENDING;
					watchReadEvents(m_notifyReads);
					watchWriteEvents(true);
				}
				break;
			case EC_RO_CONNECTED:
				fprintf(stderr, "BUG: when outbox is non-empty the state should never be EC_RO_CONNECTED!\n");
				abort();
				break;
			case EC_WRITES_PENDING:
			case EC_RO_CONNECTED_WITH_WRITES_PENDING:
				watchReadEvents(m_notifyReads);
				watchWriteEvents(true);
				break;
			case EC_TOO_MANY_WRITES_PENDING:
			case EC_DISCONNECTING_WITH_WRITES_PENDING:
				watchReadEvents(false);
				watchWriteEvents(true);
				break;
			default:
				// Should never be reached.
				abort();
			}
		}
	}
	
	void watchReadEvents(bool enable = true) {
		if (readWatcher.is_active() && !enable) {
			readWatcher.stop();
		} else if (!readWatcher.is_active() && enable) {
			readWatcher.start();
		}
	}
	
	void watchWriteEvents(bool enable = true) {
		if (writeWatcher.is_active() && !enable) {
			writeWatcher.stop();
		} else if (!writeWatcher.is_active() && enable) {
			writeWatcher.start();
		}
	}
	
	void emitEvent(Callback callback) {
		if (callback != NULL) {
			callback(this);
		}
	}
	
	void emitSystemErrorEvent(const string &message, int code) {
		if (onSystemError != NULL) {
			onSystemError(this, message, code);
		}
	}
	
public:
	/** The client's file descriptor. Could be -1: see <tt>ioAllowed()</tt>. */
	FileDescriptor fd;
	
	/** Controls what to do when a write error is encountered. */
	enum {
		/** Forcefully disconnect the client. */
		DISCONNECT_FULL,
		/** Close the writer side of the connection, but continue allowing reading. */
		DISCONNECT_WRITE
	} writeErrorAction;
	
	/**
	 * Called when the file descriptor becomes readable and read notifications
	 * are enabled (see <tt>notifyRead()</tt>). When there's too much pending
	 * outgoing data, readability notifications are temporarily disabled; see
	 * <tt>write()</tt> for details.
	 */
	Callback onReadable;
	
	/**
	 * Called when the client is disconnected. This happens either immediately
	 * when <tt>disconnect()</tt> is called, or a short amount of time later.
	 * See the documentation for that function for details.
	 *
	 * Please note that destroying an EventedClient object does *not* cause
	 * this callback to be called.
	 */
	Callback onDisconnect;
	
	/**
	 * Called when <tt>detach()</tt> is called for the first time.
	 */
	Callback onDetach;
	
	/**
	 * Called after all pending outgoing data have been written out.
	 * If <tt>write()</tt> can be completed immediately without scheduling
	 * data for later, then <tt>write()</tt> will call this callback
	 * immediately after writing.
	 */
	Callback onPendingDataFlushed;
	
	/**
	 * System call errors are reported with this callback.
	 */
	SystemErrorCallback onSystemError;
	
	/**
	 * EventedClient doesn't do anything with this. Set it to whatever you want.
	 */
	void *userData;
	
	/**
	 * Creates a new EventedClient with the given libev loop and file descriptor.
	 * The initial reference count is 1.
	 */
	EventedClient(struct ev_loop *loop, const FileDescriptor &_fd)
		: readWatcher(loop),
		  writeWatcher(loop),
		  fd(_fd)
	{
		state              = EC_CONNECTED;
		refcount           = 1;
		m_notifyReads      = false;
		outboxLimit        = 1024 * 32;
		writeErrorAction   = DISCONNECT_FULL;
		onReadable         = NULL;
		onDisconnect       = NULL;
		onDetach           = NULL;
		onPendingDataFlushed = NULL;
		onSystemError      = NULL;
		userData           = NULL;
		readWatcher.set(fd, ev::READ);
		readWatcher.set<EventedClient, &EventedClient::_onReadable>(this);
		writeWatcher.set<EventedClient, &EventedClient::onWritable>(this);
		writeWatcher.set(fd, ev::WRITE);
	}
	
	virtual ~EventedClient() {
		// Unregister file descriptor from the event loop poller before
		// closing the file descriptor.
		watchReadEvents(false);
		watchWriteEvents(false);
	}
	
	/**
	 * Increase reference count.
	 */
	void ref() {
		refcount++;
	}
	
	/**
	 * Decrease reference count. Upon reaching 0, this EventedClient object
	 * will be destroyed.
	 */
	void unref() {
		refcount--;
		assert(refcount >= 0);
		if (refcount == 0) {
			delete this;
		}
	}
	
	/**
	 * Returns whether it is allowed to perform some kind of I/O with
	 * this client, either reading or writing.
	 * Usually true, and false when the client is either being disconnected
	 * or has been disconnected. A return value of false indicates that
	 * <tt>fd</tt> might be -1, but even when it isn't -1 you shouldn't
	 * access <tt>fd</tt> anymore.
	 * When the connection is half-closed (e.g. after closeWrite() has
	 * been called) the return value is still be true. Only when I/O of any
	 * kind is disallowed will this function return false.
	 */
	bool ioAllowed() const {
		return state != EC_DISCONNECTING_WITH_WRITES_PENDING
			&& state != EC_DISCONNECTED;
	}
	
	/**
	 * Returns whether it is allowed to write data to the client.
	 * Usually true, and false when the client is either being disconnected
	 * or has been disconnected or when the writer side of the client
	 * connection has been closed. write() will do nothing if this function
	 * returns false.
	 */
	bool writeAllowed() const {
		return state == EC_CONNECTED
			|| state == EC_WRITES_PENDING
			|| state == EC_TOO_MANY_WRITES_PENDING
			|| state == EC_RO_CONNECTED_WITH_WRITES_PENDING;
	}
	
	/** Used by unit tests. */
	bool readWatcherActive() const {
		return readWatcher.is_active();
	}
	
	/**
	 * Returns the number of bytes that are scheduled to be sent to the
	 * client at a later time.
	 * 
	 * @see write()
	 */
	size_t pendingWrites() const {
		return outbox.size();
	}
	
	/**
	 * Sets whether you're interested in read events. This will start or
	 * stop the input readiness watcher appropriately according to the
	 * current state.
	 *
	 * If the client connection is already being closed or has already
	 * been closed then this method does nothing.
	 */
	void notifyReads(bool enable) {
		if (!ioAllowed()) {
			return;
		}
		
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		m_notifyReads = enable;
		updateWatcherStates();
	}
	
	/**
	 * Sets a limit on the client outbox. The outbox is where data is stored
	 * that could not be immediately sent to the client, e.g. because of
	 * network congestion. Whenver the outbox's size grows past this limit,
	 * EventedClient will enter a state in which it will stop listening for
	 * read events and instead concentrate on sending out all pending data.
	 *
	 * Setting this to 0 means that the outbox has an unlimited size. Please
	 * note however that this also means that the outbox's memory could grow
	 * unbounded if the client is too slow at receiving data.
	 *
	 * The default value is some non-zero value.
	 *
	 * If the client connection is already being closed or has already
	 * been closed then this method does nothing.
	 */
	void setOutboxLimit(unsigned int size) {
		if (!ioAllowed()) {
			return;
		}
		
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		outboxLimit = size;
		updateWatcherStates();
	}
	
	void write(const StaticString &data) {
		write(&data, 1);
	}
	
	/**
	 * Sends data to this client. This method will try to send the data
	 * immediately (in which no intermediate copies of the data will be made),
	 * but if the client is not yet ready to receive data (e.g. because of
	 * network congestion) then the data will be buffered and scheduled for
	 * sending later.
	 *
	 * If an I/O error was encountered then the action taken depends on the
	 * value of <em>writeActionError</em>. By default it is DISCONNECT_FULL,
	 * meaning the client connection will be closed by calling
	 * <tt>disconnect(true)</tt>. This means this method could potentially
	 * call the <tt>onDisconnect</tt> callback.
	 *
	 * If the client connection is already being closed, has already
	 * been closed or if the writer side is closed, then this method does
	 * nothing.
	 *
	 * The <tt>onPendingDataFlushed</tt> callback will be called after
	 * this data and whatever existing pending data have been written
	 * out. That may either be immediately or after a short period of
	 * of time.
	 */
	void write(const StaticString data[], unsigned int count) {
		if (!writeAllowed()) {
			return;
		}
		
		ssize_t ret;
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		
		ret = gatheredWrite(fd, data, count, outbox);
		if (ret == -1) {
			int e = errno;
			if (writeErrorAction == DISCONNECT_FULL) {
				disconnect(true);
			} else {
				closeWrite();
			}
			emitSystemErrorEvent("Cannot write data to client", e);
		} else {
			updateWatcherStates();
			if (outbox.empty()) {
				emitEvent(onPendingDataFlushed);
			}
		}
	}
	
	/**
	 * Close only the writer side of the client connection.
	 * After calling this method, subsequent write() calls won't do anything
	 * anymore. Any pending outgoing data will be sent out whenever the
	 * opportunity arises.
	 *
	 * This function does nothing if the client is being disconnected,
	 * already disconnected or if only the writer side is closed.
	 */
	void closeWrite() {
		this_thread::disable_syscall_interruption dsi;
		
		switch (state) {
		case EC_CONNECTED:
			assert(outbox.empty());
			state = EC_RO_CONNECTED;
			if (syscalls::shutdown(fd, SHUT_WR) == -1) {
				int e = errno;
				emitSystemErrorEvent(
					"Cannot shutdown writer half of the client socket",
					e);
			}
			break;
		case EC_WRITES_PENDING:
		case EC_TOO_MANY_WRITES_PENDING:
			state = EC_RO_CONNECTED_WITH_WRITES_PENDING;
			break;
		default:
			break;
		}
		updateWatcherStates();
	}
	
	/**
	 * Disconnects the client. This actually closes the underlying file
	 * descriptor, even if the FileDescriptor object still has references.
	 *
	 * If <em>force</em> is true then the client will be disconnected
	 * immediately, and any pending outgoing data will be discarded.
	 * Otherwise the client will be disconnected after all pending
	 * outgoing data have been sent; in the mean time no new data can be
	 * received from or sent to the client.
	 *
	 * After the client has actually been disconnected (which may be either
	 * immediately or after a short period of time), a disconnect event will
	 * be emitted.
	 *
	 * If the client connection has already been closed then this method
	 * does nothing. If the client connection is being closed (because
	 * there's pending outgoing data) then the behavior depends on the
	 * <tt>force</tt> argument: if true then the connection is closed
	 * immediately and the pending data is discarded, otherwise this
	 * method does nothing.
	 *
	 * The <tt>onDisconnect</tt> callback will be called after the file
	 * descriptor is closed, which is either immediately or after all
	 * pending data has been sent out.
	 */
	void disconnect(bool force = false) {
		if (!ioAllowed() && !(state == EC_DISCONNECTING_WITH_WRITES_PENDING && force)) {
			return;
		}
		
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		
		if (state == EC_CONNECTED || state == EC_RO_CONNECTED || force) {
			state = EC_DISCONNECTED;
			watchReadEvents(false);
			watchWriteEvents(false);
			try {
				fd.close();
			} catch (const SystemException &e) {
				emitSystemErrorEvent(e.brief(), e.code());
			}
			emitEvent(onDisconnect);
		} else {
			state = EC_DISCONNECTING_WITH_WRITES_PENDING;
			watchReadEvents(false);
			watchWriteEvents(true);
			if (syscalls::shutdown(fd, SHUT_RD) == -1) {
				int e = errno;
				emitSystemErrorEvent(
					"Cannot shutdown reader half of the client socket",
					e);
			}
		}
	}
	
	/**
	 * Detaches the client file descriptor so that this EventedClient no longer
	 * has any control over it. Any EventedClient I/O watchers on the client file
	 * descriptor will be stopped and further I/O on the file descriptor via
	 * EventedClient will become impossible. Any pending outgoing data will be
	 * discarded. The original client file descriptor is returned and
	 * <tt>onDetach</tt> is called. Subsequent calls to this function will
	 * return -1 and will no longer call <tt>onDetach</tt>.
	 *
	 * @post !ioAllowed()
	 * @post fd == -1
	 */
	FileDescriptor detach() {
		if (state == EC_DISCONNECTED) {
			return fd;
		} else {
			FileDescriptor oldFd = fd;
			state = EC_DISCONNECTED;
			watchReadEvents(false);
			watchWriteEvents(false);
			fd = -1;
			emitEvent(onDetach);
			return oldFd;
		}
	}
};


} // namespace Passenger

#endif /* _PASSENGER_EVENTED_CLIENT_H_ */
