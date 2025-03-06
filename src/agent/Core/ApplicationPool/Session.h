/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2025 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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
#ifndef _PASSENGER_APPLICATION_POOL_SESSION_H_
#define _PASSENGER_APPLICATION_POOL_SESSION_H_

#include <sys/types.h>
#include <boost/atomic.hpp>
#include <oxt/macros.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <Utils/ScopeGuard.h>
#include <Utils/Lock.h>
#include <Core/ApplicationPool/Context.h>
#include <Core/ApplicationPool/BasicProcessInfo.h>
#include <Core/ApplicationPool/BasicGroupInfo.h>
#include <Core/ApplicationPool/Socket.h>
#include <Core/ApplicationPool/AbstractSession.h>
#include <Shared/ApplicationPoolApiKey.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace oxt;


/**
 * Represents a communication session with a process. A communication session
 * within Phusion Passenger is usually a single request + response but the API
 * allows arbitrary I/O. See Process's class overview for normal usage of Session.
 *
 * A Session object is created from a Process object.
 *
 * This class can be used outside the ApplicationPool lock, because the methods in this
 * class only return immutable data and only modify data inside the Session object.
 * However, it is not thread-safe, and so should only be accessed through 1 thread.
 *
 * You MUST destroy all Session objects before destroying the Context that
 * it was allocated from. Outside unit tests, Context lives in Pool, so
 * so in that case you must not destroy Pool before destroying all Session
 * objects.
 */
class Session: public AbstractSession {
public:
	typedef void (*Callback)(Session *session);

private:
	/**
	 * Pointer to the Context that this Session was allocated from. Always
	 * non-NULL. Allows the Session to free itself from the memory pool
	 * inside the Context.
	 */
	Context * const context;
	/**
	 * Backpointers to Socket that this Session was made from, as well as the immutable info
	 * of the Group and Process that this Session belongs to.
	 *
	 * These are non-NULL if and only if the Session hasn't been closed.
	 * This works because Group waits until all sessions are closed
	 * before destroying a Process.
	 */
	const BasicProcessInfo *processInfo;
	Socket *socket;

	Connection connection;
	mutable boost::atomic<int> refcount;
	bool closed;

	void deinitiate(bool success, bool wantKeepAlive) {
		connection.fail = !success;
		connection.wantKeepAlive = wantKeepAlive;
		socket->checkinConnection(connection);
		connection.fd = -1;
	}

	void callOnInitiateFailure() {
		if (OXT_LIKELY(onInitiateFailure != NULL)) {
			onInitiateFailure(this);
		}
	}

	void callOnClose() {
		if (OXT_LIKELY(onClose != NULL)) {
			onClose(this);
		}
		closed = true;
	}

	void destroySelf() const {
		Context *context = this->context;
		Session *storagePointer = const_cast<Session *>(this);
		this->~Session();

		LockGuard l(context->memoryManagementSyncher);
		// Use `storagePointer` because using `this` after calling the destructor
		// is undefined behavior.
		context->sessionObjectPool.free(storagePointer);
	}

public:
	Callback onInitiateFailure;
	Callback onClose;

	Session(Context *_context, const BasicProcessInfo *_processInfo, Socket *_socket)
		: context(_context),
		  processInfo(_processInfo),
		  socket(_socket),
		  refcount(1),
		  closed(false),
		  onInitiateFailure(NULL),
		  onClose(NULL)
		{ }

	~Session() {
		TRACE_POINT();
		// If user doesn't close() explicitly, we penalize performance.
		if (OXT_LIKELY(initiated())) {
			deinitiate(false, false);
		}
		if (OXT_LIKELY(!closed)) {
			callOnClose();
		}
	}


	Group *getGroup() const {
		assert(!closed);
		return processInfo->groupInfo->group;
	}

	Process *getProcess() const {
		assert(!closed);
		return processInfo->process;
	}


	virtual const ApiKey &getApiKey() const override {
		assert(!closed);
		return processInfo->groupInfo->apiKey;
	}

	virtual pid_t getPid() const override {
		assert(!closed);
		return processInfo->pid;
	}

	virtual StaticString getGupid() const override {
		assert(!closed);
		return StaticString(processInfo->gupid, processInfo->gupidSize);
	}

	virtual unsigned int getStickySessionId() const override {
		assert(!closed);
		return processInfo->stickySessionId;
	}

	Socket *getSocket() const {
		assert(!closed);
		return socket;
	}

	virtual StaticString getProtocol() const override {
		return getSocket()->protocol;
	}

	// See AbstractSession.h for docs
	virtual bool initiate() override {
		assert(!closed);
		ScopeGuard g(boost::bind(&Session::callOnInitiateFailure, this));
		Connection connection = socket->checkoutConnection();
		connection.fail = true;
		g.clear();
		this->connection = connection;
		return connection.ready;
	}

	bool initiated() const {
		return connection.fd != -1;
	}

	virtual int fd() const override {
		assert(!closed);
		return connection.fd;
	}

	/**
	 * This Session object becomes fully unusable after closing.
	 */
	virtual void close(bool success, bool wantKeepAlive = false) override {
		if (OXT_LIKELY(initiated())) {
			deinitiate(success, wantKeepAlive);
		}
		if (OXT_LIKELY(!closed)) {
			callOnClose();
		}
		processInfo = NULL;
		socket = NULL;
	}

	virtual bool isClosed() const override {
		return closed;
	}

	virtual void requestOOBW() override;


	virtual void ref() const override {
		refcount.fetch_add(1, boost::memory_order_relaxed);
	}

	virtual void unref() const override {
		if (refcount.fetch_sub(1, boost::memory_order_release) == 1) {
			boost::atomic_thread_fence(boost::memory_order_acquire);
			destroySelf();
		}
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SESSION_H_ */
