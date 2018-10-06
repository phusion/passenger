/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2013-2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_STR_INT_TOOLS_TEMPLATE_H_
#define _PASSENGER_STR_INT_TOOLS_TEMPLATE_H_

#include <string>
#include <cstring>
#include <cstdio>
#include <StaticString.h>
#include <StrIntTools/StrIntUtils.h>
#include <DataStructures/StringMap.h>

namespace Passenger {

using namespace std;


/**
 * Implements a simple HTML templating language.
 */
class Template {
private:
	typedef string::size_type size_type;

	struct State {
		string result;
		const StringMap<StaticString> &substitutions;

		State(const StaticString &content, const StringMap<StaticString> &_substitutions)
			: result(content.data(), content.size()),
			  substitutions(_substitutions)
			{ }
	};

	struct Options {
		bool raw;
		string defaultValue;

		Options() {
			raw = false;
		}
	};

	StaticString content;

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

	static string &makeBreakable(string &html) {
		string::size_type i = 0;
		while (i < html.size()) {
			char ch = html[i];
			if (ch == '=' || ch == ',' || ch == ';' || ch == ':') {
				html.insert(i + 1, "<wbr>");
				i += sizeof("<wbr>");
			} else if (ch == '&') {
				// HTML escape character; skip to end.
				do {
					i++;
				} while (i < html.size() && html[i] != ';');
				if (i < html.size()) {
					i++;
				}
			} else {
				i++;
			}
		}
		return html;
	}

	size_type processIf(State &state, size_type pos, size_type conditionEndPos, const string &name) {
		const size_t prefixSize = sizeof("if ") - 1;
		const size_t endIfSize  = sizeof("{{/if}}") - 1;
		const string condition = name.substr(prefixSize, name.size() - prefixSize);
		const string evalResult = state.substitutions.get(condition);

		conditionEndPos += sizeof("}}") - 1;
		size_type endIfPos = state.result.find("{{/if}}", conditionEndPos);
		if (endIfPos == string::npos) {
			return state.result.size();
		}

		if (!evalResult.empty() && evalResult != "false") {
			const string subContent = state.result.substr(conditionEndPos, endIfPos - conditionEndPos);
			State subState(subContent, state.substitutions);
			apply(subState);
			state.result.replace(pos, endIfPos + endIfSize - pos, subState.result);
			return pos + subState.result.size();
		} else {
			state.result.erase(pos, endIfPos - pos + sizeof("{{/if}}") - 1);
			return pos;
		}
	}

	size_type processSubsitution(State &state, size_type pos, size_type endPos, string name) {
		Options options;
		size_type sep = name.find('|');
		if (sep != string::npos) {
			const string optionsString = name.substr(sep + 1);
			name = name.substr(0, sep);
			options = parseOptions(optionsString);
		}

		string value = state.substitutions.get(name);
		if (value.empty()) {
			value = options.defaultValue;
		}
		if (!options.raw) {
			value = escapeHTML(value);
			makeBreakable(value);
		}

		state.result.replace(pos, endPos - pos + (sizeof("}}") - 1), value);
		return pos + value.size();
	}

	size_type processCommand(State &state, size_type pos) {
		size_type endPos = state.result.find("}}", pos);
		if (endPos == string::npos) {
			return state.result.size();
		}

		string name = state.result.substr(pos + 2, endPos - pos - 2);
		if (startsWith(name, "if ")) {
			return processIf(state, pos, endPos, name);
		} else {
			return processSubsitution(state, pos, endPos, name);
		}
	}

	void apply(State &state) {
		size_type searchStart = 0;

		while (searchStart < state.result.size()) {
			size_type pos = state.result.find("{{", searchStart);
			if (pos == string::npos) {
				searchStart = state.result.size();
			} else {
				searchStart = processCommand(state, pos);
			}
		}
	}

public:
	Template(const StaticString &_content)
		: content(_content)
		{ }

	string apply(const StringMap<StaticString> &substitutions) {
		State state(content, substitutions);
		apply(state);
		return state.result;
	}

	static string apply(const StaticString &content, const StringMap<StaticString> &substitutions) {
		return Template(content).apply(substitutions);
	}
};


} // namespace Passenger

#endif /* _PASSENGER_STR_INT_TOOLS_TEMPLATE_H_ */
