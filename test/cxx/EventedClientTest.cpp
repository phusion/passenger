#include "TestSupport.h"
#include "EventedClient.h"
#include "Utils/ScopeGuard.h"
#include "Utils/IOUtils.h"

#include <oxt/thread.hpp>

using namespace Passenger;
using namespace std;
using namespace boost;
using namespace oxt;

namespace tut {
	struct EventedClientTest {
		FileDescriptor fd1, fd2;
		ev::dynamic_loop eventLoop;
		ev::async exitWatcher;
		oxt::thread *thr;
		string lastErrorMessage;
		int lastErrorCode;
		AtomicInt integer;
		string data;
		
		EventedClientTest()
			: exitWatcher(eventLoop)
		{
			SocketPair sockets = createUnixSocketPair();
			fd1 = sockets.first;
			fd2 = sockets.second;
			setNonBlocking(fd2);
			
			exitWatcher.set<EventedClientTest, &EventedClientTest::unloop>(this);
			exitWatcher.start();
			thr = NULL;
			lastErrorCode = -1;
		}
		
		~EventedClientTest() {
			stopEventLoop();
		}
		
		void startEventLoop() {
			thr = new oxt::thread(
				boost::bind(&EventedClientTest::runLoop, this)
			);
		}
		
		void stopEventLoop() {
			if (thr != NULL) {
				exitWatcher.send();
				waitUntilEventLoopExits();
			}
		}
		
		void waitUntilEventLoopExits() {
			if (thr != NULL) {
				thr->join();
				delete thr;
				thr = NULL;
			}
		}
		
		void runLoop() {
			eventLoop.loop();
		}
		
		void unloop(ev::async &watcher, int revents) {
			eventLoop.unloop();
		}
		
		static void setIntToOne(EventedClient *client) {
			EventedClientTest *test = (EventedClientTest *) client->userData;
			test->integer = 1;
		}
		
		static void setIntToTwo(EventedClient *client) {
			EventedClientTest *test = (EventedClientTest *) client->userData;
			test->integer = 2;
		}
		
		static void saveSystemError(EventedClient *client, const string &message, int code) {
			EventedClientTest *self = (EventedClientTest *) client->userData;
			self->lastErrorMessage = message;
			self->lastErrorCode = code;
		}
		
		static void exitEventLoop(EventedClient *client) {
			EventedClientTest *self = (EventedClientTest *) client->userData;
			self->eventLoop.unloop();
		}
		
		static void readAndExitOnEof(EventedClient *client) {
			EventedClientTest *self = (EventedClientTest *) client->userData;
			char buf[1024];
			ssize_t ret = read(client->fd, buf, sizeof(buf));
			if (ret <= 0) {
				self->eventLoop.unloop();
			} else {
				self->data.append(buf, ret);
			}
		}
	};

	DEFINE_TEST_GROUP(EventedClientTest);
	
	#define EVENT_LOOP_GUARD ScopeGuard g(boost::bind(&EventedClientTest::stopEventLoop, this))

	TEST_METHOD(1) {
		// It doesn't watch read events by default.
		EventedClient client(eventLoop, fd2);
		write(fd1, "x", 1);
		client.userData = this;
		client.onReadable = EventedClientTest::setIntToOne;
		
		EVENT_LOOP_GUARD;
		startEventLoop();
		
		SHOULD_NEVER_HAPPEN(100,
			result = integer == 1;
		);
	}
	
	TEST_METHOD(2) {
		// It watches read events after calling notifyReads(true).
		EventedClient client(eventLoop, fd2);
		write(fd1, "x", 1);
		client.userData = this;
		client.onReadable = EventedClientTest::setIntToOne;
		client.notifyReads(true);
		
		EVENT_LOOP_GUARD;
		startEventLoop();
		
		EVENTUALLY(1,
			result = integer == 1;
		);
	}
	
	TEST_METHOD(3) {
		// I/O is allowed in the initial state.
		EventedClient client(eventLoop, fd2);
		ensure(client.ioAllowed());
	}
	
	TEST_METHOD(4) {
		// EventedClientTest.write() writes all data to the socket
		// immediately whenever possible.
		StaticString data[] = { "hello ", "world" };
		EventedClient client(eventLoop, fd2);
		client.write(data, 2);
		ensure_equals(client.pendingWrites(), 0u);
		
		EVENT_LOOP_GUARD;
		startEventLoop();
		
		char buf[100];
		memset(buf, 0, sizeof(buf));
		ensure(readExact(fd1, buf, strlen("hello world")));
		ensure_equals(string(buf), "hello world");
	}
	
	TEST_METHOD(5) {
		// If EventedClientTest.write() cannot write out all data immediately,
		// and the remaining data is within the outbox limit, then it ensures
		// that all pending data is written to the socket eventually.
		// readWatcher will be active in the mean time.
		string str(1024 * 256, '\1');
		StaticString data = str;
		EventedClient client(eventLoop, fd2);
		client.setOutboxLimit(str.size() + 1);
		client.write(&data, 1);
		ensure(client.pendingWrites() > 0);
		
		client.userData = this;
		client.onReadable = EventedClientTest::setIntToOne;
		client.notifyReads(true);
		ensure(client.readWatcherActive());
		
		EVENT_LOOP_GUARD;
		startEventLoop();
		
		char buf[str.size()];
		memset(buf, 0, sizeof(buf));
		ensure(readExact(fd1, buf, str.size()));
		ensure(memcmp(buf, str.c_str(), str.size()) == 0);
	}
	
	TEST_METHOD(6) {
		// If EventedClientTest.write() cannot write out all data immediately,
		// and the remaining data is larger than the outbox limit, then it ensures
		// that all pending data is written to the socket eventually.
		// readWatcher will not be active in the mean time.
		string str(1024 * 256, '\1');
		StaticString data = str;
		EventedClient client(eventLoop, fd2);
		client.setOutboxLimit(1);
		client.userData = this;
		client.onReadable = EventedClientTest::setIntToOne;
		
		client.notifyReads(true);
		client.write(&data, 1);
		ensure("(1)", client.pendingWrites() > 0);
		ensure("(2)", !client.readWatcherActive());
		client.notifyReads(true);
		ensure("(3)", !client.readWatcherActive());
		
		startEventLoop();
		EVENT_LOOP_GUARD;
		
		char buf[str.size()];
		memset(buf, 0, sizeof(buf));
		ensure(readExact(fd1, buf, str.size()));
		ensure(memcmp(buf, str.c_str(), str.size()) == 0);
		
		// readWatcher will become active again after all pending data has been sent.
		stopEventLoop();
		ensure(client.readWatcherActive());
	}
	
	TEST_METHOD(7) {
		// disconnect() closes the connection and emits a disconnection event.
		EventedClient client(eventLoop, fd2);
		client.userData = this;
		client.onDisconnect = EventedClientTest::setIntToTwo;
		client.disconnect();
		ensure(!client.ioAllowed());
		
		char buf;
		ensure_equals(read(fd1, &buf, 1), (ssize_t) 0);
		ensure_equals(integer, 2);
	}
	
	TEST_METHOD(8) {
		// If there's pending outgoing data then disconnect(false) does
		// not disconnect immediately and does not discard pending
		// outgoing data. Pending outgoing data will eventually be sent out.
		// In the mean time readWatcher will be disabled.
		// Disconnection event will be emitted after all pending data
		// is sent out.
		string str(1024 * 256, '\1');
		StaticString data = str;
		EventedClient client(eventLoop, fd2);
		client.setOutboxLimit(str.size() + 1);
		client.userData = this;
		client.onReadable = EventedClientTest::setIntToOne;
		client.notifyReads(true);
		client.onDisconnect = EventedClientTest::setIntToTwo;
		client.write(&data, 1);
		client.disconnect();
		
		ensure(!client.ioAllowed());
		ensure(!client.readWatcherActive());
		
		char buf[str.size()];
		ensure(!client.ioAllowed());
		ensure_equals(read(fd1, buf, 1), (ssize_t) 1);
		ensure_equals(buf[0], '\1');
		
		startEventLoop();
		EVENT_LOOP_GUARD;
		
		// No disconnection event yet.
		SHOULD_NEVER_HAPPEN(100,
			result = integer == 2;
		);
		
		memset(buf, 0, sizeof(buf));
		ensure(readExact(fd1, buf, str.size() - 1));
		ensure(memcmp(buf, str.c_str() + 1, str.size() - 1) == 0);
		
		ensure_equals(read(fd1, buf, 1), (ssize_t) 0);
		
		stopEventLoop();
		// Disconnection event should now have fired.
		ensure_equals(integer, 2);
	}
	
	TEST_METHOD(9) {
		// If there's pending outgoing data then disconnect(true)
		// closes the connection immediately without sending out
		// pending data. Disconnection event is emitted immediately.
		string str(1024 * 256, '\1');
		StaticString data = str;
		EventedClient client(eventLoop, fd2);
		client.userData = this;
		client.onDisconnect = EventedClientTest::setIntToTwo;
		client.setOutboxLimit(str.size() + 1);
		client.write(&data, 1);
		client.disconnect(true);
		
		ensure(!client.ioAllowed());
		ensure(!client.readWatcherActive());
		ensure(client.pendingWrites() > 0);
		ensure_equals(integer, 2);
		
		string result = readAll(fd1);
		ensure_equals(result.size(), str.size() - client.pendingWrites());
	}
	
	TEST_METHOD(10) {
		// EventedClient.write() doesn't do anything if the client is
		// already disconnected.
		string str(1024 * 256, '\1');
		StaticString data = str;
		EventedClient client(eventLoop, fd2);
		client.disconnect();
		client.write(&data, 1);
		
		char buf;
		ensure_equals(read(fd1, &buf, 1), (ssize_t) 0);
	}
	
	TEST_METHOD(11) {
		// EventedClient.write() doesn't do anything if the client
		// is being disconnected after pending data is sent out.
		string str1(1024 * 256, '\1');
		string str2(1024 * 256, '\2');
		StaticString data;
		size_t pending;
		
		EventedClient client(eventLoop, fd2);
		client.setOutboxLimit(1);
		data = str1;
		client.write(&data, 1);
		pending = client.pendingWrites();
		client.disconnect();
		data = str2;
		client.write(&data, 1);
		ensure_equals(client.pendingWrites(), pending);
		
		startEventLoop();
		EVENT_LOOP_GUARD;
		
		string result = readAll(fd1);
		ensure_equals(result, str1);
	}
	
	TEST_METHOD(12) {
		// EventedClient.detach() returns the original file descriptor
		// and makes I/O to the file descriptor via its own object
		// impossible.
		EventedClient client(eventLoop, fd2);
		FileDescriptor fd3 = client.detach();
		ensure_equals(fd3, fd2);
		ensure_equals(client.fd, -1);
		ensure(!client.ioAllowed());
		ensure(!client.readWatcherActive());
		
		StaticString data = "hi";
		client.write(&data, 1);
		
		char buf[2];
		ssize_t ret;
		int e;
		setNonBlocking(fd1);
		ret = read(fd1, buf, 2);
		e = errno;
		ensure_equals(ret, -1);
		ensure_equals(e, EAGAIN);
	}
	
	TEST_METHOD(13) {
		// EventedClient.detach() emits a detach event.
		EventedClient client(eventLoop, fd2);
		client.userData = this;
		client.onDetach = &EventedClientTest::setIntToTwo;
		client.detach();
		ensure_equals(integer, 2);
	}
	
	TEST_METHOD(14) {
		// Subsequent calls to EventedClient.detach() return -1
		// and no longer emit detach events.
		EventedClient client(eventLoop, fd2);
		client.detach();
		client.userData = this;
		client.onDetach = &EventedClientTest::setIntToTwo;
		ensure_equals(client.detach(), -1);
		ensure_equals(integer, 0);
	}
	
	TEST_METHOD(15) {
		// EventedClient.write() emits a pending data flushed event
		// if no outgoing data needs to be scheduled for later.
		EventedClient client(eventLoop, fd2);
		client.userData = this;
		client.onPendingDataFlushed = &EventedClientTest::setIntToTwo;
		client.write("hi");
		ensure_equals(client.pendingWrites(), (size_t) 0);
		ensure_equals(integer, 2);
	}
	
	TEST_METHOD(16) {
		// EventedClient emits a pending data flushed event
		// after all pending outgoing data is flushed.
		string str(1024 * 256, '\1');
		EventedClient client(eventLoop, fd2);
		client.userData = this;
		client.onPendingDataFlushed = &EventedClientTest::setIntToTwo;
		client.write(str);
		ensure(client.pendingWrites() > 0);
		ensure_equals(integer, 0);
		
		startEventLoop();
		EVENT_LOOP_GUARD;
		
		char buf[str.size()];
		readExact(fd1, buf, str.size());
		EVENTUALLY(2,
			result = integer == 2;
		);
	}
	
	TEST_METHOD(17) {
		// EventedClient.write() ensures that the given data is written
		// after what's already in the outbox.
		EventedClient client(eventLoop, fd2);
		string header(1024 * 4, 'x');
		string body(1024 * 128, 'y');
		char buf[header.size() + body.size() + 1024];
		
		client.write(header);
		client.write(body);
		ensure(client.pendingWrites() > 0);
		
		ensure_equals(readExact(fd1, buf, header.size()), (unsigned int) header.size());
		ensure_equals(StaticString(buf, header.size()), header);
		
		client.write("hello world");
		
		startEventLoop();
		EVENT_LOOP_GUARD;
		
		unsigned int len = body.size() + strlen("hello world");
		ensure_equals(readExact(fd1, buf, len), len);
		ensure_equals(StaticString(buf, len), body + "hello world");
	}
	
	TEST_METHOD(18) {
		// If writeErrorAction is DISCONNECT_FULL then
		// EventedClient.write(), upon encountering a write error,
		// will forcefully disconnect the connection.
		EventedClient client(eventLoop, fd2);
		client.userData = this;
		client.writeErrorAction = EventedClient::DISCONNECT_FULL;
		client.onSystemError = saveSystemError;
		fd1.close();
		client.write("hello");
		ensure_equals(lastErrorCode, EPIPE);
		ensure_equals(client.fd, -1);
	}
	
	TEST_METHOD(19) {
		// If writeErrorAction is DISCONNECT_FULL then
		// the background writer disconnects the connection
		// on write error.
		EventedClient client(eventLoop, fd2);
		client.userData = this;
		client.writeErrorAction = EventedClient::DISCONNECT_FULL;
		client.onSystemError = saveSystemError;
		
		string str(1024 * 128, 'x');
		client.write(str);
		ensure(client.pendingWrites() > 0);
		
		fd1.close();
		client.onDisconnect = exitEventLoop;
		startEventLoop();
		waitUntilEventLoopExits();
		
		ensure_equals(lastErrorCode, EPIPE);
		ensure_equals(client.fd, -1);
	}
	
	TEST_METHOD(20) {
		// If writeErrorAction is DISCONNECT_WRITE then
		// EventedClient.write(), upon encountering a write error,
		// will forcefully disconnect the writer side of the
		// connection but continue allowing reading.
		EventedClient client(eventLoop, fd2);
		client.userData = this;
		client.writeErrorAction = EventedClient::DISCONNECT_WRITE;
		client.onSystemError = saveSystemError;
		client.onReadable = readAndExitOnEof;
		client.notifyReads(true);
		
		writeExact(fd1, "world", 5);
		fd1.close();
		client.write("hello");
		
		startEventLoop();
		waitUntilEventLoopExits();
		
		ensure(client.fd != -1);
		ensure_equals(data, "world");
	}
	
	TEST_METHOD(21) {
		// If writeErrorAction is DISCONNECT_WRITE then
		// the background writer, upon encountering a write error,
		// will forcefully disconnect the writer side of the
		// connection but continue allowing reading.
		EventedClient client(eventLoop, fd2);
		client.userData = this;
		client.writeErrorAction = EventedClient::DISCONNECT_WRITE;
		client.onSystemError = saveSystemError;
		client.onReadable = readAndExitOnEof;
		client.notifyReads(true);
		
		string str(1024 * 128, 'x');
		client.write(str);
		ensure(client.pendingWrites() > 0);
		
		writeExact(fd1, "world", 5);
		fd1.close();
		
		startEventLoop();
		waitUntilEventLoopExits();
		
		ensure(client.fd != -1);
		ensure_equals(data, "world");
	}
}
