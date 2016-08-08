#include "TestSupport.h"
#include <modp_b64.h>

using namespace Passenger;
using namespace modp;
using namespace std;

namespace tut {
	struct Base64DecodingTest {
		Base64DecodingTest() {
		}

		string decode(const char* base64string){
			return b64_decode(base64string);
		}

	};

	DEFINE_TEST_GROUP(Base64DecodingTest);

	/***** Valid base64 *****/
	TEST_METHOD(1) {
		ensure_equals(decode(""),"");
	}
	TEST_METHOD(2) {
		ensure_equals(decode("YQ=="),"a");
	}
	TEST_METHOD(3) {
		ensure_equals(decode("YWI="),"ab");
	}
	TEST_METHOD(4) {
		ensure_equals(decode("YWJj"),"abc");
	}
	TEST_METHOD(5) {
		ensure_equals(decode("VGhpcyBpcyBhIHRlc3Qgb2YgYSBsb25nZXIgc3RyaW5nLg=="),"This is a test of a longer string.");
	}
}
