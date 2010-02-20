#include "TestSupport.h"
#include "Utils/Base64.h"

using namespace Passenger;
using namespace std;

namespace tut {
	struct Base64Test {
	};
	
	DEFINE_TEST_GROUP(Base64Test);

	TEST_METHOD(1) {
		// Test encoding.
		ensure_equals(Base64::encode(""), "");
		ensure_equals(Base64::encode("a"), "YQ==");
		ensure_equals(Base64::encode("b"), "Yg==");
		ensure_equals(Base64::encode("ab"), "YWI=");
		ensure_equals(Base64::encode("abc"), "YWJj");
		ensure_equals(Base64::encode("abcd"), "YWJjZA==");
		ensure_equals(Base64::encode("\1\2\3\4\5\6\7\255"), "AQIDBAUGB60=");
		ensure_equals(Base64::encode("The gamma-ray burst from April 23, a "
			"powerful explosion from a dying star, was detected by the "
			"Swift satellite using on-board gamma-ray and X-ray instruments."),
			"VGhlIGdhbW1hLXJheSBidXJzdCBmcm9tIEFwcmlsIDIzLCBhIHBvd2VyZnVs"
			"IGV4cGxvc2lvbiBmcm9tIGEgZHlpbmcgc3Rhciwgd2FzIGRldGVjdGVkIGJ5"
			"IHRoZSBTd2lmdCBzYXRlbGxpdGUgdXNpbmcgb24tYm9hcmQgZ2FtbWEtcmF5"
			"IGFuZCBYLXJheSBpbnN0cnVtZW50cy4=");
		
		ensure_equals(Base64::encodeForUrl("\003\340\177X"), "A-B_WA");
	}
	
	TEST_METHOD(2) {
		// Test decoding.
		ensure_equals(Base64::decode(""), "");
		ensure_equals(Base64::decode("YQ=="), "a");
		ensure_equals(Base64::decode("Yg=="), "b");
		ensure_equals(Base64::decode("YWI="), "ab");
		ensure_equals(Base64::decode("YWJj"), "abc");
		ensure_equals(Base64::decode("YWJjZA=="), "abcd");
		ensure_equals(Base64::decode("AQIDBAUGB60="), "\1\2\3\4\5\6\7\255");
		ensure_equals(Base64::decode("VGhlIGdhbW1hLXJheSBidXJzdCBmcm9tIEFwcmlsIDIzLCBhIHBvd2VyZnVs"
			"IGV4cGxvc2lvbiBmcm9tIGEgZHlpbmcgc3Rhciwgd2FzIGRldGVjdGVkIGJ5"
			"IHRoZSBTd2lmdCBzYXRlbGxpdGUgdXNpbmcgb24tYm9hcmQgZ2FtbWEtcmF5"
			"IGFuZCBYLXJheSBpbnN0cnVtZW50cy4="),
			"The gamma-ray burst from April 23, a "
			"powerful explosion from a dying star, was detected by the "
			"Swift satellite using on-board gamma-ray and X-ray instruments.");
	}
}
