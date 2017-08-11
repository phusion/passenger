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
		ensure_equals(error.getMessage(), "'foo' is required");
		ensure(!schema.validateValue("bar", Json::nullValue, error));
		ensure_equals(error.getMessage(), "'bar' is required");
	}

	TEST_METHOD(6) {
		set_test_name("Validating required keys with the right value types");
		Json::Value doc;

		schema.add("string", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.add("integer", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		schema.add("integer_unsigned", ConfigKit::UINT_TYPE, ConfigKit::REQUIRED);
		schema.add("float", ConfigKit::FLOAT_TYPE, ConfigKit::REQUIRED);
		schema.add("boolean", ConfigKit::BOOL_TYPE, ConfigKit::REQUIRED);
		schema.add("array", ConfigKit::ARRAY_TYPE, ConfigKit::REQUIRED);
		schema.add("string_array", ConfigKit::STRING_ARRAY_TYPE, ConfigKit::REQUIRED);
		schema.add("object", ConfigKit::OBJECT_TYPE, ConfigKit::REQUIRED);
		schema.add("any", ConfigKit::ANY_TYPE, ConfigKit::REQUIRED);
		schema.finalize();

		ensure(schema.validateValue("string", "string", error));
		ensure(schema.validateValue("string", 123, error));
		ensure(schema.validateValue("string", 123.45, error));
		ensure(schema.validateValue("string", true, error));
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
		ensure(schema.validateValue("any", "string", error));
		ensure(schema.validateValue("any", 123, error));
		ensure(schema.validateValue("any", 123.45, error));
		ensure(schema.validateValue("any", -123, error));
		ensure(schema.validateValue("any", true, error));
		ensure(schema.validateValue("any", Json::arrayValue, error));
		ensure(schema.validateValue("any", Json::objectValue, error));

		doc = Json::Value(Json::arrayValue);
		doc.append("string");
		doc.append(123);
		ensure(schema.validateValue("array", doc, error));

		doc = Json::Value(Json::arrayValue);
		doc.append("string");
		doc.append("string");
		ensure(schema.validateValue("string_array", doc, error));

		doc = Json::Value(Json::objectValue);
		doc["string"] = "string";
		doc["int"] = 123;
		ensure(schema.validateValue("object", doc, error));
	}

	TEST_METHOD(7) {
		set_test_name("Validating required keys with the wrong value types");
		Json::Value doc;

		schema.add("integer", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		schema.add("integer_unsigned", ConfigKit::UINT_TYPE, ConfigKit::REQUIRED);
		schema.add("float", ConfigKit::FLOAT_TYPE, ConfigKit::REQUIRED);
		schema.add("boolean", ConfigKit::BOOL_TYPE, ConfigKit::REQUIRED);
		schema.add("array", ConfigKit::ARRAY_TYPE, ConfigKit::REQUIRED);
		schema.add("string_array", ConfigKit::STRING_ARRAY_TYPE, ConfigKit::REQUIRED);
		schema.add("object", ConfigKit::OBJECT_TYPE, ConfigKit::REQUIRED);
		schema.finalize();

		ensure(!schema.validateValue("integer", "string", error));
		ensure_equals(error.getMessage(), "'integer' must be an integer");

		ensure(!schema.validateValue("integer_unsigned", -123, error));
		ensure_equals(error.getMessage(), "'integer_unsigned' must be greater than 0");

		ensure(!schema.validateValue("float", "string", error));
		ensure_equals(error.getMessage(), "'float' must be a number");

		ensure(!schema.validateValue("boolean", "string", error));
		ensure_equals(error.getMessage(), "'boolean' must be a boolean");

		ensure(!schema.validateValue("array", "string", error));
		ensure_equals(error.getMessage(), "'array' must be an array");

		ensure(!schema.validateValue("string_array", "string", error));
		ensure_equals(error.getMessage(), "'string_array' must be an array");

		doc = Json::Value(Json::arrayValue);
		doc.append(123);
		doc.append("string");
		ensure(!schema.validateValue("string_array", doc, error));
		ensure_equals(error.getMessage(), "'string_array' may only contain strings");

		ensure(!schema.validateValue("object", "string", error));
		ensure_equals(error.getMessage(), "'object' must be a JSON object");
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
		Json::Value doc;

		schema.add("string", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL);
		schema.add("integer", ConfigKit::INT_TYPE, ConfigKit::OPTIONAL);
		schema.add("integer_unsigned", ConfigKit::UINT_TYPE, ConfigKit::OPTIONAL);
		schema.add("float", ConfigKit::FLOAT_TYPE, ConfigKit::OPTIONAL);
		schema.add("boolean", ConfigKit::BOOL_TYPE, ConfigKit::OPTIONAL);
		schema.add("array", ConfigKit::ARRAY_TYPE, ConfigKit::OPTIONAL);
		schema.add("string_array", ConfigKit::STRING_ARRAY_TYPE, ConfigKit::OPTIONAL);
		schema.add("object", ConfigKit::OBJECT_TYPE, ConfigKit::OPTIONAL);
		schema.add("any", ConfigKit::ANY_TYPE, ConfigKit::OPTIONAL);
		schema.finalize();

		ensure(schema.validateValue("string", "string", error));
		ensure(schema.validateValue("string", 123, error));
		ensure(schema.validateValue("string", 123.45, error));
		ensure(schema.validateValue("string", true, error));
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
		ensure(schema.validateValue("any", "string", error));
		ensure(schema.validateValue("any", 123, error));
		ensure(schema.validateValue("any", 123.45, error));
		ensure(schema.validateValue("any", -123, error));
		ensure(schema.validateValue("any", true, error));
		ensure(schema.validateValue("any", Json::arrayValue, error));
		ensure(schema.validateValue("any", Json::objectValue, error));

		doc = Json::Value(Json::arrayValue);
		doc.append("string");
		doc.append(123);
		ensure(schema.validateValue("array", doc, error));

		doc = Json::Value(Json::arrayValue);
		doc.append("string");
		doc.append("string");
		ensure(schema.validateValue("string_array", doc, error));

		doc = Json::Value(Json::objectValue);
		doc["string"] = "string";
		doc["int"] = 123;
		ensure(schema.validateValue("object", doc, error));
	}

	TEST_METHOD(12) {
		set_test_name("Validating optional keys with the wrong value types");
		Json::Value doc;

		schema.add("integer", ConfigKit::INT_TYPE, ConfigKit::OPTIONAL);
		schema.add("integer_unsigned", ConfigKit::UINT_TYPE, ConfigKit::OPTIONAL);
		schema.add("float", ConfigKit::FLOAT_TYPE, ConfigKit::OPTIONAL);
		schema.add("boolean", ConfigKit::BOOL_TYPE, ConfigKit::OPTIONAL);
		schema.add("array", ConfigKit::ARRAY_TYPE, ConfigKit::OPTIONAL);
		schema.add("string_array", ConfigKit::STRING_ARRAY_TYPE, ConfigKit::OPTIONAL);
		schema.add("object", ConfigKit::OBJECT_TYPE, ConfigKit::OPTIONAL);
		schema.finalize();

		ensure(!schema.validateValue("integer", "string", error));
		ensure_equals(error.getMessage(), "'integer' must be an integer");

		ensure(!schema.validateValue("integer_unsigned", -123, error));
		ensure_equals(error.getMessage(), "'integer_unsigned' must be greater than 0");

		ensure(!schema.validateValue("float", "string", error));
		ensure_equals(error.getMessage(), "'float' must be a number");

		ensure(!schema.validateValue("boolean", "string", error));
		ensure_equals(error.getMessage(), "'boolean' must be a boolean");

		ensure(!schema.validateValue("string_array", "string", error));
		ensure_equals(error.getMessage(), "'string_array' must be an array");

		doc = Json::Value(Json::arrayValue);
		doc.append(123);
		doc.append("string");
		ensure(!schema.validateValue("string_array", doc, error));
		ensure_equals(error.getMessage(), "'string_array' may only contain strings");

		ensure(!schema.validateValue("object", "string", error));
		ensure_equals(error.getMessage(), "'object' must be a JSON object");
	}


	/*********** Test inspect() ***********/

	TEST_METHOD(20) {
		set_test_name("It marks secret fields as such");

		schema.add("secret", ConfigKit::INT_TYPE, ConfigKit::REQUIRED | ConfigKit::SECRET);
		schema.finalize();

		Json::Value doc = schema.inspect();
		ensure(doc["secret"]["secret"].asBool());
	}
}
