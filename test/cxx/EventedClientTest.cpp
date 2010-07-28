#include "TestSupport.h"
#include "EventedClient.h"
#include "Utils/ScopeGuard.h"
#include "Utils/IOUtils.h"

#include <oxt/thread.hpp>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

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
		AtomicInt integer;
		
		EventedClientTest()
			: exitWatcher(eventLoop)
		{
			int fds[2];
			
			socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
			fd1 = fds[0];
			fd2 = fds[1];
			setNonBlocking(fd2);
			
			exitWatcher.set<EventedClientTest, &EventedClientTest::unloop>(this);
			exitWatcher.start();
			thr = NULL;
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
		
		void setIntToOne(ev::io &watcher, int revents) {
			integer = 1;
		}
		
		void setIntToTwo() {
			integer = 2;
		}
	};

	DEFINE_TEST_GROUP(EventedClientTest);
	
	#define EVENT_LOOP_GUARD ScopeGuard g(boost::bind(&EventedClientTest::stopEventLoop, this))

	TEST_METHOD(1) {
		// It doesn't watch read events by default.
		EventedClient client(eventLoop, fd2);
		write(fd1, "x", 1);
		client.readWatcher.set<EventedClientTest, &EventedClientTest::setIntToOne>(this);
		
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
		client.readWatcher.set<EventedClientTest, &EventedClientTest::setIntToOne>(this);
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
		ensure(MessageChannel(fd1).readRaw(buf, strlen("hello world")));
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
		
		client.readWatcher.set<EventedClientTest, &EventedClientTest::setIntToOne>(this);
		client.notifyReads(true);
		ensure(client.readWatcher.is_active());
		
		EVENT_LOOP_GUARD;
		startEventLoop();
		
		char buf[str.size()];
		memset(buf, 0, sizeof(buf));
		ensure(MessageChannel(fd1).readRaw(buf, str.size()));
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
		client.readWatcher.set<EventedClientTest, &EventedClientTest::setIntToOne>(this);
		
		client.notifyReads(true);
		client.write(&data, 1);
		ensure("(1)", client.pendingWrites() > 0);
		ensure("(2)", !client.readWatcher.is_active());
		client.notifyReads(true);
		ensure("(3)", !client.readWatcher.is_active());
		
		startEventLoop();
		EVENT_LOOP_GUARD;
		
		char buf[str.size()];
		memset(buf, 0, sizeof(buf));
		ensure(MessageChannel(fd1).readRaw(buf, str.size()));
		ensure(memcmp(buf, str.c_str(), str.size()) == 0);
		
		// readWatcher will become active again after all pending data has been sent.
		stopEventLoop();
		ensure(client.readWatcher.is_active());
	}
	
	TEST_METHOD(7) {
		// disconnect() closes the connection and emits a disconnection event.
		EventedClient client(eventLoop, fd2);
		client.onDisconnect = boost::bind(&EventedClientTest::setIntToTwo, this);
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
		client.readWatcher.set<EventedClientTest, &EventedClientTest::setIntToOne>(this);
		client.notifyReads(true);
		client.onDisconnect = boost::bind(&EventedClientTest::setIntToTwo, this);
		client.write(&data, 1);
		client.disconnect();
		
		ensure(!client.ioAllowed());
		ensure(!client.readWatcher.is_active());
		
		char buf[str.size()];
		ensure(!client.ioAllowed());
		ensure_equals(read(fd1, buf, 1), (ssize_t) 1);
		ensure_equals(buf[0], '\1');
		
		EVENT_LOOP_GUARD;
		startEventLoop();
		
		// No disconnection event yet.
		SHOULD_NEVER_HAPPEN(100,
			result = integer == 2;
		);
		
		memset(buf, 0, sizeof(buf));
		ensure(MessageChannel(fd1).readRaw(buf, str.size() - 1));
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
		client.onDisconnect = boost::bind(&EventedClientTest::setIntToTwo, this);
		client.setOutboxLimit(str.size() + 1);
		client.write(&data, 1);
		client.disconnect(true);
		
		ensure(!client.ioAllowed());
		ensure(!client.readWatcher.is_active());
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
		
		EVENT_LOOP_GUARD;
		startEventLoop();
		
		string result = readAll(fd1);
		ensure_equals(result, str1);
	}
}
