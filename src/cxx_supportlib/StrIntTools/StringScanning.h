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
#ifndef _PASSENGER_STR_INT_TOOLS_STRING_SCANNING_H_
#define _PASSENGER_STR_INT_TOOLS_STRING_SCANNING_H_

#include <cstring>
#include <cstdlib>
#include <string>
#include <StaticString.h>
#include <StrIntTools/StrIntUtils.h>


/**
 * Utility functions for scanning strings. Given a pointer to string data,
 * these functions can read or skip parts of it and advance the pointer.
 * Especially useful for parsing the output of command line tools.
 *
 *     const char *data = "hello world 1234";
 *
 *     readNextWord(&data);       // => "hello"
 *     printf("%s\n", data);      // => " world 1234"
 *
 *     readNextWord(&data);       // => "world"
 *     printf("%s\n", data);      // => " 1234"
 *
 *     readNextWordAsInt(&data);  // => 1234
 *     printf("%s\n", data);      // => ""
 *
 *     readNextWord(&data);       // => ParseException,
 *                                //    because end has been reached
 */


namespace Passenger {

using namespace std;

struct ParseException {};

/**
 * Scan the given data for the first word that appears on the first line.
 * Leading whitespaces (but not newlines) are ignored. If a word is found
 * then the word is returned and the data pointer is moved to the end of
 * the word.
 *
 * If the first line only contains whitespaces, or if the first line is empty,
 * then a ParseException is thrown.
 *
 * @post result.size() > 0
 */
inline StaticString
readNextWord(const char **data) {
	skipLeadingWhitespaces(data);
	if (**data == '\n' || **data == '\0') {
		throw ParseException();
	}

	// Find end of word and extract the word.
	const char *endOfWord = *data;
	while (*endOfWord != ' ' && *endOfWord != '\n' && *endOfWord != '\0') {
		endOfWord++;
	}
	StaticString result(*data, endOfWord - *data);

	// Move data pointer to the end of this word.
	*data = endOfWord;
	return result;
}

inline long long
_processNextWordAsLongLong(const StaticString &word, char *nullTerminatedWord) {
	memcpy(nullTerminatedWord, word.c_str(), word.size());
	nullTerminatedWord[word.size()] = '\0';
	if (*nullTerminatedWord == '\0') {
		throw ParseException();
	} else {
		return atoll(nullTerminatedWord);
	}
}

/**
 * Scan the given data for the first word that appears on the first line.
 * Leading whitespaces (but not newlines) are ignored. If a word is found
 * then the word is parsed as a 'long long' number and returned, and the
 * data pointer is moved to the end of the word.
 *
 * If the first line only contains whitespaces, or if the first line is empty,
 * then a ParseException is thrown.
 */
inline long long
readNextWordAsLongLong(const char **data) {
	StaticString word = readNextWord(data);
	if (word.size() < 50) {
		char nullTerminatedWord[50];
		return _processNextWordAsLongLong(word, nullTerminatedWord);
	} else {
		string nullTerminatedWord(word.size() + 1, '\0');
		return _processNextWordAsLongLong(word, &nullTerminatedWord[0]);
	}
}

inline int
_processNextWordAsInt(const StaticString &word, char *nullTerminatedWord) {
	memcpy(nullTerminatedWord, word.c_str(), word.size());
	nullTerminatedWord[word.size()] = '\0';
	if (*nullTerminatedWord == '\0') {
		throw ParseException();
	} else {
		return atoi(nullTerminatedWord);
	}
}

/**
 * Scan the given data for the first word that appears on the first line.
 * Leading whitespaces (but not newlines) are ignored. If a word is found
 * then the word is parsed as an 'int' number and returned, and the
 * data pointer is moved to the end of the word.
 *
 * If the first line only contains whitespaces, or if the first line is empty,
 * then a ParseException is thrown.
 */
inline int
readNextWordAsInt(const char **data) {
	StaticString word = readNextWord(data);
	if (word.size() < 50) {
		char nullTerminatedWord[50];
		return _processNextWordAsInt(word, nullTerminatedWord);
	} else {
		string nullTerminatedWord(word.size() + 1, '\0');
		return _processNextWordAsInt(word, &nullTerminatedWord[0]);
	}
}

inline double
_processNextWordAsDouble(const StaticString &word, char *nullTerminatedWord) {
	memcpy(nullTerminatedWord, word.c_str(), word.size());
	nullTerminatedWord[word.size()] = '\0';
	if (*nullTerminatedWord == '\0') {
		throw ParseException();
	} else {
		return atof(nullTerminatedWord);
	}
}

/**
 * Scan the given data for the first word that appears on the first line.
 * Leading whitespaces (but not newlines) are ignored. If a word is found
 * then the word is parsed as an 'double' number and returned, and the
 * data pointer is moved to the end of the word.
 *
 * If the first line only contains whitespaces, or if the first line is empty,
 * then a ParseException is thrown.
 */
inline double
readNextWordAsDouble(const char **data) {
	StaticString word = readNextWord(data);
	if (word.size() < 50) {
		char nullTerminatedWord[50];
		return _processNextWordAsDouble(word, nullTerminatedWord);
	} else {
		string nullTerminatedWord(word.size() + 1, '\0');
		return _processNextWordAsDouble(word, &nullTerminatedWord[0]);
	}
}

/**
 * Return the first line in the given data, excluding leading and trailing
 * whitespaces, and excluding newline. If the first line only contains
 * whitespaces or if the first line is empty, then the empty string is
 * returned.
 *
 * If the data does not contain a newline, then a ParseException is thrown.
 */
inline string
readRestOfLine(const char *data) {
	skipLeadingWhitespaces(&data);
	// Rest of line is allowed to be empty.
	if (*data == '\n' || *data == '\0') {
		return "";
	}

	// Look for newline character. From there, scan back until we've
	// found a non-whitespace character.
	const char *endOfLine = strchr(data, '\n');
	if (endOfLine == NULL) {
		throw ParseException();
	}
	while (*(endOfLine - 1) == ' ') {
		endOfLine--;
	}

	return string(data, endOfLine - data);
}

inline bool
skipToNextLine(const char **data) {
	const char *pos = strchr(*data, '\n');
	if (pos != NULL) {
		pos++;
		*data = pos;
		return true;
	} else {
		return false;
	}
}

/**
 * Extract the first sentence from the first line in the data, where the end
 * of the sentence is dictated by the `terminator` argument.
 * Leading whitespaces (but not newlines) are ignored. The sentence is returned
 * and the data pointer is moved to 1 character past the terminator.
 *
 * The terminator may not be '\n' or '\0'.
 *
 * A ParseException is thrown if one of the following conditions are encountered:
 *
 *  * The first line only contains whitespaces.
 *  * The first line is empty.
 *  * The read sentence is empty.
 *  * The terminator character is not found.
 *
 * @post result.size() > 0
 */
inline StaticString
readNextSentence(const char **data, char terminator) {
	skipLeadingWhitespaces(data);
	if (**data == '\n' || **data == '\0' || **data == terminator) {
		throw ParseException();
	}

	// Find end of sentence and extract the sentence.
	const char *endOfSentence = *data;
	do {
		endOfSentence++;
	} while (*endOfSentence != terminator && *endOfSentence != '\n' && *endOfSentence != '\0');

	if (*endOfSentence == terminator) {
		StaticString result(*data, endOfSentence - *data);
		// Move data pointer 1 character past the terminator.
		*data = endOfSentence + 1;
		return result;
	} else {
		throw ParseException();
	}
}

} // namespace Passenger

#endif /* _PASSENGER_STR_INT_TOOLS_TEMPLATE_H_ */
