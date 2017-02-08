/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_INI_FILE_H_
#define _PASSENGER_INI_FILE_H_

#include <utility>
#include <string>
#include <map>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cctype>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <Exceptions.h>

namespace Passenger {

using namespace std;
using namespace boost;


class IniFileSection {
protected:
	typedef map<string, string> ValueMap;
	string sectionName;
	ValueMap values;

public:
	IniFileSection(const string &sectionName) {
		this->sectionName = sectionName;
	}

	bool hasKey(const string &keyName) const {
		return values.find(keyName) != values.end();
	}

	string get(const string &keyName) const {
		ValueMap::const_iterator it = values.find(keyName);
		if (it != values.end()) {
			return it->second;
		} else {
			return string();
		}
	}
	
	string operator[](const string &keyName) const {
		return get(keyName);
	}

	void set(const string &keyName, const string &value) {
		values[keyName] = value;
	}

	string getSectionName() const {
		return sectionName;
	}

	void inspect() const {
		ValueMap::const_iterator it = values.begin();
		while (it != values.end()) {
			cout << it->first << " = " << it->second << endl;
			it++;
		}
	}
};

class IniFileLexer {
public:
	class Token {
	public:
		enum Kind {
			UNKNOWN = 0,
			NEWLINE,
			SECTION_NAME,
			IDENTIFIER,
			ASSIGNMENT,
			TEXT,
			END_OF_FILE
		};

		const Kind kind;
		const string value;
		const int line;
		const int column;

		// String representations of the Kind enum.
		const static char *identityByKind(Kind kind) {
			const static char* KIND_IDENTITY_TABLE[] = {
				"<T_UNKNOWN>",
				"<T_NEWLINE>",
				"<T_SECTION_NAME>",
				"<T_IDENTIFIER>",
				"<T_ASSIGNMENT>",
				"<T_TEXT>",
				"<T_EOF>"
			};
			
			return KIND_IDENTITY_TABLE[kind];
		}

		Token(const Kind kind, const string &value, const int line, const int column)
			: kind(kind), value(value), line(line), column(column) {
			
		}
		
		class ExpectanceException : public std::exception {
		private:
			char message[255];

		public:
			ExpectanceException(char expected, char got, int line, int column) {
				int messageSize = sizeof(message);
				memset(message, 0, messageSize);
				snprintf(message, messageSize,
					"On line %i, column %i: Expected '%c', got '%c' instead.",
					line, column, expected, got);
			}

			ExpectanceException(Token::Kind expected, Token got) {
				const char *expectedKindString = Token::identityByKind(expected);
				int messageSize = sizeof(message);
				memset(message, 0, messageSize);
				snprintf(message, messageSize,
					"On line %i, column %i: Expected '%s', got '%s' instead.",
					got.line, got.column, expectedKindString, got.value.c_str());
			}

			ExpectanceException(char expected, Token::Kind got, int line, int column) {
				const char *gotKindString = Token::identityByKind(got);
				int messageSize = sizeof(message);
				memset(message, 0, messageSize);
				snprintf(message, messageSize,
					"On line %i, column %i: Expected '%c', got '%s' instead.",
					line, column, expected, gotKindString);
			}

			virtual const char* what() const throw() {
				return message;
			}
		};
	};

	typedef boost::shared_ptr<IniFileLexer::Token> TokenPtr;

	

protected:
	ifstream iniFileStream;

	char lastAcceptedChar;
	int upcomingChar;
	bool upcomingTokenPtrIsStale;

	int currentLine;
	int currentColumn;

	TokenPtr upcomingTokenPtr;

	void expect(char ch) {
		int upcomingChar = iniFileStream.peek();
	
		if (ch != upcomingChar) {
			switch(upcomingChar) {
				case EOF:
					throw Token::ExpectanceException(ch, Token::END_OF_FILE,
						currentLine, currentColumn + 1);
				case '\n':
					throw Token::ExpectanceException(ch, upcomingChar,
						currentLine + 1, 0);
				default:
					throw Token::ExpectanceException(ch, upcomingChar,
						currentLine, currentColumn + 1);
			}
		}
	}

	void accept() {
		if (upcomingChar == EOF) return;
	
		lastAcceptedChar = (char)iniFileStream.get();
		upcomingChar     = iniFileStream.peek();
		currentColumn++;
	
		if (lastAcceptedChar == '\n') {
			currentLine++;
			currentColumn = 1;
		}
	}

	void ignore() {
		if (upcomingChar == EOF) return;
	
		upcomingChar = iniFileStream.peek();
		currentColumn++;
	
		if ((char)iniFileStream.get() == '\n') {
			currentLine++;
			currentColumn = 1;
		}
	}

	void expectAndAccept(char ch) {
		expect(ch);
		accept();
	}

	void acceptWhileNewLine() {
		while (iniFileStream.good() && upcomingChar == '\n') {
			accept();
		}
	}

	void ignoreWhileNotNewLine() {
		while (iniFileStream.good() && upcomingChar != '\n') {
			ignore();
		}
	}

	Token tokenizeIdentifier() {
		int line   = currentLine;
		int column = currentColumn;
		string result;
	
		while (isalnum(upcomingChar) || upcomingChar == '_' || upcomingChar == '-') {
			result.append(1, upcomingChar);
			accept();
		}
			
		return Token(Token::IDENTIFIER, result, line, column);
	}

	Token tokenizeSection() {
		expectAndAccept('[');
		Token sectionName = tokenizeSectionName();
		expectAndAccept(']');
		return sectionName;
	}

	Token tokenizeSectionName() {
		int line   = currentLine;
		int column = currentColumn;
		string result;
	
		//while (upcomingChar != ']' && upcomingChar != '[' && upcomingChar != '\n' && upcomingChar != EOF) {
		while (isalnum(upcomingChar) || upcomingChar == '_' || upcomingChar == '-') {
			result.append(1, upcomingChar);
			accept();
		}
	
		return Token(Token::SECTION_NAME, result, line, column);
	}

	Token tokenizeAssignment() {
		expectAndAccept('=');
		return Token(Token::ASSIGNMENT, "=", currentLine, currentColumn);
	}

	Token tokenizeText() {
		int line   = currentLine;
		int column = currentColumn;
		string result;
	
		while (upcomingChar != '\n' && upcomingChar != EOF) {
			result.append(1, upcomingChar);
			accept();
		}
	
		return Token(Token::TEXT, result, line, column);
	}

	Token tokenizeKey() {
		return tokenizeIdentifier();
	}

	Token tokenizeValue() {
		return tokenizeText();
	}

	Token tokenizeUnknown() {
		int line   = currentLine;
		int column = currentColumn;
		string result;
	
		while (upcomingChar != EOF) {
			result.append(1, upcomingChar);
			accept();
		}
	
		return Token(Token::UNKNOWN, result, line, column);
	}

public:
	IniFileLexer(const string &fileName) {
		currentLine   = 1;
		currentColumn = 1;
		upcomingTokenPtrIsStale = true;
		iniFileStream.open(fileName.c_str());
		if (iniFileStream.fail()) {
			int e = errno;
			throw FileSystemException("Cannot open file '" + fileName + "' for reading",
				e, fileName);
		}
	}

	~IniFileLexer() {
		iniFileStream.close();
	}

	int getCurrentLine() {
		return currentLine;
	}

	int getCurrentColumn() {
		return currentColumn;
	}

	TokenPtr peekToken() {
		if (upcomingTokenPtrIsStale) {
			Token upcomingToken = getToken();
			upcomingTokenPtr = boost::make_shared<Token>(upcomingToken);
			upcomingTokenPtrIsStale = false;
		}
	
		return upcomingTokenPtr;
	}

	Token getToken() {
		if (!upcomingTokenPtrIsStale) {
			upcomingTokenPtrIsStale = true;
			return *upcomingTokenPtr;
		}
	
		while (iniFileStream.good()) {
			upcomingChar = iniFileStream.peek();
			switch(upcomingChar) {
				case '[':
					return tokenizeSection();
				case '\n':
					if (lastAcceptedChar != '\n') {
						accept();
						return Token(Token::NEWLINE, "\n", currentLine, currentColumn);
					} else {
						ignore();
						break;
					}
				case ';':
					// Comment encountered: accept all characters until newline (exclusive).
					ignoreWhileNotNewLine();
					break;
				case '=':
					return tokenizeAssignment();
				case EOF:
					return Token(Token::END_OF_FILE, "<END_OF_FILE>", currentLine, currentColumn);
				default:
					if (isblank(upcomingChar)) {
						ignore();
					} else {
						switch(lastAcceptedChar) {
							case '\n':
								return tokenizeKey();
							case '=':
								return tokenizeValue();
							default:
								return tokenizeUnknown();
						}
					}
			}
		}
	
		return Token(Token::END_OF_FILE, "<END_OF_FILE>", currentLine, currentColumn);
	}
};

typedef boost::shared_ptr<IniFileSection> IniFileSectionPtr;


class IniFile {
protected:
	typedef map<string, IniFileSectionPtr> SectionMap;
	string name;
	SectionMap sections;
	
	class IniFileParser {
	typedef IniFileLexer::Token Token;

	protected:
		IniFileLexer lexer;
		IniFile *iniFile;

		// The Start Symbol.
		void parseSections() {		
			while ((lexer.peekToken())->kind == Token::SECTION_NAME) {
				parseSection();
			}
		}

		void parseSection() {
			Token token = acceptAndReturnif(Token::SECTION_NAME);
			acceptIfEOL();

			string sectionName = token.value;
			IniFileSection *section = new IniFileSection(sectionName);
			iniFile->addSection(section);

			parseSectionBody(section);
		}

		void parseSectionBody(IniFileSection *currentSection) {
			while ((lexer.peekToken())->kind == Token::IDENTIFIER) {
				parseKeyValue(currentSection);
			}
		}

		void parseKeyValue(IniFileSection *currentSection) {
			Token identifierToken = acceptAndReturnif (Token::IDENTIFIER);
			acceptif (Token::ASSIGNMENT);
			Token valueToken = acceptAndReturnif (Token::TEXT);
			acceptIfEOL();
			currentSection->set(identifierToken.value, valueToken.value);
		}

		void acceptif (Token::Kind expectedKind) {
			Token token = lexer.getToken();

			if (token.kind != expectedKind) {
				throw Token::ExpectanceException(expectedKind, token);
			}
		}

		void acceptIfEOL() {
			Token token = lexer.getToken();

			if (token.kind != Token::NEWLINE && token.kind != Token::END_OF_FILE) {
				throw Token::ExpectanceException(Token::NEWLINE, token);
			}
		}

		Token acceptAndReturnif (Token::Kind expectedKind) {
			Token token = lexer.getToken();

			if (token.kind != expectedKind) {
				throw Token::ExpectanceException(expectedKind, token);
			}

			return token;
		}

	public:
		IniFileParser(IniFile *iniFile) : lexer(iniFile->getName()), iniFile(iniFile) {
			parseSections();
		}
	};
	
public:
	IniFile(const string &iniFileName)
		: name(iniFileName)
	{
		IniFileParser parser(this);
	}

	void addSection(IniFileSection *section) {
		sections.insert(make_pair(section->getSectionName(), IniFileSectionPtr(section)));
	}

	IniFileSectionPtr section(const string &sectionName) {
		SectionMap::iterator it = sections.find(sectionName);
		if (it != sections.end()) {
			return it->second;
		} else {
			return IniFileSectionPtr();
		}
	}

	bool hasSection(const string &sectionName) const {
		return sections.find(sectionName) != sections.end();
	}

	string getName() const {
		return name;
	}

	void inspect() const {
		SectionMap::const_iterator it = sections.begin();
		while (it != sections.end()) {
			cout << "[" << (it->first) << "]" << endl;
			it->second->inspect();

			it++;
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_INI_FILE_H_ */
