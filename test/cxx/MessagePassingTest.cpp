#include <TestSupport.h>
#include <boost/bind.hpp>
#include <Utils/MessagePassing.h>
#include <Utils/Timer.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct MessagePassingTest {
		MessageBoxPtr box;

		MessagePassingTest() {
			box = boost::make_shared<MessageBox>();
		}

		void sendMessagesLater() {
			syscalls::usleep(20000);
			box->send("hi");
			syscalls::usleep(20000);
			box->send("ho");
		}
	};
	
	DEFINE_TEST_GROUP(MessagePassingTest);

	TEST_METHOD(1) {
		// Sending and receiving 1 message.
		box->send("hi");
		ensure_equals(box->size(), 1u);
		ensure_equals(box->recv("hi")->name, "hi");
		ensure_equals(box->size(), 0u);
	}

	TEST_METHOD(2) {
		// Sending and receiving multiple messages out of order.
		box->send("ho");
		box->send("hi");
		box->send("ha");
		ensure_equals(box->size(), 3u);
		ensure_equals(box->recv("hi")->name, "hi");
		ensure_equals(box->size(), 2u);
		ensure_equals(box->recv("ho")->name, "ho");
		ensure_equals(box->size(), 1u);
		ensure_equals(box->recv("ha")->name, "ha");
		ensure_equals(box->size(), 0u);
	}

	TEST_METHOD(3) {
		// Receive with zero timeout.
		unsigned long long timeout = 0;
		Timer timer;
		ensure_equals(box->recv("hi", &timeout), MessagePtr());
		ensure(timer.elapsed() < 10);
		ensure_equals(timeout, 0ull);
	}

	TEST_METHOD(4) {
		// Receive with non-zero timeout.
		unsigned long long timeout = 20000;
		Timer timer;
		ensure_equals(box->recv("hi", &timeout), MessagePtr());
		ensure("(1)", timer.elapsed() >= 19);
		ensure("(2)", timer.elapsed() < 95);
		ensure("(3)", timeout >= 19000ull);
	}

	TEST_METHOD(5) {
		// Test waiting with timeout.
		TempThread thr(boost::bind(&MessagePassingTest::sendMessagesLater, this));
		unsigned long long timeout = 200000;
		Timer timer;
		ensure_equals(box->recv("ho", &timeout)->name, "ho");
		ensure(timer.elapsed() >= 39);
		ensure(timer.elapsed() < 95);
		ensure_equals(box->size(), 1u);
		ensure_equals(box->recv("hi")->name, "hi");
		ensure_equals(box->size(), 0u);
		ensure(timeout >= 140000);
	}
}
