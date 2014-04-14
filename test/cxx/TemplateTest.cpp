#include <TestSupport.h>
#include <Utils/Template.h>
#include <cstdarg>

using namespace Passenger;

namespace tut {
	struct TemplateTest {
		string apply(const char *templateContent, ...) {
			va_list ap;
			const char *arg;
			StringMap<StaticString> params;

			va_start(ap, templateContent);
			while ((arg = va_arg(ap, const char *)) != NULL) {
				params.set(arg, va_arg(ap, const char *));
			}
			string result = Template::apply(templateContent, params);
			va_end(ap);
			return result;
		}
	};
	
	DEFINE_TEST_GROUP(TemplateTest);
	
	TEST_METHOD(1) {
		// Test 1 substitution.
		string result = apply("hello {{name}}",
			"name", "world",
			NULL);
		ensure_equals(result, "hello world");
	}

	TEST_METHOD(2) {
		// Test multiple substitutions.
		string result = apply("hello {{name}} and {{name2}}",
			"name", "joe",
			"name2", "jane",
			NULL);
		ensure_equals(result, "hello joe and jane");
	}

	TEST_METHOD(3) {
		// Test unspecified substitutions.
		string result = apply("hello {{name}} and {{name2}}!",
			"name", "joe",
			NULL);
		ensure_equals(result, "hello joe and !");
	}

	TEST_METHOD(4) {
		// Test default values.
		string result = apply("hello {{name|default=joe}} and {{name2|default=jane}}",
			NULL);
		ensure_equals(result, "hello joe and jane");
	}

	TEST_METHOD(5) {
		// Substitutions are HTML-escaped by default.
		string result = apply("hello {{name}}",
			"name", "<joe>",
			NULL);
		ensure_equals(result, "hello &lt;joe&gt;");
		result = apply("hello {{name|default=<joe>}}",
			NULL);
		ensure_equals(result, "hello &lt;joe&gt;");
	}

	TEST_METHOD(6) {
		// HTML escaping can be disabled with the 'raw' option.
		string result = apply("hello {{name|raw}}",
			"name", "<joe>",
			NULL);
		ensure_equals(result, "hello <joe>");
	}

	TEST_METHOD(7) {
		// Test combining default values and the 'raw' option.
		string result = apply("hello {{name|raw,default=<joe>}}",
			NULL);
		ensure_equals(result, "hello <joe>");
	}

	TEST_METHOD(8) {
		// Test 'if' statements.
		string result = apply("hello. {{if morning}}good morning. {{/if}}{{if evening}}good evening. {{/if}}",
			"morning", "true",
			NULL);
		ensure_equals(result, "hello. good morning. ");
	}

	TEST_METHOD(9) {
		// An 'if' condition is considered true when it is neither the empty string nor "false".
		string result = apply("hello. {{if morning}}good morning. {{/if}}"
			"{{if afternoon}}good afternoon. {{/if}"
			"{{if evening}}good evening. {{/if}}",
			"morning", "of course",
			"afternoon", "false",
			NULL);
		ensure_equals(result, "hello. good morning. ");
	}

	TEST_METHOD(10) {
		// Test nesting substitutions within 'if's.
		string result = apply("hello. {{if name}}good morning {{name}}.{{/if}}",
			"name", "joe",
			NULL);
		ensure_equals(result, "hello. good morning joe.");
	}

	TEST_METHOD(11) {
		// Test auto-breaking on certain characters.
		string result = apply("{{content}}",
			"content", "Hello, world: a=b;c=d",
			NULL);
		ensure_equals(result, "Hello,<wbr> world:<wbr> a=<wbr>b;<wbr>c=<wbr>d");
	}
}
