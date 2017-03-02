#include <TestSupport.h>
#include <ConfigKit/Common.h>
#include <ConfigKit/TableTranslator.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct ConfigKit_TranslationTest {
	};

	DEFINE_TEST_GROUP(ConfigKit_TranslationTest);

	TEST_METHOD(1) {
		ConfigKit::TableTranslator translator;
		ConfigKit::Error error("Key {{foo}} is invalid when {{bar}} is given");
		vector<ConfigKit::Error> errors;

		translator.add("bar", "main_bar");
		translator.finalize();

		errors.push_back(error);
		errors = translator.translate(errors);

		ensure_equals(errors.size(), 1u);
		ensure_equals(errors[0].getMessage(), "Key foo is invalid when main_bar is given");
	}
}
