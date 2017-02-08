/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_SERVER_KIT_CLIENT_H_
#define _PASSENGER_SERVER_KIT_CLIENT_H_

#include <psg_sysqueue.h>
#include <boost/atomic.hpp>
#include <boost/cstdint.hpp>
#include <ServerKit/Hooks.h>
#include <ServerKit/FdSourceChannel.h>
#include <ServerKit/FileBufferedFdSinkChannel.h>

namespace Passenger {
namespace ServerKit {

using namespace std;
using namespace boost;


class BaseClient {
private:
	/** Reference to the Server that this Client belongs to.
	 * It's a tagged pointer, with the lower 2 bits containing
	 * the connection state. Phusion Passenger is never going
	 * to be used on 16-bit systems anyway.
	 */
	void *server;

public:
	enum ConnState {
		/** Client object is in the server's freelist. No file descriptor
		 * is associated and no I/O operations are possible. From this state,
		 * it can transition to ACTIVE.
		 *
		 * @invariant
		 *     fd == -1
		 */
		IN_FREELIST,

		/** Client object is actively being used. There's a file descriptor
		 * associated and no I/O operations are possible. From this state,
		 * it can transition to either DISCONNECTED or IN_FREELIST.
		 *
		 * @invariant
		 *      fd != -1
		 *      fdnum != -1
		 */
		ACTIVE,

		/** Client object is disconnected, but isn't yet put in the freelist,
		 * because there are still references to the client object. No file
		 * descriptor is associated and no I/O operations are possible. The
		 * original file descriptor number is stored in fdnum for debugging
		 * purposes, but it does not refer to a valid file descriptor.
		 *
		 * @invariant
		 *      fd == -1
		 *      fdnum != -1
		 */
		DISCONNECTED
	};

	boost::atomic<int> refcount;
	Hooks hooks;
	FdSourceChannel input;
	FileBufferedFdSinkChannel output;

	BaseClient(void *_server)
		: server(_server),
		  refcount(2)
	{
		setConnState(DISCONNECTED);
	}

	virtual ~BaseClient() { }

	OXT_FORCE_INLINE
	int getFd() const {
		return input.getFd();
	}

	OXT_FORCE_INLINE
	bool connected() const {
		return getConnState() == ACTIVE;
	}

	OXT_FORCE_INLINE
	ConnState getConnState() const {
		return (ConnState) ((uintptr_t) server & 0x3);
	}

	const char *getConnStateString() const {
		switch (getConnState()) {
		case IN_FREELIST:
			return "IN_FREELIST";
		case ACTIVE:
			return "ACTIVE";
		case DISCONNECTED:
			return "DISCONNECTED";
		default:
			return "UNKNOWN";
		}
	}

	OXT_FORCE_INLINE
	void setConnState(ConnState state) {
		server = (void *) ((uintptr_t) getServerBaseClassPointer() | (uintptr_t) state);
	}

	/**
	 * Returns a pointer to the BaseServer base class object. Using it is dangerous.
	 * You should use BaseServer::getServerFromClient() instead, which provides
	 * better type-safety and which allows safe recasting.
	 */
	OXT_FORCE_INLINE
	void *getServerBaseClassPointer() {
		return (void *) ((uintptr_t) server & ~((uintptr_t) 0x3));
	}
};


#define DEFINE_SERVER_KIT_BASE_CLIENT_FOOTER(ClientType) \
	public: \
	union { \
		STAILQ_ENTRY(ClientType) freeClient; \
		TAILQ_ENTRY(ClientType) activeOrDisconnectedClient; \
	} nextClient; \
	unsigned int number


class Client: public BaseClient {
public:
	Client(void *server)
		: BaseClient(server)
		{ }

	DEFINE_SERVER_KIT_BASE_CLIENT_FOOTER(Client);
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_CLIENT_H_ */
