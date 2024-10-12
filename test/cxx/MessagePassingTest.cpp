#include <TestSupport.h>
#include <boost/bind/bind.hpp>
#include <Utils/MessagePassing.h>
#include <Utils/Timer.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct MessagePassingTest: public TestBase {
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
		unsigned long long timeoutUSec = 0;
		Timer<> timer;
		ensure_equals(box->recv("hi", &timeoutUSec), MessagePtr());
		ensure(timer.elapsed() < 10);
		ensure_equals(timeoutUSec, 0ull);
	}

	TEST_METHOD(4) {
		// Receive with non-zero timeout.
		unsigned long long timeoutUSec = 20000;
		Timer<> timer;
		ensure_equals(box->recv("hi", &timeoutUSec), MessagePtr());
		ensure("(1)", timer.elapsed() >= 19);
		ensure("(2)", timer.elapsed() <= 200);
		ensure("(3)", timeoutUSec <= 2000ull);
	}

	TEST_METHOD(5) {
		// Test waiting with timeout.
		TempThread thr(boost::bind(&MessagePassingTest::sendMessagesLater, this));
		unsigned long long timeoutUSec = 700000;
		Timer<> timer;
		ensure_equals("(1)", box->recv("ho", &timeoutUSec)->name, "ho");
		ensure("(2)", timer.elapsed() >= 39);
		ensure("(3)", timer.elapsed() <= 500);
		ensure_equals("(4)", box->size(), 1u);
		ensure_equals("(5)", box->recv("hi")->name, "hi");
		ensure_equals("(6)", box->size(), 0u);
		ensure("(7)", timeoutUSec >= 100000);
	}
}
