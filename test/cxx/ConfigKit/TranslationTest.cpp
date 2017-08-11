#include <TestSupport.h>
#include <ConfigKit/Common.h>
#include <ConfigKit/TableTranslator.h>
#include <ConfigKit/PrefixTranslator.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct ConfigKit_TranslationTest {
	};

	DEFINE_TEST_GROUP(ConfigKit_TranslationTest);

	TEST_METHOD(1) {
		set_test_name("Test TableTranslator document translation");
		ConfigKit::TableTranslator translator;
		Json::Value doc;

		doc["foo"] = 123;
		doc["bar"] = 456;
		translator.add("bar", "main_bar");
		translator.finalize();

		doc = translator.translate(doc);
		ensure_equals(doc.size(), 2u);
		ensure_equals("Translating docs works",
			doc["foo"].asInt(), 123);
		ensure_equals("Translating docs works",
			doc["main_bar"].asInt(), 456);

		doc = translator.translate(doc);
		ensure_equals(doc.size(), 2u);
		ensure_equals("Translating docs is idempotent",
			doc["foo"].asInt(), 123);
		ensure_equals("Translating docs is idempotent",
			doc["main_bar"].asInt(), 456);

		doc = translator.reverseTranslate(doc);
		ensure_equals(doc.size(), 2u);
		ensure_equals("Reverse translating docs works",
			doc["foo"].asInt(), 123);
		ensure_equals("Reverse translating docs works",
			doc["bar"].asInt(), 456);

		doc = translator.reverseTranslate(doc);
		ensure_equals(doc.size(), 2u);
		ensure_equals("Reverse translating docs is idempotent",
			doc["foo"].asInt(), 123);
		ensure_equals("Reverse translating docs is idempotent",
			doc["bar"].asInt(), 456);
	}

	TEST_METHOD(2) {
		set_test_name("Test TableTranslator error translation");
		ConfigKit::TableTranslator translator;
		ConfigKit::Error error("Key {{foo}} is invalid when {{bar}} is given");
		vector<ConfigKit::Error> errors;
		errors.push_back(error);

		translator.add("bar", "main_bar");
		translator.finalize();

		errors = translator.translate(errors);
		ensure_equals(errors.size(), 1u);
		ensure_equals("Translating errors works",
			errors[0].getMessage(),
			"Key foo is invalid when main_bar is given");

		errors = translator.translate(errors);
		ensure_equals(errors.size(), 1u);
		ensure_equals("Translating errors is idempotent",
			errors[0].getMessage(),
			"Key foo is invalid when main_bar is given");

		errors = translator.reverseTranslate(errors);
		ensure_equals(errors.size(), 1u);
		ensure_equals("Reverse translating errors works",
			errors[0].getMessage(),
			"Key foo is invalid when bar is given");

		errors = translator.reverseTranslate(errors);
		ensure_equals(errors.size(), 1u);
		ensure_equals("Reverse translating errors is idempotent",
			errors[0].getMessage(),
			"Key foo is invalid when bar is given");
	}

	TEST_METHOD(5) {
		set_test_name("Test PrefixTranslator document translation");
		ConfigKit::PrefixTranslator translator("main_");
		Json::Value doc;

		doc["main_foo"] = 123;
		doc["main_bar"] = 456;

		doc = translator.translate(doc);
		ensure_equals(doc.size(), 2u);
		ensure_equals("Translating docs works",
			doc["foo"].asInt(), 123);
		ensure_equals("Translating docs works",
			doc["bar"].asInt(), 456);

		doc = translator.translate(doc);
		ensure_equals(doc.size(), 2u);
		ensure_equals("Translating docs is idempotent",
			doc["foo"].asInt(), 123);
		ensure_equals("Translating docs is idempotent",
			doc["bar"].asInt(), 456);

		doc = translator.reverseTranslate(doc);
		ensure_equals(doc.size(), 2u);
		ensure_equals("Reverse translating docs works",
			doc["main_foo"].asInt(), 123);
		ensure_equals("Reverse translating docs works",
			doc["main_bar"].asInt(), 456);

		doc = translator.reverseTranslate(doc);
		ensure_equals(doc.size(), 2u);
		ensure_equals("Reverse translating docs is idempotent",
			doc["main_foo"].asInt(), 123);
		ensure_equals("Reverse translating docs is idempotent",
			doc["main_bar"].asInt(), 456);
	}

	TEST_METHOD(6) {
		set_test_name("Test PrefixTranslator error translation");
		ConfigKit::PrefixTranslator translator("main_");
		ConfigKit::Error error("Key {{main_foo}} is invalid when {{main_bar}} is given");
		vector<ConfigKit::Error> errors;
		errors.push_back(error);

		errors = translator.translate(errors);
		ensure_equals(errors.size(), 1u);
		ensure_equals("Translating errors works",
			errors[0].getMessage(),
			"Key foo is invalid when bar is given");

		errors = translator.translate(errors);
		ensure_equals(errors.size(), 1u);
		ensure_equals("Translating errors is idempotent",
			errors[0].getMessage(),
			"Key foo is invalid when bar is given");

		errors = translator.reverseTranslate(errors);
		ensure_equals(errors.size(), 1u);
		ensure_equals("Reverse translating errors works",
			errors[0].getMessage(),
			"Key main_foo is invalid when main_bar is given");

		errors = translator.reverseTranslate(errors);
		ensure_equals(errors.size(), 1u);
		ensure_equals("Reverse translating errors is idempotent",
			errors[0].getMessage(),
			"Key main_foo is invalid when main_bar is given");
	}
}
