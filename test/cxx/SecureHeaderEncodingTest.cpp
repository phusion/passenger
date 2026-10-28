#include <TestSupport.h>
#include <SecureHeaderEncoding.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct SecureHeaderEncodingTest: public TestBase {
		string encode(const string &value) {
			string result(psg_secure_header_value_encoded_len(value.data(), value.size()), '\0');
			char *end = psg_secure_header_value_encode(&result[0], value.data(), value.size());
			ensure_equals("Encoded length matches", (size_t) (end - result.data()), result.size());
			return result;
		}

		bool decode(const string &value, string &result) {
			result.assign(value.size(), '\0');
			size_t size = psg_secure_header_value_decode(&result[0], value.data(), value.size());
			if (size == (size_t) -1) {
				return false;
			}
			result.resize(size);
			return true;
		}
	};

	DEFINE_TEST_GROUP(SecureHeaderEncodingTest);

	TEST_METHOD(1) {
		set_test_name("Encoding leaves ordinary values unchanged");
		ensure_equals(encode(""), "");
		ensure_equals(encode("/var/www/app/public"), "/var/www/app/public");
		ensure_equals(encode("bundle exec rails s -p $PORT"), "bundle exec rails s -p $PORT");
	}

	TEST_METHOD(2) {
		set_test_name("Encoding escapes '%' and control characters, so the result never contains CR or LF");
		ensure_equals(encode("100%"), "100%25");
		ensure_equals(encode("a\r\n!~PASSENGER_APP_TYPE: x"), "a%0D%0A!~PASSENGER_APP_TYPE: x");
		ensure_equals(encode(string("\x00\x1F\x7F", 3)), "%00%1F%7F");
		ensure_equals(encode("\t"), "%09");
	}

	TEST_METHOD(3) {
		set_test_name("Decoding reverses encoding");
		string value = string("x%\r\n\t\x7F ~!", 9) + string(1, '\0') + "\xC3\xA9";
		string decoded;
		ensure(decode(encode(value), decoded));
		ensure_equals(decoded, value);
	}

	TEST_METHOD(4) {
		set_test_name("Decoding accepts lowercase hex digits");
		string decoded;
		ensure(decode("%0d%0a", decoded));
		ensure_equals(decoded, "\r\n");
	}

	TEST_METHOD(5) {
		set_test_name("Decoding rejects '%' that is not followed by two hex digits");
		string decoded;
		ensure("(1)", !decode("%", decoded));
		ensure("(2)", !decode("100%", decoded));
		ensure("(3)", !decode("%0", decoded));
		ensure("(4)", !decode("%0G", decoded));
		ensure("(5)", !decode("%G0", decoded));
		ensure("(6)", !decode("% 0A", decoded));
	}
}
