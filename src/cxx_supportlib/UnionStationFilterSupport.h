/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_FILTER_SUPPORT_H_
#define _PASSENGER_FILTER_SUPPORT_H_

#ifdef __cplusplus

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <oxt/tracable_exception.hpp>

#include <string>
#include <set>
// Checking for _PCREPOSIX_H avoids conflicts with headers provided by Apache.
// https://code.google.com/p/phusion-passenger/issues/detail?id=651
#ifndef _PCREPOSIX_H
	#include <boost/regex.h>
#endif
#include <cstdio>
#include <cstring>
#include <string.h>
#include <stdlib.h>

#include <StaticString.h>
#include <Exceptions.h>
#include <Utils/StrIntUtils.h>
#include <Utils/ReleaseableScopedPointer.h>

namespace Passenger {
namespace FilterSupport {

using namespace std;
using namespace boost;
using namespace oxt;


class Tokenizer {
public:
	enum TokenType {
		NONE,
		NOT,
		AND,
		OR,
		MATCHES,
		NOT_MATCHES,
		EQUALS,
		NOT_EQUALS,
		GREATER_THAN,
		GREATER_THAN_OR_EQUALS,
		LESS_THAN,
		LESS_THAN_OR_EQUALS,
		LPARENTHESIS,
		RPARENTHESIS,
		COMMA,
		REGEXP,
		STRING,
		INTEGER,
		TRUE_LIT,
		FALSE_LIT,
		IDENTIFIER,
		END_OF_DATA
	};

	enum TokenOptions {
		NO_OPTIONS = 0,
		REGEXP_OPTION_CASE_INSENSITIVE = 1
	};

	struct Token {
		TokenType type;
		int options;
		unsigned int pos;
		unsigned int size;
		StaticString rawValue;

		Token() {
			type = NONE;
		}

		Token(TokenType _type, unsigned int _pos, unsigned int _size, const StaticString &_rawValue)
			: type(_type),
			  options(NO_OPTIONS),
			  pos(_pos),
			  size(_size),
			  rawValue(_rawValue)
			{ }

		string toString() const {
			return Tokenizer::typeToString(type);
		}
	};

private:
	StaticString data;
	bool debug;
	unsigned int pos;

	static bool isWhitespace(char ch) {
		return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
	}

	void skipWhitespaces() {
		while (pos < data.size() && isWhitespace(data[pos])) {
			pos++;
		}
	}

	unsigned int available() const {
		return data.size() - pos;
	}

	char current() const {
		return data[pos];
	}

	char next() const {
		return data[pos + 1];
	}

	static bool isIdentifierChar(char ch) {
		return (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch >= '0' && ch <= '9')
			|| ch == '_';
	}

	static bool isDigit(char ch) {
		return ch >= '0' && ch <= '9';
	}

	Token logToken(const Token &token) const {
		if (debug) {
			printf("# Token: %s\n", token.toString().c_str());
		}
		return token;
	}

	void raiseSyntaxError(const string &message = "") {
		string msg = "Syntax error at character " + toString(pos + 1);
		if (!message.empty()) {
			msg.append(": ");
			msg.append(message);
		}
		throw SyntaxError(msg);
	}

	void expectingAtLeast(unsigned int size) {
		if (available() < size) {
			raiseSyntaxError("at least " + toString(size) +
				" more characters expected");
		}
	}

	void expectingNextChar(char ch) {
		expectingAtLeast(2);
		if (next() != ch) {
			raiseSyntaxError("expected '" + toString(ch) +
				"', but found '" + toString(next()) +
				"'");
		}
	}

	Token matchToken(TokenType type, unsigned int size = 0) {
		unsigned int oldPos = pos;
		pos += size;
		return Token(type, oldPos, size, data.substr(oldPos, size));
	}

	Token matchTokensStartingWithNegation() {
		expectingAtLeast(2);
		switch (next()) {
		case '~':
			return matchToken(NOT_MATCHES, 2);
		case '=':
			return matchToken(NOT_EQUALS, 2);
		default:
			return matchToken(NOT, 1);
		};
	}

	Token matchAnd() {
		expectingNextChar('&');
		return matchToken(AND, 2);
	}

	Token matchOr() {
		expectingNextChar('|');
		return matchToken(OR, 2);
	}

	Token matchTokensStartingWithEquals() {
		expectingAtLeast(2);
		switch (next()) {
		case '~':
			return matchToken(MATCHES, 2);
		case '=':
			return matchToken(EQUALS, 2);
		default:
			raiseSyntaxError("unrecognized operator '" + data.substr(pos, 2) + "'");
			return Token(); // Shut up compiler warning.
		}
	}

	Token matchTokensStartingWithGreaterThan() {
		if (available() == 0 || next() != '=') {
			return matchToken(GREATER_THAN, 1);
		} else {
			return matchToken(GREATER_THAN_OR_EQUALS, 2);
		}
	}

	Token matchTokensStartingWithLessThan() {
		if (available() == 0 || next() != '=') {
			return matchToken(LESS_THAN, 1);
		} else {
			return matchToken(LESS_THAN_OR_EQUALS, 2);
		}
	}

	Token matchRegexp(char terminator) {
		unsigned int start = pos;
		bool endFound = false;

		// Match initial quote slash.
		pos++;

		// Match rest of regexp including terminating slash.
		while (pos < data.size() && !endFound) {
			char ch = current();
			if (ch == '\\') {
				pos++;
				if (pos >= data.size()) {
					raiseSyntaxError("unterminated regular expression");
				} else {
					pos++;
				}
			} else if (ch == terminator) {
				pos++;
				endFound = true;
			} else {
				pos++;
			}
		}

		if (endFound) {
			Token t(REGEXP, start, pos - start, data.substr(start, pos - start));

			// Match regexp options.
			endFound = false;
			while (pos < data.size() && !endFound) {
				char ch = current();
				if (ch == 'i') {
					t.options |= Tokenizer::REGEXP_OPTION_CASE_INSENSITIVE;
				} else if (isWhitespace(ch)) {
					endFound = true;
				}
				pos++;
			}

			return t;
		} else {
			raiseSyntaxError("unterminated regular expression");
			return Token(); // Shut up compiler warning.
		}
	}

	Token matchString(char terminator) {
		unsigned int start = pos;
		bool endFound = false;

		// Match initial quote character.
		pos++;

		// Match rest of string including terminating quote.
		while (pos < data.size() && !endFound) {
			char ch = current();
			if (ch == '\\') {
				pos++;
				if (pos >= data.size()) {
					raiseSyntaxError("unterminated string");
				} else {
					pos++;
				}
			} else if (ch == terminator) {
				pos++;
				endFound = true;
			} else {
				pos++;
			}
		}

		if (endFound) {
			return Token(STRING, start, pos - start, data.substr(start, pos - start));
		} else {
			raiseSyntaxError("unterminated string");
			return Token(); // Shut up compiler warning.
		}
	}

	Token matchInteger() {
		unsigned int start = pos;

		// Accept initial minus or digit.
		pos++;

		while (pos < data.size() && isDigit(data[pos])) {
			pos++;
		}

		return Token(INTEGER, start, pos - start, data.substr(start, pos - start));
	}

	Token matchIdentifier() {
		char ch = current();
		if ((ch >= 'a' && ch <= 'z') ||
		    (ch >= 'A' && ch <= 'Z') ||
		    ch == '_') {
			unsigned int start = pos;
			pos++;
			while (pos < data.size() && isIdentifierChar(current())) {
				pos++;
			}

			StaticString val = data.substr(start, pos - start);
			if (val == "true") {
				return Token(TRUE_LIT, start, pos - start, val);
			} else if (val == "false") {
				return Token(FALSE_LIT, start, pos - start, val);
			} else {
				return Token(IDENTIFIER, start, pos - start, val);
			}
		} else {
			raiseSyntaxError("Identifier expected, but got an unknown token");
			return Token(); // Shut up compiler warning.
		}
	}

public:
	Tokenizer(const StaticString &data, bool debug = false) {
		this->data = data;
		this->debug = debug;
		pos = 0;
	}

	Token getNext() {
		skipWhitespaces();
		if (pos >= data.size()) {
			return logToken(Token(END_OF_DATA, data.size(), 0, ""));
		}

		switch (current()) {
		case '!':
			return logToken(matchTokensStartingWithNegation());
		case '&':
			return logToken(matchAnd());
		case '|':
			return logToken(matchOr());
		case '=':
			return logToken(matchTokensStartingWithEquals());
		case '>':
			return logToken(matchTokensStartingWithGreaterThan());
		case '<':
			return logToken(matchTokensStartingWithLessThan());
		case '(':
			return logToken(matchToken(LPARENTHESIS, 1));
		case ')':
			return logToken(matchToken(RPARENTHESIS, 1));
		case ',':
			return logToken(matchToken(COMMA, 1));
		case '/':
			return logToken(matchRegexp('/'));
		case '%':
			expectingAtLeast(3);
			if (memcmp(data.data() + pos, "%r{", 3) != 0) {
				raiseSyntaxError("expected '%r{', but found '" +
					data.substr(pos, 3) + "'");
			}
			pos += 2;
			return logToken(matchRegexp('}'));
		case '"':
			return logToken(matchString('"'));
		case '\'':
			return logToken(matchString('\''));
		case '-':
			return logToken(matchInteger());
		default:
			if (isDigit(current())) {
				return logToken(matchInteger());
			} else {
				return logToken(matchIdentifier());
			}
		}
	}

	static string typeToString(TokenType type) {
		switch (type) {
		case NONE:
			return "NONE";
		case NOT:
			return "NOT";
		case AND:
			return "AND";
		case OR:
			return "OR";
		case MATCHES:
			return "MATCHES";
		case NOT_MATCHES:
			return "NOT_MATCHES";
		case EQUALS:
			return "EQUALS";
		case NOT_EQUALS:
			return "NOT_EQUALS";
		case GREATER_THAN:
			return "GREATER_THAN";
		case GREATER_THAN_OR_EQUALS:
			return "GREATER_THAN_OR_EQUALS";
		case LESS_THAN:
			return "LESS_THAN";
		case LESS_THAN_OR_EQUALS:
			return "LESS_THAN_OR_EQUALS";
		case LPARENTHESIS:
			return "LPARENTHESIS";
		case RPARENTHESIS:
			return "RPARENTHESIS";
		case COMMA:
			return "COMMA";
		case REGEXP:
			return "REGEXP";
		case STRING:
			return "STRING";
		case INTEGER:
			return "INTEGER";
		case TRUE_LIT:
			return "TRUE";
		case FALSE_LIT:
			return "FALSE";
		case IDENTIFIER:
			return "IDENTIFIER";
		case END_OF_DATA:
			return "END_OF_DATA";
		default:
			return "(unknown)";
		}
	}
};


enum ValueType {
	REGEXP_TYPE,
	STRING_TYPE,
	INTEGER_TYPE,
	BOOLEAN_TYPE,
	UNKNOWN_TYPE
};


class Context {
public:
	enum FieldIdentifier {
		URI,
		CONTROLLER,
		RESPONSE_TIME,
		RESPONSE_TIME_WITHOUT_GC,
		STATUS,
		STATUS_CODE,
		GC_TIME
	};

	virtual ~Context() { }

	virtual string getURI() const = 0;
	virtual string getController() const = 0;
	virtual int getResponseTime() const = 0;
	virtual string getStatus() const = 0;
	virtual int getStatusCode() const = 0;
	virtual int getGcTime() const = 0;
	virtual bool hasHint(const string &name) const = 0;

	int getResponseTimeWithoutGc() const {
		return getResponseTime() - getGcTime();
	}

	string queryStringField(FieldIdentifier id) const {
		switch (id) {
		case URI:
			return getURI();
		case CONTROLLER:
			return getController();
		case RESPONSE_TIME:
			return toString(getResponseTime());
		case RESPONSE_TIME_WITHOUT_GC:
			return toString(getResponseTimeWithoutGc());
		case STATUS:
			return getStatus();
		case STATUS_CODE:
			return toString(getStatusCode());
		case GC_TIME:
			return toString(getGcTime());
		default:
			return "";
		}
	}

	int queryIntField(FieldIdentifier id) const {
		switch (id) {
		case RESPONSE_TIME:
			return getResponseTime();
		case RESPONSE_TIME_WITHOUT_GC:
			return getResponseTimeWithoutGc();
		case STATUS_CODE:
			return getStatusCode();
		case GC_TIME:
			return getGcTime();
		default:
			return 0;
		}
	}

	bool queryBoolField(FieldIdentifier id) const {
		switch (id) {
		case URI:
			return !getURI().empty();
		case CONTROLLER:
			return !getController().empty();
		case RESPONSE_TIME:
			return getResponseTime() > 0;
		case RESPONSE_TIME_WITHOUT_GC:
			return getResponseTimeWithoutGc() > 0;
		case STATUS:
			return !getStatus().empty();
		case STATUS_CODE:
			return getStatusCode() > 0;
		case GC_TIME:
			return getGcTime() > 0;
		default:
			return false;
		}
	}

	static ValueType getFieldType(FieldIdentifier id) {
		switch (id) {
		case URI:
		case CONTROLLER:
		case STATUS:
			return STRING_TYPE;
		case RESPONSE_TIME:
		case RESPONSE_TIME_WITHOUT_GC:
		case STATUS_CODE:
		case GC_TIME:
			return INTEGER_TYPE;
		default:
			return UNKNOWN_TYPE;
		}
	}
};

class SimpleContext: public Context {
public:
	string uri;
	string controller;
	string status;
	int responseTime;
	int statusCode;
	int gcTime;
	set<string> hints;

	SimpleContext() {
		responseTime = 0;
		statusCode = 0;
		gcTime = 0;
	}

	virtual string getURI() const {
		return uri;
	}

	virtual string getController() const {
		return controller;
	}

	virtual int getResponseTime() const {
		return responseTime;
	}

	virtual string getStatus() const {
		return status;
	}

	virtual int getStatusCode() const {
		return statusCode;
	}

	virtual int getGcTime() const {
		return gcTime;
	}

	virtual bool hasHint(const string &name) const {
		return hints.find(name) != hints.end();
	}
};

class ContextFromLog: public Context {
private:
	StaticString logData;
	mutable SimpleContext *parsedData;

	struct ParseState {
		unsigned long long requestProcessingStart;
		unsigned long long requestProcessingEnd;
		unsigned long long smallestTimestamp;
		unsigned long long largestTimestamp;
		unsigned long long gcTimeStart;
		unsigned long long gcTimeEnd;
	};

	static void parseLine(const StaticString &txnId, unsigned long long timestamp,
		const StaticString &data, SimpleContext &ctx, ParseState &state)
	{
		if (startsWith(data, "BEGIN: request processing")) {
			state.requestProcessingStart = extractEventTimestamp(data);
		} else if (startsWith(data, "END: request processing")
		        || startsWith(data, "FAIL: request processing")) {
			state.requestProcessingEnd = extractEventTimestamp(data);
		} else if (startsWith(data, "URI: ")) {
			ctx.uri = data.substr(data.find(':') + 2);
		} else if (startsWith(data, "Controller action: ")) {
			StaticString value = data.substr(data.find(':') + 2);
			size_t pos = value.find('#');
			if (pos != string::npos) {
				ctx.controller = value.substr(0, pos);
			}
		} else if (startsWith(data, "Status: ")) {
			StaticString value = data.substr(data.find(':') + 2);
			ctx.status = value;
			ctx.statusCode = stringToInt(value);
		} else if (startsWith(data, "Initial GC time: ")) {
			StaticString value = data.substr(data.find(':') + 2);
			state.gcTimeStart = stringToULL(value);
		} else if (startsWith(data, "Final GC time: ")) {
			StaticString value = data.substr(data.find(':') + 2);
			state.gcTimeEnd = stringToULL(value);
		}

		if (state.smallestTimestamp == 0 || timestamp < state.smallestTimestamp) {
			state.smallestTimestamp = timestamp;
		}
		if (timestamp > state.largestTimestamp) {
			state.largestTimestamp = timestamp;
		}
	}

	static void reallyParse(const StaticString &data, SimpleContext &ctx) {
		const char *current = data.data();
		const char *end     = data.data() + data.size();

		ParseState state;
		memset(&state, 0, sizeof(state));

		while (current < end) {
			current = skipNewlines(current, end);
			if (current < end) {
				const char *endOfLine = findEndOfLine(current, end);
				StaticString line(current, endOfLine - current);
				if (!line.empty()) {
					StaticString txnId;
					unsigned long long timestamp;
					unsigned int writeCount;
					StaticString lineData;

					// If we want to do more complicated analysis we should sort
					// the lines but for the purposes of ContextFromLog
					// analyzing the data without sorting is good enough.
					if (splitLine(line, txnId, timestamp, writeCount, lineData)) {
						parseLine(txnId, timestamp, lineData, ctx,
						state);
					}
				}
				current = endOfLine;
			}
		}

		if (state.requestProcessingEnd != 0) {
			ctx.responseTime = int(state.requestProcessingEnd -
				state.requestProcessingStart);
		} else if (state.smallestTimestamp != 0) {
			ctx.responseTime = state.largestTimestamp - state.smallestTimestamp;
		}

		if (state.gcTimeEnd != 0) {
			ctx.gcTime = state.gcTimeEnd - state.gcTimeStart;
		}
	}

	static bool splitLine(const StaticString &line, StaticString &txnId,
		unsigned long long &timestamp, unsigned int &writeCount,
		StaticString &data)
	{
		size_t firstDelim = line.find(' ');
		if (firstDelim == string::npos) {
			return false;
		}

		size_t secondDelim = line.find(' ', firstDelim + 1);
		if (secondDelim == string::npos) {
			return false;
		}

		size_t thirdDelim = line.find(' ', secondDelim + 1);
		if (thirdDelim == string::npos) {
			return false;
		}

		txnId = line.substr(0, firstDelim);
		timestamp = hexatriToULL(line.substr(firstDelim + 1, secondDelim - firstDelim - 1));
		writeCount = (unsigned int) hexatriToULL(line.substr(secondDelim + 1,
			thirdDelim - secondDelim - 1));
		data = line.substr(thirdDelim + 1);
		return true;
	}

	static unsigned long long extractEventTimestamp(const StaticString &data) {
		size_t pos = data.find('(');
		if (pos == string::npos) {
			return 0;
		} else {
			pos++;
			size_t start = pos;
			while (pos < data.size() && isDigit(data[pos])) {
				pos++;
			}
			if (pos >= data.size()) {
				return 0;
			} else {
				return hexatriToULL(data.substr(start, pos - start));
			}
		}
	}

	static bool isNewline(char ch) {
		return ch == '\n' || ch == '\r';
	}

	static bool isDigit(char ch) {
		return ch >= '0' && ch <= '9';
	}

	static const char *skipNewlines(const char *current, const char *end) {
		while (current < end && isNewline(*current)) {
			current++;
		}
		return current;
	}

	static const char *findEndOfLine(const char *current, const char *end) {
		while (current < end && !isNewline(*current)) {
			current++;
		}
		return current;
	}

	SimpleContext *parse() const {
		if (parsedData == NULL) {
			ReleaseableScopedPointer<SimpleContext> ctx(new SimpleContext());
			reallyParse(logData, *ctx.get());
			parsedData = ctx.release();
		}
		return parsedData;
	}

public:
	ContextFromLog(const StaticString &logData) {
		this->logData = logData;
		parsedData = NULL;
	}

	~ContextFromLog() {
		delete parsedData;
	}

	virtual string getURI() const {
		return parse()->uri;
	}

	virtual string getController() const {
		return parse()->getController();
	}

	virtual int getResponseTime() const {
		return parse()->getResponseTime();
	}

	virtual string getStatus() const {
		return parse()->getStatus();
	}

	virtual int getStatusCode() const {
		return parse()->getStatusCode();
	}

	virtual int getGcTime() const {
		return parse()->getGcTime();
	}

	virtual bool hasHint(const string &name) const {
		return parse()->hasHint(name);
	}
};


class Filter {
private:
	typedef Tokenizer::Token Token;
	typedef Tokenizer::TokenType TokenType;

	struct BooleanComponent;
	struct MultiExpression;
	struct Comparison;
	struct FunctionCall;
	typedef boost::shared_ptr<BooleanComponent> BooleanComponentPtr;
	typedef boost::shared_ptr<MultiExpression> MultiExpressionPtr;
	typedef boost::shared_ptr<Comparison> ComparisonPtr;
	typedef boost::shared_ptr<FunctionCall> FunctionCallPtr;

	struct BooleanComponent {
		virtual ~BooleanComponent() { }
		virtual bool evaluate(const Context &ctx) = 0;
	};

	enum LogicalOperator {
		AND,
		OR
	};

	enum Comparator {
		MATCHES,
		NOT_MATCHES,
		EQUALS,
		NOT_EQUALS,
		GREATER_THAN,
		GREATER_THAN_OR_EQUALS,
		LESS_THAN,
		LESS_THAN_OR_EQUALS,
		UNKNOWN_COMPARATOR
	};

	struct MultiExpression: public BooleanComponent {
		struct Part {
			LogicalOperator theOperator;
			BooleanComponentPtr expression;
		};

		BooleanComponentPtr firstExpression;
		vector<Part> rest;

		virtual bool evaluate(const Context &ctx) {
			bool result = firstExpression->evaluate(ctx);
			unsigned int i = 0;
			bool done = i == rest.size();

			while (!done) {
				Part &nextPart = rest[i];
				if (nextPart.theOperator == AND) {
					result = result && nextPart.expression->evaluate(ctx);
					done = !result;
				} else {
					result = result || nextPart.expression->evaluate(ctx);
				}
				i++;
				done = done || i == rest.size();
			}

			return result;
		}
	};

	struct Negation: public BooleanComponent {
		BooleanComponentPtr expr;

		Negation(const BooleanComponentPtr &e)
			: expr(e)
			{ }

		virtual bool evaluate(const Context &ctx) {
			return !expr->evaluate(ctx);
		}
	};

	struct Value {
		enum Source {
			REGEXP_LITERAL,
			STRING_LITERAL,
			INTEGER_LITERAL,
			BOOLEAN_LITERAL,
			CONTEXT_FIELD_IDENTIFIER
		};

		Source source;
		union {
			struct {
				char stringStorage[sizeof(string)];
				string *stringPointer;
				struct {
					regex_t regexp;
					int options;
				} regexp;
			} stringOrRegexpValue;
			int intValue;
			bool boolValue;
			Context::FieldIdentifier contextFieldIdentifier;
		} u;

		Value() {
			source = INTEGER_LITERAL;
			u.intValue = 0;
		}

		Value(const Value &other) {
			initializeFrom(other);
		}

		Value(bool regexp, const StaticString &value, bool caseInsensitive = false) {
			if (regexp) {
				source = REGEXP_LITERAL;
			} else {
				source = STRING_LITERAL;
			}
			u.stringOrRegexpValue.stringPointer = new (u.stringOrRegexpValue.stringStorage)
				string(value.data(), value.size());
			if (regexp) {
				int options = REG_EXTENDED;
				u.stringOrRegexpValue.regexp.options = 0;
				if (caseInsensitive) {
					options |= REG_ICASE;
					u.stringOrRegexpValue.regexp.options |=
						Tokenizer::REGEXP_OPTION_CASE_INSENSITIVE;
				}
				regcomp(&u.stringOrRegexpValue.regexp.regexp,
					u.stringOrRegexpValue.stringPointer->c_str(),
					options);
			}
		}

		Value(int val) {
			source = INTEGER_LITERAL;
			u.intValue = val;
		}

		Value(bool val) {
			source = BOOLEAN_LITERAL;
			u.boolValue = val;
		}

		Value(Context::FieldIdentifier identifier) {
			source = CONTEXT_FIELD_IDENTIFIER;
			u.contextFieldIdentifier = identifier;
		}

		~Value() {
			freeStorage();
		}

		Value &operator=(const Value &other) {
			freeStorage();
			initializeFrom(other);
			return *this;
		}

		regex_t *getRegexpValue(const Context &ctx) const {
			if (source == REGEXP_LITERAL) {
				return &storedRegexp();
			} else {
				return NULL;
			}
		}

		string getStringValue(const Context &ctx) const {
			switch (source) {
			case REGEXP_LITERAL:
			case STRING_LITERAL:
				return storedString();
			case INTEGER_LITERAL:
				return toString(u.intValue);
			case BOOLEAN_LITERAL:
				if (u.boolValue) {
					return "true";
				} else {
					return "false";
				}
			case CONTEXT_FIELD_IDENTIFIER:
				return ctx.queryStringField(u.contextFieldIdentifier);
			default:
				return "";
			}
		}

		int getIntegerValue(const Context &ctx) const {
			switch (source) {
			case REGEXP_LITERAL:
				return 0;
			case STRING_LITERAL:
				return atoi(storedString());
			case INTEGER_LITERAL:
				return u.intValue;
			case BOOLEAN_LITERAL:
				return (int) u.boolValue;
			case CONTEXT_FIELD_IDENTIFIER:
				return ctx.queryIntField(u.contextFieldIdentifier);
			default:
				return 0;
			}
		}

		bool getBooleanValue(const Context &ctx) const {
			switch (source) {
			case REGEXP_LITERAL:
				return true;
			case STRING_LITERAL:
				return !storedString().empty();
			case INTEGER_LITERAL:
				return (bool) u.intValue;
			case BOOLEAN_LITERAL:
				return u.boolValue;
			case CONTEXT_FIELD_IDENTIFIER:
				return ctx.queryBoolField(u.contextFieldIdentifier);
			default:
				return 0;
			}
		}

		ValueType getType() const {
			switch (source) {
			case REGEXP_LITERAL:
				return REGEXP_TYPE;
			case STRING_LITERAL:
				return STRING_TYPE;
			case INTEGER_LITERAL:
				return INTEGER_TYPE;
			case BOOLEAN_LITERAL:
				return BOOLEAN_TYPE;
			case CONTEXT_FIELD_IDENTIFIER:
				return Context::getFieldType(u.contextFieldIdentifier);
			default:
				return UNKNOWN_TYPE;
			}
		}

	private:
		const string &storedString() const {
			return *u.stringOrRegexpValue.stringPointer;
		}

		regex_t &storedRegexp() const {
			return (regex_t &) u.stringOrRegexpValue.regexp.regexp;
		}

		void freeStorage() {
			if (source == REGEXP_LITERAL || source == STRING_LITERAL) {
				storedString().~string();
				if (source == REGEXP_LITERAL) {
					regfree(&storedRegexp());
				}
			}
		}

		void initializeFrom(const Value &other) {
			int options;
			source = other.source;
			switch (source) {
			case REGEXP_LITERAL:
				u.stringOrRegexpValue.stringPointer = new (u.stringOrRegexpValue.stringStorage)
					string(other.storedString());
				options = REG_EXTENDED;
				if (other.u.stringOrRegexpValue.regexp.options & Tokenizer::REGEXP_OPTION_CASE_INSENSITIVE) {
					options |= REG_ICASE;
				}
				regcomp(&u.stringOrRegexpValue.regexp.regexp,
					storedString().c_str(),
					options);
				u.stringOrRegexpValue.regexp.options = other.u.stringOrRegexpValue.regexp.options;
				break;
			case STRING_LITERAL:
				u.stringOrRegexpValue.stringPointer = new (u.stringOrRegexpValue.stringStorage)
					string(other.storedString());
				break;
			case INTEGER_LITERAL:
				u.intValue = other.u.intValue;
				break;
			case BOOLEAN_LITERAL:
				u.boolValue = other.u.boolValue;
				break;
			case CONTEXT_FIELD_IDENTIFIER:
				u.contextFieldIdentifier = other.u.contextFieldIdentifier;
				break;
			}
		}
	};

	struct SingleValueComponent: public BooleanComponent {
		Value val;

		SingleValueComponent(const Value &v)
			: val(v)
			{ }

		virtual bool evaluate(const Context &ctx) {
			return val.getBooleanValue(ctx);
		}
	};

	struct Comparison: public BooleanComponent {
		Value subject;
		Comparator comparator;
		Value object;

		virtual bool evaluate(const Context &ctx) {
			switch (subject.getType()) {
			case STRING_TYPE:
				return compareStringOrRegexp(subject.getStringValue(ctx), ctx);
			case INTEGER_TYPE:
				return compareInteger(subject.getIntegerValue(ctx), ctx);
			case BOOLEAN_TYPE:
				return compareBoolean(subject.getBooleanValue(ctx), ctx);
			default:
				// error
				return false;
			}
		}

	private:
		bool compareStringOrRegexp(const string &str, const Context &ctx) {
			switch (comparator) {
			case MATCHES:
				return regexec(object.getRegexpValue(ctx), str.c_str(), 0, NULL, 0) == 0;
			case NOT_MATCHES:
				return regexec(object.getRegexpValue(ctx), str.c_str(), 0, NULL, 0) != 0;
			case EQUALS:
				return str == object.getStringValue(ctx);
			case NOT_EQUALS:
				return str != object.getStringValue(ctx);
			default:
				// error
				return false;
			}
		}

		bool compareInteger(int value, const Context &ctx) {
			int value2 = object.getIntegerValue(ctx);
			switch (comparator) {
			case EQUALS:
				return value == value2;
			case NOT_EQUALS:
				return value != value2;
			case GREATER_THAN:
				return value > value2;
			case GREATER_THAN_OR_EQUALS:
				return value >= value2;
			case LESS_THAN:
				return value < value2;
			case LESS_THAN_OR_EQUALS:
				return value <= value2;
			default:
				// error
				return false;
			}
		}

		bool compareBoolean(bool value, const Context &ctx) {
			bool value2 = object.getBooleanValue(ctx);
			switch (comparator) {
			case EQUALS:
				return value == value2;
			case NOT_EQUALS:
				return value != value2;
			default:
				// error
				return false;
			}
		}
	};

	struct FunctionCall: public BooleanComponent {
		vector<Value> arguments;

		virtual void checkArguments() const = 0;
	};

	struct StartsWithFunctionCall: public FunctionCall {
		virtual bool evaluate(const Context &ctx) {
			return startsWith(arguments[0].getStringValue(ctx),
				arguments[1].getStringValue(ctx));
		}

		virtual void checkArguments() const {
			if (arguments.size() != 2) {
				throw SyntaxError("you passed " + toString(arguments.size()) +
					" argument(s) to starts_with(), but it accepts exactly 2 arguments");
			}
		}
	};

	struct HasHintFunctionCall: public FunctionCall {
		virtual bool evaluate(const Context &ctx) {
			return ctx.hasHint(arguments[0].getStringValue(ctx));
		}

		virtual void checkArguments() const {
			if (arguments.size() != 1) {
				throw SyntaxError("you passed " + toString(arguments.size()) +
					" argument(s) to has_hint(), but it accepts exactly 1 argument");
			}
		}
	};

	Tokenizer tokenizer;
	BooleanComponentPtr root;
	Token lookahead;
	bool debug;

	static bool isLiteralToken(const Token &token) {
		return token.type == Tokenizer::REGEXP
			|| token.type == Tokenizer::STRING
			|| token.type == Tokenizer::INTEGER
			|| token.type == Tokenizer::TRUE_LIT
			|| token.type == Tokenizer::FALSE_LIT;
	}

	static bool isValueToken(const Token &token) {
		return isLiteralToken(token) || token.type == Tokenizer::IDENTIFIER;
	}

	static bool isLogicalOperatorToken(const Token &token) {
		return token.type == Tokenizer::AND
			|| token.type == Tokenizer::OR;
	}

	static Comparator determineComparator(Tokenizer::TokenType type) {
		switch (type) {
		case Tokenizer::MATCHES:
			return MATCHES;
		case Tokenizer::NOT_MATCHES:
			return NOT_MATCHES;
		case Tokenizer::EQUALS:
			return EQUALS;
		case Tokenizer::NOT_EQUALS:
			return NOT_EQUALS;
		case Tokenizer::GREATER_THAN:
			return GREATER_THAN;
		case Tokenizer::GREATER_THAN_OR_EQUALS:
			return GREATER_THAN_OR_EQUALS;
		case Tokenizer::LESS_THAN:
			return LESS_THAN;
		case Tokenizer::LESS_THAN_OR_EQUALS:
			return LESS_THAN_OR_EQUALS;
		default:
			return UNKNOWN_COMPARATOR;
		}
	}

	static bool comparatorAcceptsValueTypes(Comparator cmp, ValueType subjectType, ValueType objectType) {
		switch (cmp) {
		case MATCHES:
		case NOT_MATCHES:
			return subjectType == STRING_TYPE && objectType == REGEXP_TYPE;
		case EQUALS:
		case NOT_EQUALS:
			return (subjectType == STRING_TYPE || subjectType == INTEGER_TYPE || subjectType == BOOLEAN_TYPE)
				&& subjectType == objectType;
		case GREATER_THAN:
		case GREATER_THAN_OR_EQUALS:
		case LESS_THAN:
		case LESS_THAN_OR_EQUALS:
			return subjectType == INTEGER_TYPE && objectType == INTEGER_TYPE;
		default:
			abort();
			return false; // Shut up compiler warning.
		}
	}

	static string unescapeCString(const StaticString &data) {
		string result;
		result.reserve(data.size());

		const char *current = data.data();
		const char *end     = data.data() + data.size();
		while (current < end) {
			char ch = *current;
			if (ch == '\\') {
				current++;
				if (current < end) {
					ch = *current;
					switch (ch) {
					case 'r':
						result.append(1, '\r');
						break;
					case 'n':
						result.append(1, '\n');
						break;
					case 't':
						result.append(1, '\t');
						break;
					default:
						result.append(1, ch);
						break;
					}
					current++;
				}
			} else {
				result.append(1, ch);
				current++;
			}
		}

		return result;
	}

	void logMatch(int level, const char *name) const {
		if (level > 100) {
			// If level is too deep then it's probably a bug.
			abort();
		}
		if (debug) {
			for (int i = 0; i < level; i++) {
				printf("   ");
			}
			printf("Matching: %s\n", name);
		}
	}

	Token peek() const {
		return lookahead;
	}

	bool peek(Tokenizer::TokenType type) const {
		return lookahead.type == type;
	}

	Token match(TokenType type) {
		if (lookahead.type == type) {
			return match();
		} else {
			raiseSyntaxError("Expected a " + Tokenizer::typeToString(type) +
				" token, but got " + lookahead.toString(),
				lookahead);
			return Token(); // Shut up compiler warning.
		}
	}

	Token match() {
		Token old = lookahead;
		lookahead = tokenizer.getNext();
		return old;
	}

	void raiseSyntaxError(const string &msg = "", const Token &token = Token()) {
		if (token.type != Tokenizer::NONE) {
			string message = "at character " + toString(token.pos + 1);
			if (!msg.empty()) {
				message.append(": ");
				message.append(msg);
			}
			throw SyntaxError(message);
		} else {
			throw SyntaxError(msg);
		}
	}

	BooleanComponentPtr matchMultiExpression(int level) {
		logMatch(level, "matchMultiExpression()");
		MultiExpressionPtr result = boost::make_shared<MultiExpression>();

		result->firstExpression = matchExpression(level + 1);
		while (isLogicalOperatorToken(peek())) {
			MultiExpression::Part part;
			part.theOperator = matchOperator(level + 1);
			part.expression  = matchExpression(level + 1);
			result->rest.push_back(part);
		}

		return result;
	}

	BooleanComponentPtr matchExpression(int level) {
		logMatch(level, "matchExpression()");
		bool negate = false;

		if (peek(Tokenizer::NOT)) {
			match();
			negate = true;
		}

		Token next = peek();
		if (next.type == Tokenizer::LPARENTHESIS) {
			match();
			BooleanComponentPtr expression = matchMultiExpression(level + 1);
			match(Tokenizer::RPARENTHESIS);
			if (negate) {
				return boost::make_shared<Negation>(expression);
			} else {
				return expression;
			}
		} else if (isValueToken(next)) {
			BooleanComponentPtr component;
			Token &current = next;
			match();

			if (peek(Tokenizer::LPARENTHESIS)) {
				component = matchFunctionCall(level + 1, current);
			} else if (determineComparator(peek().type) != UNKNOWN_COMPARATOR) {
				component = matchComparison(level + 1, current);
			} else if (current.type == Tokenizer::TRUE_LIT || current.type == Tokenizer::FALSE_LIT) {
				component = matchSingleValueComponent(level + 1, current);
			} else {
				raiseSyntaxError("expected a function call, comparison or boolean literal", current);
			}

			if (negate) {
				return boost::make_shared<Negation>(component);
			} else {
				return component;
			}
		} else {
			raiseSyntaxError("expected a left parenthesis or an identifier", next);
			return BooleanComponentPtr(); // Shut up compiler warning.
		}
	}

	BooleanComponentPtr matchSingleValueComponent(int level, const Token &token) {
		logMatch(level, "matchSingleValueComponent()");
		return boost::make_shared<SingleValueComponent>(matchLiteral(level + 1, token));
	}

	ComparisonPtr matchComparison(int level, const Token &subjectToken) {
		logMatch(level, "matchComparison()");
		ComparisonPtr comparison = boost::make_shared<Comparison>();
		comparison->subject    = matchValue(level + 1, subjectToken);
		comparison->comparator = matchComparator(level + 1);
		comparison->object     = matchValue(level + 1, match());
		if (!comparatorAcceptsValueTypes(comparison->comparator, comparison->subject.getType(), comparison->object.getType())) {
			raiseSyntaxError("the comparator cannot operate on the given combination of types", subjectToken);
		}
		return comparison;
	}

	FunctionCallPtr matchFunctionCall(int level, const Token &id) {
		logMatch(level, "matchFunctionCall()");
		FunctionCallPtr function;

		if (id.rawValue == "starts_with") {
			function = boost::make_shared<StartsWithFunctionCall>();
		} else if (id.rawValue == "has_hint") {
			function = boost::make_shared<HasHintFunctionCall>();
		} else {
			raiseSyntaxError("unknown function '" + id.rawValue + "'", id);
		}

		match(Tokenizer::LPARENTHESIS);
		if (isValueToken(peek())) {
			function->arguments.push_back(matchValue(level + 1, match()));
			while (peek(Tokenizer::COMMA)) {
				match();
				function->arguments.push_back(matchValue(level + 1, match()));
			}
		}
		match(Tokenizer::RPARENTHESIS);
		function->checkArguments();
		return function;
	}

	Value matchValue(int level, const Token &token) {
		logMatch(level, "matchValue()");
		if (isLiteralToken(token)) {
			return matchLiteral(level + 1, token);
		} else if (token.type == Tokenizer::IDENTIFIER) {
			return matchContextFieldIdentifier(level + 1, token);
		} else {
			raiseSyntaxError("Unrecognized value token " +
				Tokenizer::typeToString(token.type), token);
			return Value(); // Shut up compiler warning.
		}
	}

	LogicalOperator matchOperator(int level) {
		logMatch(level, "matchOperator()");
		if (peek(Tokenizer::AND)) {
			logMatch(level + 1, "AND");
			match();
			return AND;
		} else if (peek(Tokenizer::OR)) {
			logMatch(level + 1, "OR");
			match();
			return OR;
		} else {
			raiseSyntaxError("", peek());
			return AND; // Shut up compiler warning.
		}
	}

	Comparator matchComparator(int level) {
		logMatch(level, "matchComparator()");
		Comparator comparator = determineComparator(peek().type);
		if (comparator == UNKNOWN_COMPARATOR) {
			raiseSyntaxError("", peek());
			return MATCHES; // Shut up compiler warning.
		} else {
			logMatch(level + 1, Tokenizer::typeToString(peek().type).c_str());
			match();
			return comparator;
		}
	}

	Value matchLiteral(int level, const Token &token) {
		logMatch(level, "matchLiteral()");
		if (token.type == Tokenizer::REGEXP) {
			logMatch(level + 1, "regexp");
			return Value(true, unescapeCString(token.rawValue.substr(1, token.rawValue.size() - 2)),
				token.options & Tokenizer::REGEXP_OPTION_CASE_INSENSITIVE);
		} else if (token.type == Tokenizer::STRING) {
			logMatch(level + 1, "string");
			return Value(false, unescapeCString(token.rawValue.substr(1, token.rawValue.size() - 2)));
		} else if (token.type == Tokenizer::INTEGER) {
			logMatch(level + 1, "integer");
			return Value(atoi(token.rawValue.toString()));
		} else if (token.type == Tokenizer::TRUE_LIT) {
			logMatch(level + 1, "true");
			return Value(true);
		} else if (token.type == Tokenizer::FALSE_LIT) {
			logMatch(level + 1, "false");
			return Value(false);
		} else {
			raiseSyntaxError("regular expression, string, integer or boolean expected", token);
			return Value(); // Shut up compiler warning.
		}
	}

	Value matchContextFieldIdentifier(int level, const Token &token) {
		logMatch(level, "matchContextFieldIdentifier()");
		if (token.rawValue == "uri") {
			return Value(Context::URI);
		} else if (token.rawValue == "controller") {
			return Value(Context::CONTROLLER);
		} else if (token.rawValue == "response_time") {
			return Value(Context::RESPONSE_TIME);
		} else if (token.rawValue == "response_time_without_gc") {
			return Value(Context::RESPONSE_TIME_WITHOUT_GC);
		} else if (token.rawValue == "status") {
			return Value(Context::STATUS);
		} else if (token.rawValue == "status_code") {
			return Value(Context::STATUS_CODE);
		} else if (token.rawValue == "gc_time") {
			return Value(Context::GC_TIME);
		} else {
			raiseSyntaxError("unknown field '" + token.rawValue + "'", token);
			return Value(); // Shut up compiler warning.
		}
	}

public:
	Filter(const StaticString &source, bool debug = false)
		: tokenizer(source, debug)
	{
		this->debug = debug;
		lookahead = tokenizer.getNext();
		root = matchMultiExpression(0);
		logMatch(0, "end of data");
		match(Tokenizer::END_OF_DATA);
	}

	bool run(const Context &ctx) {
		return root->evaluate(ctx);
	}
};

typedef boost::shared_ptr<Filter> FilterPtr;


} // namespace FilterSupport
} // namespace Passenger

#endif /* __cplusplus */


/********* C bindings *********/

#ifdef __cplusplus
extern "C" {
#endif

typedef void *PassengerFilter;

PassengerFilter *passenger_filter_create(const char *source, int size, char **error);
void passenger_filter_free(PassengerFilter *filter);
char *passenger_filter_validate(const char *source, int size);

#ifdef __cplusplus
}
#endif


#endif /* _PASSENGER_FILTER_SUPPORT_H_ */
