#include <TestSupport.h>
#include <ConfigKit/Schema.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct ConfigKit_SchemaTest {
		ConfigKit::Schema schema;
		ConfigKit::Error error;
	};

	DEFINE_TEST_GROUP(ConfigKit_SchemaTest);

	/*********** Test validation ***********/

	TEST_METHOD(1) {
		set_test_name("Validating against an unregistered key fails");

		schema.finalize();
		try {
			schema.validateValue("foo", "str", error);
			fail();
		} catch (const ArgumentException &) {
			// pass
		}
	}

	TEST_METHOD(5) {
		set_test_name("Validating required keys with null values");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.add("bar", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.finalize();

		ensure(!schema.validateValue("foo", Json::nullValue, error));
		ensure(error.getMessage(), "'foo' is required");
		ensure(!schema.validateValue("foo", Json::nullValue, error));
		ensure(error.getMessage(), "'bar' is required");
	}

	TEST_METHOD(6) {
		set_test_name("Validating required keys with the right value types");

		schema.add("string", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.add("password", ConfigKit::PASSWORD_TYPE, ConfigKit::REQUIRED);
		schema.add("integer", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		schema.add("integer_unsigned", ConfigKit::UINT_TYPE, ConfigKit::REQUIRED);
		schema.add("float", ConfigKit::FLOAT_TYPE, ConfigKit::REQUIRED);
		schema.add("boolean", ConfigKit::BOOL_TYPE, ConfigKit::REQUIRED);
		schema.finalize();

		ensure(schema.validateValue("string", "string", error));
		ensure(schema.validateValue("string", 123, error));
		ensure(schema.validateValue("string", 123.45, error));
		ensure(schema.validateValue("string", true, error));
		ensure(schema.validateValue("password", "password", error));
		ensure(schema.validateValue("password", 123, error));
		ensure(schema.validateValue("password", 123.45, error));
		ensure(schema.validateValue("password", true, error));
		ensure(schema.validateValue("integer", 123, error));
		ensure(schema.validateValue("integer", 123.45, error));
		ensure(schema.validateValue("integer", true, error));
		ensure(schema.validateValue("integer", -123, error));
		ensure(schema.validateValue("integer_unsigned", 123, error));
		ensure(schema.validateValue("integer_unsigned", 123.45, error));
		ensure(schema.validateValue("integer_unsigned", true, error));
		ensure(schema.validateValue("float", 123, error));
		ensure(schema.validateValue("float", 123.45, error));
		ensure(schema.validateValue("boolean", true, error));
		ensure(schema.validateValue("boolean", 123, error));
		ensure(schema.validateValue("boolean", 123.45, error));
	}

	TEST_METHOD(7) {
		set_test_name("Validating required keys with the wrong value types");

		schema.add("integer", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		schema.add("integer_unsigned", ConfigKit::UINT_TYPE, ConfigKit::REQUIRED);
		schema.add("float", ConfigKit::FLOAT_TYPE, ConfigKit::REQUIRED);
		schema.add("boolean", ConfigKit::BOOL_TYPE, ConfigKit::REQUIRED);
		schema.finalize();

		ensure(!schema.validateValue("integer", "string", error));
		ensure(error.getMessage(), "'integer' must be an integer");

		ensure(!schema.validateValue("integer_unsigned", -123, error));
		ensure(error.getMessage(), "'integer_unsigned' must be greater than 0");

		ensure(!schema.validateValue("float", "string", error));
		ensure(error.getMessage(), "'float' must be a number");

		ensure(!schema.validateValue("boolean", "string", error));
		ensure(error.getMessage(), "'boolean' must be a boolean");
	}

	TEST_METHOD(10) {
		set_test_name("Validating optional keys with null values");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL);
		schema.add("bar", ConfigKit::INT_TYPE, ConfigKit::OPTIONAL);
		schema.finalize();

		ensure(schema.validateValue("foo", Json::nullValue, error));
		ensure(schema.validateValue("bar", Json::nullValue, error));
	}

	TEST_METHOD(11) {
		set_test_name("Validating optional keys with the right value types");

		schema.add("string", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL);
		schema.add("password", ConfigKit::PASSWORD_TYPE, ConfigKit::OPTIONAL);
		schema.add("integer", ConfigKit::INT_TYPE, ConfigKit::OPTIONAL);
		schema.add("integer_unsigned", ConfigKit::UINT_TYPE, ConfigKit::OPTIONAL);
		schema.add("float", ConfigKit::FLOAT_TYPE, ConfigKit::OPTIONAL);
		schema.add("boolean", ConfigKit::BOOL_TYPE, ConfigKit::OPTIONAL);
		schema.finalize();

		ensure(schema.validateValue("string", "string", error));
		ensure(schema.validateValue("string", 123, error));
		ensure(schema.validateValue("string", 123.45, error));
		ensure(schema.validateValue("string", true, error));
		ensure(schema.validateValue("password", "password", error));
		ensure(schema.validateValue("password", 123, error));
		ensure(schema.validateValue("password", 123.45, error));
		ensure(schema.validateValue("password", true, error));
		ensure(schema.validateValue("integer", 123, error));
		ensure(schema.validateValue("integer", 123.45, error));
		ensure(schema.validateValue("integer", true, error));
		ensure(schema.validateValue("integer", -123, error));
		ensure(schema.validateValue("integer_unsigned", 123, error));
		ensure(schema.validateValue("integer_unsigned", 123.45, error));
		ensure(schema.validateValue("integer_unsigned", true, error));
		ensure(schema.validateValue("float", 123, error));
		ensure(schema.validateValue("float", 123.45, error));
		ensure(schema.validateValue("boolean", true, error));
		ensure(schema.validateValue("boolean", 123, error));
		ensure(schema.validateValue("boolean", 123.45, error));
	}

	TEST_METHOD(12) {
		set_test_name("Validating optional keys with the wrong value types");

		schema.add("integer", ConfigKit::INT_TYPE, ConfigKit::OPTIONAL);
		schema.add("integer_unsigned", ConfigKit::UINT_TYPE, ConfigKit::OPTIONAL);
		schema.add("float", ConfigKit::FLOAT_TYPE, ConfigKit::OPTIONAL);
		schema.add("boolean", ConfigKit::BOOL_TYPE, ConfigKit::OPTIONAL);
		schema.finalize();

		ensure(!schema.validateValue("integer", "string", error));
		ensure(error.getMessage(), "'integer' must be an integer");

		ensure(!schema.validateValue("integer_unsigned", -123, error));
		ensure(error.getMessage(), "'integer_unsigned' must be greater than 0");

		ensure(!schema.validateValue("float", "string", error));
		ensure(error.getMessage(), "'float' must be a number");

		ensure(!schema.validateValue("boolean", "string", error));
		ensure(error.getMessage(), "'boolean' must be a boolean");
	}
}
