#include "TestSupport.h"
#include <UstRouter/Transaction.h>

using namespace Passenger;
using namespace Passenger::UstRouter;
using namespace std;

namespace tut {
	struct UstRouter_TransactionTest {
		UstRouter_TransactionTest() {
		}
	};

	DEFINE_TEST_GROUP(UstRouter_TransactionTest);

	TEST_METHOD(1) {
		set_test_name("Constructor");
		Transaction t("txnId", "groupName", "nodeName", "category",
			"unionStationKey", 1234, "filters");
		ensure_equals("(1)", t.getTxnId(), "txnId");
		ensure_equals("(2)", t.getGroupName(), "groupName");
		ensure_equals("(3)", t.getNodeName(), "nodeName");
		ensure_equals("(4)", t.getCategory(), "category");
		ensure_equals("(5)", t.getUnionStationKey(), "unionStationKey");
		ensure_equals("(6)", t.getFilters(), "filters");
		ensure_equals("(7)", t.getBody(), "");
	}

	TEST_METHOD(2) {
		set_test_name("Appending body data");
		Transaction t("txnId", "groupName", "nodeName", "category",
			"unionStationKey", 1234, "filters");

		t.append("timestamp1", "body1");
		t.append("timestamp2", "body2");
		ensure_equals(t.getBody(),
			"txnId timestamp1 0 body1\n"
			"txnId timestamp2 1 body2\n");
	}

	TEST_METHOD(3) {
		set_test_name("Move constructor");
		Transaction t("txnId", "groupName", "nodeName", "category",
			"unionStationKey", 1234, "filters");
		t.append("timestamp1", "body1");
		t.append("timestamp2", "body2");

		Transaction t2(boost::move(t));

		ensure_equals("(1)", t.getTxnId(), "");
		ensure_equals("(2)", t.getGroupName(), "");
		ensure_equals("(3)", t.getNodeName(), "");
		ensure_equals("(4)", t.getCategory(), "");
		ensure_equals("(5)", t.getUnionStationKey(), "");
		ensure_equals("(6)", t.getFilters(), "");
		ensure_equals("(7)", t.getBody(), "");

		ensure_equals("(11)", t2.getTxnId(), "txnId");
		ensure_equals("(12)", t2.getGroupName(), "groupName");
		ensure_equals("(13)", t2.getNodeName(), "nodeName");
		ensure_equals("(14)", t2.getCategory(), "category");
		ensure_equals("(15)", t2.getUnionStationKey(), "unionStationKey");
		ensure_equals("(16)", t2.getFilters(), "filters");
		ensure_equals("(17)", t2.getBody(),
			"txnId timestamp1 0 body1\n"
			"txnId timestamp2 1 body2\n");
	}

	TEST_METHOD(4) {
		set_test_name("Move assignment");
		Transaction t("txnId", "groupName", "nodeName", "category",
			"unionStationKey", 1234, "filters");
		t.append("timestamp1", "body1");
		t.append("timestamp2", "body2");

		Transaction t2("txnId2", "groupName2", "nodeName2", "category2",
			"unionStationKey2", 4321, "filters2");
		t2 = boost::move(t);

		ensure_equals("(1)", t.getTxnId(), "");
		ensure_equals("(2)", t.getGroupName(), "");
		ensure_equals("(3)", t.getNodeName(), "");
		ensure_equals("(4)", t.getCategory(), "");
		ensure_equals("(5)", t.getUnionStationKey(), "");
		ensure_equals("(6)", t.getFilters(), "");
		ensure_equals("(7)", t.getBody(), "");

		ensure_equals("(11)", t2.getTxnId(), "txnId");
		ensure_equals("(12)", t2.getGroupName(), "groupName");
		ensure_equals("(13)", t2.getNodeName(), "nodeName");
		ensure_equals("(14)", t2.getCategory(), "category");
		ensure_equals("(15)", t2.getUnionStationKey(), "unionStationKey");
		ensure_equals("(16)", t2.getFilters(), "filters");
		ensure_equals("(17)", t2.getBody(),
			"txnId timestamp1 0 body1\n"
			"txnId timestamp2 1 body2\n");
	}

	TEST_METHOD(5) {
		set_test_name("Expanding the storage area");
		Transaction t("txnId", "groupName", "nodeName", "category",
			"unionStationKey", 1234, "filters", 128);
		string body1(1024, 'x');
		string body2(1024, 'y');

		t.append("timestamp1", body1);
		t.append("timestamp2", body2);

		ensure_equals("(1)", t.getTxnId(), "txnId");
		ensure_equals("(2)", t.getGroupName(), "groupName");
		ensure_equals("(3)", t.getNodeName(), "nodeName");
		ensure_equals("(4)", t.getCategory(), "category");
		ensure_equals("(5)", t.getUnionStationKey(), "unionStationKey");
		ensure_equals("(6)", t.getFilters(), "filters");
		ensure_equals("(7)", t.getBody(),
			"txnId timestamp1 0 " + body1 + "\n"
			"txnId timestamp2 1 " + body2 + "\n");
	}
}
