#include <string>
#include <cstring>
#include <cstdio>
#include <StaticString.h>
#include <Utils/StrIntUtils.h>
#include <Utils/StringMap.h>

namespace Passenger {

using namespace std;


class Template {
private:
	struct Options {
		bool raw;
		string defaultValue;

		Options() {
			raw = false;
		}
	};

	static bool isNameCharacter(char ch) {
		return (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch >= '0' && ch <= '9')
			|| ch == '_';
	}

	static StaticString readOptionName(const char **current) {
		while (**current != '\0' && (**current == ',' || **current == ' ')) {
			(*current)++;
		}
		const char *begin = *current;
		while (**current != '\0' && isNameCharacter(**current)) {
			(*current)++;
		}
		const char *end = *current;
		return StaticString(begin, end - begin);
	}

	static StaticString readOptionValue(const char **current) {
		while (**current != '\0' && **current == ' ') {
			(*current)++;
		}
		if (**current == '=') {
			(*current)++;
			const char *begin = *current;
			while (**current != '\0' && **current != ',') {
				(*current)++;
			}
			const char *end = *current;
			return StaticString(begin, end - begin);
		} else {
			return StaticString();
		}
	}

	static Options parseOptions(const string &optionsString) {
		Options options;

		const char *current = optionsString.data();
		while (*current != '\0') {
			StaticString name = readOptionName(&current);
			StaticString value = readOptionValue(&current);
			if (name == "raw") {
				options.raw = true;
			} else if (name == "default") {
				options.defaultValue = value;
			} else {
				fprintf(stderr, "Unknown template option '%s'\n", name.c_str());
			}
		}
		return options;
	}

	static string &replaceAll(string &str, const string &from, const string &to) {
		string::size_type index = 0;
		while (true) {
			index = str.find(from, index);
			if (index == string::npos) {
				break;
			}
			str.replace(index, from.size(), to);
			index += to.size();
		}
		return str;
	}

	static string &makeBreakable(string &html) {
		replaceAll(html, "=", "=<wbr>");
		replaceAll(html, ",", ",<wbr>");
		replaceAll(html, ";", ";<wbr>");
		replaceAll(html, ":", ":<wbr>");
		return html;
	}

public:
	static string apply(const StaticString &templateContent, const StringMap<StaticString> &substitutions) {
		string result = templateContent;
		string::size_type searchStart = 0;
		string::size_type beginPos, endPos;

		while (searchStart < result.size()) {
			beginPos = result.find("{{", searchStart);
			if (beginPos == string::npos) {
				searchStart = result.size();
			} else {
				endPos = result.find("}}", beginPos);
				if (endPos == string::npos) {
					searchStart = result.size();
					continue;
				}
				
				string name = result.substr(beginPos + 2, endPos - beginPos - 2);
				Options options;
				string::size_type sep;
				if ((sep = name.find('|')) != string::npos) {
					string optionsString = name.substr(sep + 1);
					name = name.substr(0, sep);
					options = parseOptions(optionsString);
				}

				string value = substitutions.get(name);
				if (value.empty()) {
					value = options.defaultValue;
				}
				if (!options.raw) {
					value = escapeHTML(value);
					makeBreakable(value);
				}

				result.replace(beginPos, endPos - beginPos + 2, value);
				searchStart = beginPos + value.size();
			}
		}

		return result;
	}
};


} // namespace Passenger
