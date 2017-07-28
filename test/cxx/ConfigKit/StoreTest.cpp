#include <TestSupport.h>
#include <ConfigKit/Store.h>
#include <boost/bind.hpp>
#include <algorithm>

using namespace Passenger;
using namespace std;

namespace tut {
	struct ConfigKit_StoreTest {
		ConfigKit::Schema schema;
		ConfigKit::Store *config;
		Json::Value doc;
		vector<ConfigKit::Error> errors;

		ConfigKit_StoreTest() {
			config = NULL;
		}

		~ConfigKit_StoreTest() {
			delete config;
		}

		void init() {
			schema.finalize();
			config = new ConfigKit::Store(schema);
		}
	};

	DEFINE_TEST_GROUP(ConfigKit_StoreTest);

	/*********** Test validation ***********/

	TEST_METHOD(1) {
		set_test_name("Validating an empty schema against an empty update set succeeds");

		init();
		config->previewUpdate(doc, errors);
		ensure(errors.empty());
	}

	TEST_METHOD(2) {
		set_test_name("Validating an empty schema against a non-empty update set succeeds");

		init();
		doc["foo"] = "bar";
		config->previewUpdate(doc, errors);
		ensure(errors.empty());
	}

	TEST_METHOD(3) {
		set_test_name("Validating a non-object update set");

		init();
		doc = Json::Value("hello");
		config->previewUpdate(doc, errors);
		ensure_equals(errors.size(), 1u);
		ensure_equals(errors[0].getMessage(), "The JSON document must be an object");
	}

	TEST_METHOD(4) {
		set_test_name("Validating values through ConfigKit::Schema");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.add("bar", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		init();

		doc["bar"] = "string";
		config->previewUpdate(doc, errors);
		std::sort(errors.begin(), errors.end());
		ensure_equals(errors.size(), 2u);
		ensure_equals(errors[0].getMessage(), "'bar' must be an integer");
		ensure_equals(errors[1].getMessage(), "'foo' is required");
	}

	static void addErrorValidator(const ConfigKit::Store &store,
		vector<ConfigKit::Error> &errors)
	{
		errors.push_back(ConfigKit::Error("Cannot read '{{foo}}'!"));
	}

	TEST_METHOD(5) {
		set_test_name("Custom validators");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.addValidator(addErrorValidator);
		schema.addValidator(addErrorValidator);
		init();

		config->previewUpdate(doc, errors);
		std::sort(errors.begin(), errors.end());
		ensure_equals(errors.size(), 3u);
		ensure_equals(errors[0].getMessage(), "'foo' is required");
		ensure_equals(errors[1].getMessage(), "Cannot read 'foo'!");
		ensure_equals(errors[2].getMessage(), "Cannot read 'foo'!");
	}


	/*********** Test other stuff ***********/

	TEST_METHOD(10) {
		set_test_name("previewUpdate()");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.add("bar", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		init();

		doc["foo"] = "string";
		doc["baz"] = true;

		Json::Value preview = config->previewUpdate(doc, errors);
		ensure_equals("1 error", errors.size(), 1u);
		ensure_equals(errors[0].getMessage(), "'bar' is required");
		ensure("foo exists", preview.isMember("foo"));
		ensure("bar exists", preview.isMember("bar"));
		ensure("baz does not exists", !preview.isMember("baz"));
		ensure_equals("foo is a string", preview["foo"]["user_value"].asString(), "string");
		ensure("bar is null", preview["bar"]["user_value"].isNull());
	}

	TEST_METHOD(11) {
		set_test_name("forceApplyUpdatePreview()");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.add("bar", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		init();

		doc["foo"] = "string";
		doc["baz"] = true;

		Json::Value preview = config->previewUpdate(doc, errors);
		ensure_equals("1 error", errors.size(), 1u);
		ensure_equals(errors[0].getMessage(), "'bar' is required");

		config->forceApplyUpdatePreview(preview);
		ensure_equals("foo is a string", config->get("foo").asString(), "string");
		ensure("bar is null", config->get("bar").isNull());
	}

	TEST_METHOD(12) {
		set_test_name("inspect()");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.add("bar", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		init();

		doc["foo"] = "string";
		doc["bar"] = 123;
		ensure(config->update(doc, errors));
		ensure(errors.empty());

		Json::Value dump = config->inspect();
		ensure_equals("foo user value", dump["foo"]["user_value"].asString(), "string");
		ensure_equals("foo effective value", dump["foo"]["effective_value"].asString(), "string");
		ensure_equals("bar user value", dump["bar"]["user_value"].asInt(), 123);
		ensure_equals("bar effective value", dump["bar"]["effective_value"].asInt(), 123);
	}

	TEST_METHOD(13) {
		set_test_name("Default values");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL, "string");
		schema.add("bar", ConfigKit::INT_TYPE, ConfigKit::OPTIONAL, 123);
		init();

		ensure_equals(config->get("foo").asString(), "string");
		ensure_equals(config->get("bar").asInt(), 123);

		Json::Value dump = config->inspect();
		ensure("foo user value", dump["foo"]["user_value"].isNull());
		ensure_equals("foo default value", dump["foo"]["default_value"].asString(), "string");
		ensure_equals("foo effective value", dump["foo"]["effective_value"].asString(), "string");
		ensure("bar user value", dump["bar"]["user_value"].isNull());
		ensure_equals("bar default value", dump["bar"]["default_value"].asInt(), 123);
		ensure_equals("bar effective value", dump["bar"]["effective_value"].asInt(), 123);
	}

	static Json::Value
	getNextValueAndBump(unsigned int *nextValue) {
		unsigned int result = *nextValue;
		(*nextValue)++;
		return result;
	}

	TEST_METHOD(14) {
		set_test_name("Dynamic default values and caching them");
		unsigned int nextValue = 0;

		schema.addWithDynamicDefault("foo", ConfigKit::INT_TYPE,
			ConfigKit::OPTIONAL,
			boost::bind(getNextValueAndBump, &nextValue));
		schema.addWithDynamicDefault("bar", ConfigKit::INT_TYPE,
			ConfigKit::OPTIONAL | ConfigKit::CACHE_DEFAULT_VALUE,
			boost::bind(getNextValueAndBump, &nextValue));
		init();

		ensure_equals("(1)", config->get("foo").asUInt(), 0u);
		ensure_equals("(2)", config->get("foo").asUInt(), 1u);
		ensure_equals("(3)", config->get("bar").asUInt(), 2u);
		ensure_equals("(4)", config->get("bar").asUInt(), 2u);
	}

	TEST_METHOD(15) {
		set_test_name("Read-only keys can only be written to once");

		schema.add("foo", ConfigKit::INT_TYPE,
			ConfigKit::OPTIONAL | ConfigKit::READ_ONLY);
		schema.add("foo2", ConfigKit::INT_TYPE,
			ConfigKit::OPTIONAL | ConfigKit::READ_ONLY);
		init();

		doc["foo"] = 123;
		ensure(config->update(doc, errors));
		doc["foo2"] = 123;
		ensure(config->update(doc, errors));
		ensure_equals(config->get("foo").asInt(), 123);
		ensure(config->get("foo2").isNull());
	}

	TEST_METHOD(16) {
		set_test_name("Filtering password values in inspect()");

		schema.add("password", ConfigKit::PASSWORD_TYPE, ConfigKit::OPTIONAL);
		schema.add("password_default", ConfigKit::PASSWORD_TYPE, ConfigKit::OPTIONAL, "1234");
		schema.add("password_null", ConfigKit::PASSWORD_TYPE, ConfigKit::OPTIONAL);
		init();

		doc["password"] = "foo";
		ensure(config->update(doc, errors));

		doc = config->inspect();

		ensure_equals(doc["password"]["user_value"], Json::Value("[FILTERED]"));
		ensure_equals(doc["password"]["default_value"], Json::Value(Json::nullValue));
		ensure_equals(doc["password"]["effective_value"], Json::Value("[FILTERED]"));

		ensure_equals(doc["password_default"]["user_value"], Json::Value(Json::nullValue));
		ensure_equals(doc["password_default"]["default_value"], Json::Value("[FILTERED]"));
		ensure_equals(doc["password_default"]["effective_value"], Json::Value("[FILTERED]"));

		ensure_equals(doc["password_null"]["user_value"], Json::Value(Json::nullValue));
		ensure_equals(doc["password_null"]["default_value"], Json::Value(Json::nullValue));
		ensure_equals(doc["password_null"]["effective_value"], Json::Value(Json::nullValue));
	}
}
