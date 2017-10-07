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

	static void logSecretValidator(const ConfigKit::Store &store,
		vector<ConfigKit::Error> &errors)
	{
		errors.push_back(ConfigKit::Error("'{{secret}}' is " + store["secret"].asString()));
	}

	TEST_METHOD(5) {
		set_test_name("Custom validators");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.add("secret", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED | ConfigKit::SECRET);
		schema.addValidator(addErrorValidator);
		schema.addValidator(addErrorValidator);
		schema.addValidator(logSecretValidator);
		init();

		doc["secret"] = "42";
		config->previewUpdate(doc, errors);
		std::sort(errors.begin(), errors.end());
		ensure_equals(errors.size(), 4u);
		ensure_equals(errors[0].getMessage(), "'foo' is required");
		ensure_equals(errors[1].getMessage(), "'secret' is 42");
		ensure_equals(errors[2].getMessage(), "Cannot read 'foo'!");
		ensure_equals(errors[3].getMessage(), "Cannot read 'foo'!");
	}


	/*********** Test other stuff ***********/

	TEST_METHOD(10) {
		set_test_name("previewUpdate()");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.add("bar", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		schema.add("secret", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED | ConfigKit::SECRET);
		schema.add("secret_default", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL | ConfigKit::SECRET, "default");
		schema.add("secret_null", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL | ConfigKit::SECRET);
		init();

		doc["foo"] = "string";
		doc["baz"] = true;
		doc["secret"] = "my secret";

		Json::Value preview = config->previewUpdate(doc, errors);
		ensure_equals("1 error", errors.size(), 1u);
		ensure_equals(errors[0].getMessage(), "'bar' is required");
		ensure("foo exists", preview.isMember("foo"));
		ensure("bar exists", preview.isMember("bar"));
		ensure("baz does not exists", !preview.isMember("baz"));
		ensure_equals("foo is a string", preview["foo"]["user_value"].asString(), "string");
		ensure("bar is null", preview["bar"]["user_value"].isNull());

		ensure_equals("secret user value is filtered",
			preview["secret"]["user_value"].asString(), "[FILTERED]");
		ensure("secret default value is null",
			preview["secret"]["default_value"].isNull());
		ensure_equals("secret effective value is filtered",
			preview["secret"]["effective_value"].asString(), "[FILTERED]");

		ensure("secret_default user value is null",
			preview["secret_default"]["user_value"].isNull());
		ensure_equals("secret_default default value is filtered",
			preview["secret_default"]["default_value"].asString(), "[FILTERED]");
		ensure_equals("secret_default effective value is filtered",
			preview["secret_default"]["effective_value"].asString(), "[FILTERED]");

		ensure("secret_null user value is null",
			preview["secret_null"]["user_value"].isNull());
		ensure("secret_null has no default value",
			preview["secret_null"]["default_value"].isNull());
		ensure("secret_null effective value is null",
			preview["secret_null"]["effective_value"].isNull());
	}

	TEST_METHOD(12) {
		set_test_name("inspect()");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);
		schema.add("bar", ConfigKit::INT_TYPE, ConfigKit::REQUIRED);
		schema.add("secret", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED | ConfigKit::SECRET);
		schema.add("secret_default", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL | ConfigKit::SECRET, "default");
		schema.add("secret_null", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL | ConfigKit::SECRET);
		init();

		doc["foo"] = "string";
		doc["bar"] = 123;
		doc["secret"] = "my secret";
		ensure("update succeeds", config->update(doc, errors));
		ensure("no errors", errors.empty());

		Json::Value dump = config->inspect();
		ensure_equals("foo user value", dump["foo"]["user_value"].asString(), "string");
		ensure_equals("foo effective value", dump["foo"]["effective_value"].asString(), "string");
		ensure_equals("bar user value", dump["bar"]["user_value"].asInt(), 123);
		ensure_equals("bar effective value", dump["bar"]["effective_value"].asInt(), 123);

		ensure_equals("secret user value is filtered",
			dump["secret"]["user_value"].asString(), "[FILTERED]");
		ensure("secret default value is null",
			dump["secret"]["default_value"].isNull());
		ensure_equals("secret effective value is filtered",
			dump["secret"]["effective_value"].asString(), "[FILTERED]");

		ensure("secret_default user value is null",
			dump["secret_default"]["user_value"].isNull());
		ensure_equals("secret_default default value is filtered",
			dump["secret_default"]["default_value"].asString(), "[FILTERED]");
		ensure_equals("secret_default effective value is filtered",
			dump["secret_default"]["effective_value"].asString(), "[FILTERED]");

		ensure("secret_null user value is null",
			dump["secret_null"]["user_value"].isNull());
		ensure("secret_null has no default value",
			dump["secret_null"]["default_value"].isNull());
		ensure("secret_null effective value is null",
			dump["secret_null"]["effective_value"].isNull());
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

	static Json::Value normalizeTargetAndLevel(const Json::Value &values) {
		Json::Value updates(Json::objectValue);

		if (values["target"].isString()) {
			updates["target"]["path"] = values["target"];
		}
		if (!startsWith(values["level"].asString(), "L")) {
			updates["level"] = "L" + values["level"].asString();
		}

		return updates;
	}

	TEST_METHOD(17) {
		set_test_name("Normalizers");

		schema.add("target", ConfigKit::ANY_TYPE, ConfigKit::REQUIRED);
		schema.add("level", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED | ConfigKit::READ_ONLY);
		schema.addNormalizer(normalizeTargetAndLevel);
		init();

		doc["target"] = "/path";
		doc["level"] = "1";
		ensure("(1)", config->update(doc, errors));
		doc = config->inspect();

		ensure("(2)", config->get("target").isObject());
		ensure_equals("(3)", config->get("target")["path"].asString(), "/path");
		ensure("(4)", doc["target"]["user_value"].isObject());
		ensure_equals("(5)", doc["target"]["user_value"]["path"].asString(), "/path");
		ensure_equals("(6)", config->get("level").asString(), "L1");
		ensure_equals("(7)", doc["level"]["user_value"].asString(), "L1");

		doc = Json::objectValue;
		doc["level"] = "2";
		ensure("(10)", config->update(doc, errors));
		doc = config->inspect();

		ensure("(11)", config->get("target").isObject());
		ensure_equals("(12)", config->get("target")["path"].asString(), "/path");
		ensure("(13)", doc["target"]["user_value"].isObject());
		ensure_equals("(14)", doc["target"]["user_value"]["path"].asString(), "/path");
		ensure_equals("(15)", config->get("level").asString(), "L1");
		ensure_equals("(16)", doc["level"]["user_value"].asString(), "L1");
	}

	static Json::Value addExclamationFilter(const Json::Value &val) {
		return val.asString() + "!";
	}

	TEST_METHOD(19) {
		set_test_name("Inspect filters");

		schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED)
			.setInspectFilter(addExclamationFilter);
		init();

		doc["foo"] = "hello";
		ensure("(1)", config->update(doc, errors));
		doc = config->inspect();

		ensure_equals("(2)", config->get("foo").asString(), "hello");
		ensure_equals("(3)", doc["foo"]["user_value"].asString(), "hello!");
		ensure_equals("(4)", doc["foo"]["effective_value"].asString(), "hello!");
	}

	static Json::Value getTest20Default(const ConfigKit::Store &store) {
		return store["a1"].asInt() +
			store["a2"].asInt() +
			store["a4"].asInt() +
			store["a5"].asInt();
	}

	TEST_METHOD(20) {
		set_test_name("Cached dynamic default values that depend on other values");
		using namespace ConfigKit;

		schema.add("a1", INT_TYPE, REQUIRED);
		schema.add("a2", INT_TYPE, REQUIRED);
		schema.addWithDynamicDefault("a3", INT_TYPE,
			OPTIONAL | CACHE_DEFAULT_VALUE, getTest20Default);
		schema.add("a4", INT_TYPE, REQUIRED);
		schema.add("a5", INT_TYPE, REQUIRED);
		init();

		doc["a1"] = 1;
		doc["a2"] = 10;
		doc["a4"] = 100;
		doc["a5"] = 1000;

		ensure("(1)", config->update(doc, errors));
		doc = config->inspect();

		ensure_equals("(2)", config->get("a3").asInt(), 1111);
	}
}
