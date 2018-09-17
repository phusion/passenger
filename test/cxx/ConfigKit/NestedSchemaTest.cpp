#include <TestSupport.h>
#include <algorithm>
#include <ConfigKit/ConfigKit.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct ConfigKit_NestedSchemaTest: public TestBase {
		ConfigKit::Schema schema;
		ConfigKit::Schema nestedSchema;
		Json::Value doc;
		vector<ConfigKit::Error> errors;

		static bool errorSorter(const ConfigKit::Error &a, const ConfigKit::Error &b) {
			return a.getMessage() < b.getMessage();
		}
	};

	DEFINE_TEST_GROUP(ConfigKit_NestedSchemaTest);

	/*********** Test validation ***********/

	TEST_METHOD(1) {
		set_test_name("Array type: updating a store with a valid document yields no errors");

		nestedSchema.add("name", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		nestedSchema.add("age", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		nestedSchema.finalize();

		schema.add("people", ConfigKit::ARRAY_TYPE, nestedSchema, ConfigKit::OPTIONAL);
		schema.finalize();

		doc["people"][0]["name"] = "Joe";
		doc["people"][0]["age"] = 30;
		doc["people"][1]["name"] = "Jane";
		doc["people"][1]["age"] = 31;

		ConfigKit::Store store(schema);
		ensure(store.update(doc, errors));
	}

	TEST_METHOD(2) {
		set_test_name("Array type: updating a store with an invalid document yields errors");

		nestedSchema.add("name", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		nestedSchema.add("age", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		nestedSchema.finalize();

		schema.add("people", ConfigKit::ARRAY_TYPE, nestedSchema, ConfigKit::OPTIONAL);
		schema.finalize();

		doc["people"][0]["age"] = 30;
		doc["people"][1]["name"] = "Jane";
		doc["people"][2] = "string";
		doc["people"][3] = 123;

		ConfigKit::Store store(schema);
		ensure("There are errors", !store.update(doc, errors));
		ensure_equals("There are 3 errors", errors.size(), 3u);
		ensure_equals("1st error message",
			errors[0].getMessage(),
			"'people' element 1 is invalid: 'name' is required");
		ensure_equals("2nd error message",
			errors[1].getMessage(),
			"'people' element 2 is invalid: 'age' is required");
		ensure_equals("3rd error message",
			errors[2].getMessage(),
			"'people' may only contain JSON objects");
	}

	TEST_METHOD(3) {
		set_test_name("Object type: updating a store with a valid document yields no errors");

		nestedSchema.add("name", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		nestedSchema.add("age", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		nestedSchema.finalize();

		schema.add("people", ConfigKit::OBJECT_TYPE, nestedSchema, ConfigKit::OPTIONAL);
		schema.finalize();

		doc["people"]["first"]["name"] = "Joe";
		doc["people"]["first"]["age"] = 30;
		doc["people"]["second"]["name"] = "Jane";
		doc["people"]["second"]["age"] = 31;

		ConfigKit::Store store(schema);
		ensure(store.update(doc, errors));
	}

	TEST_METHOD(4) {
		set_test_name("Object type: updating a store with an invalid document yields errors");

		nestedSchema.add("name", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		nestedSchema.add("age", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		nestedSchema.finalize();

		schema.add("people", ConfigKit::OBJECT_TYPE, nestedSchema, ConfigKit::OPTIONAL);
		schema.finalize();

		doc["people"]["first"]["age"] = 30;
		doc["people"]["second"]["name"] = "Jane";
		doc["people"]["third"] = "string";
		doc["people"]["fourth"] = 123;

		ConfigKit::Store store(schema);
		ensure("There are errors", !store.update(doc, errors));
		std::sort(errors.begin(), errors.end(), errorSorter);
		ensure_equals("There are 3 errors", errors.size(), 3u);
		ensure_equals("1st error message",
			errors[0].getMessage(),
			"'people' key 'first' is invalid: 'name' is required");
		ensure_equals("2nd error message",
			errors[1].getMessage(),
			"'people' key 'second' is invalid: 'age' is required");
		ensure_equals("3rd error message",
			errors[2].getMessage(),
			"'people' may only contain JSON objects");
	}


	/*********** Test type casting ***********/

	TEST_METHOD(10) {
		set_test_name("Array type: typecasting works");

		nestedSchema.add("name", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		nestedSchema.add("age", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		nestedSchema.add("address", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL);
		nestedSchema.add("comments", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL, "none");
		nestedSchema.finalize();

		schema.add("people", ConfigKit::ARRAY_TYPE, nestedSchema, ConfigKit::OPTIONAL);
		schema.finalize();

		doc["people"][0]["name"] = 123;
		doc["people"][0]["age"] = 30;

		ConfigKit::Store store(schema);
		Json::Value preview = store.previewUpdate(doc, errors);
		store.update(doc, errors);
		ensure("There are no errors", errors.empty());
		Json::Value inspection = store.inspect();

		Json::Value expected;
		expected[0]["name"] = "123";
		expected[0]["age"] = 30;
		expected[0]["address"] = Json::nullValue;
		expected[0]["comments"] = Json::nullValue;
		ensure_equals("Preview user value",
			preview["people"]["user_value"], expected);
		ensure_equals("Updated user value",
			inspection["people"]["user_value"], expected);

		expected = Json::Value();
		expected[0]["name"] = "123";
		expected[0]["age"] = 30;
		expected[0]["address"] = Json::nullValue;
		expected[0]["comments"] = "none";
		ensure_equals("Preview effective value",
			preview["people"]["effective_value"], expected);
		ensure_equals("Updated effective value",
			inspection["people"]["effective_value"], expected);
	}

	TEST_METHOD(11) {
		set_test_name("Object type: typecasting works");

		nestedSchema.add("name", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		nestedSchema.add("age", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		nestedSchema.add("address", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL);
		nestedSchema.add("comments", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL, "none");
		nestedSchema.finalize();

		schema.add("people", ConfigKit::OBJECT_TYPE, nestedSchema, ConfigKit::OPTIONAL);
		schema.finalize();

		doc["people"]["first"]["name"] = 123;
		doc["people"]["first"]["age"] = 30;

		ConfigKit::Store store(schema);
		Json::Value preview = store.previewUpdate(doc, errors);
		store.update(doc, errors);
		ensure("There are no errors", errors.empty());
		Json::Value inspection = store.inspect();

		Json::Value expected;
		expected["first"]["name"] = "123";
		expected["first"]["age"] = 30;
		expected["first"]["address"] = Json::nullValue;
		expected["first"]["comments"] = Json::nullValue;
		ensure_equals("Preview user value",
			preview["people"]["user_value"], expected);
		ensure_equals("Updated user value",
			inspection["people"]["user_value"], expected);

		expected = Json::Value();
		expected["first"]["name"] = "123";
		expected["first"]["age"] = 30;
		expected["first"]["address"] = Json::nullValue;
		expected["first"]["comments"] = "none";
		ensure_equals("Preview effective value",
			preview["people"]["effective_value"], expected);
		ensure_equals("Updated effective value",
			inspection["people"]["effective_value"], expected);
	}
}
