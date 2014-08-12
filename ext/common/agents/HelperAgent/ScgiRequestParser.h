/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2014 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
#ifndef _PASSENGER_SCGI_REQUEST_PARSER_H_
#define _PASSENGER_SCGI_REQUEST_PARSER_H_

#include <string>
#include <algorithm>
#include <cstdlib>
#include <cstddef>

#include <oxt/macros.hpp>

#include <StaticString.h>
#include <Utils/HashMap.h>

namespace Passenger {

using namespace std;

/**
 * A highly efficient parser for SCGI requests. It parses the request header and
 * ignores the body data. It supports size limiting for security reasons and it
 * is zero-copy whenever possible.
 *
 * <h2>Usage</h2>
 * Construct a parser object, then feed data to the parser until it no longer
 * accepts input, meaning that it has either reached the final (accepting) state
 * or the error state.
 *
 * Example:
 * @code
 *    ScgiRequestParser parser;
 *    char buf[1024 * 16];
 *    ssize_t size;
 *    unsigned in bytesAccepted;
 *
 *    do {
 *        size = read(fd, buf, sizeof(buf));
 *        bytesAccepted = parser.feed(buf, size);
 *    } while (parser.acceptingInput());
 *
 *    // Check whether a parse error occured.
 *    if (parser.getState() == ScgiRequestParser::ERROR) {
 *        bailOut();
 *    } else {
 *        // All good! Do something with the SCGI header that the parser parsed.
 *        processHeader(parser.getHeaderData());
 *        print(parser.getHeader("DOCUMENT_ROOT"));
 *
 *        // If the last buffer passed to the parser also contains body data,
 *        // then the body data starts at 'buf + bytesAccepted'.
 *        if (bytesAccepted < size) {
 *            processBody(buf + bytesAccepted, size - bytesAccepted);
 *        }
 *    }
 * @endcode
 *
 * <h2>Parser properties</h2>
 * - A parser object can only process a single SCGI request. You must discard
 *   the existing parser object and create a new one if you want to process
 *   another SCGI request.
 * - It checks the header netstring for both syntax validity and content validity.
 *   If the netstring value is too large (larger than the given limit) or equal
 *   to 0 then the parser will enter an error state.
 * - It also checks the body for syntax validity, i.e. whether the NULL bytes
 *   are there, whether the closing comma exists, etc. However it does not check
 *   the body contents, e.g. it doesn't check that "CONTENT_LENGTH" is the first
 *   header, or that the "SCGI" header is present.
 *
 * <h2>Zero-copy notes</h2>
 * If the first feed() call contains a full SCGI header, then the parser will enter
 * zero-copy mode. The return value of getHeaderData() will then refer the passed
 * data and the internal header map will also refer to that same data. No extra
 * copy of anything is made. You must ensure that the first call's data is kept
 * around.
 *
 * If the first feed() call does not contain a full SCGI header then the parser
 * will enter buffering mode. All fed data, in so far they're recognized as SCGI
 * headers, will be buffered into an internal string. getHeaderData() and the
 * internal header map will then refer to this internal string. In this case
 * you don't need to ensure that the original data is kept around.
 */
class ScgiRequestParser {
public:
	typedef HashMap< StaticString, StaticString, StaticString::Hash, equal_to<StaticString> > HeaderMap;
	typedef HeaderMap::const_iterator const_iterator;
	typedef HeaderMap::iterator iterator;

	enum State {
		READING_LENGTH_STRING,
		READING_HEADER_DATA,
		EXPECTING_COMMA,
		DONE,
		ERROR
	};

	enum ErrorReason {
		NONE,

		/** The header has a length of 0 bytes. */
		EMPTY_HEADER,

		/** The length string is too large. */
		LENGTH_STRING_TOO_LARGE,

		/** The header is larger than the maxSize value provided to the constructor. */
		LIMIT_REACHED,

		/** The length string contains an invalid character. */
		INVALID_LENGTH_STRING,

		/** A header terminator character (",") was expected, but something else
		 * was encountered instead. */
		HEADER_TERMINATOR_EXPECTED,

		/** The header data itself contains errors. */
		INVALID_HEADER_DATA
	};

private:
	State state;
	ErrorReason errorReason;
	unsigned int lengthStringBufferSize;
	size_t headerSize;
	size_t maxSize;

	StaticString headerData;
	string headerBuffer;
	HeaderMap headers;
	char lengthStringBuffer[sizeof("4294967296")];

	static inline bool isDigit(char byte) {
		return byte >= '0' && byte <= '9';
	}

	/**
	 * Parse the given header data into key-value pairs, returns whether parsing succeeded.
	 */
	bool parseHeaderData(const StaticString &data, HeaderMap &output) {
		const char *current = data.data();
		const char *end     = data.data() + data.size();

		while (current < end) {
			const char *keyEnd = (const char *) memchr(current, '\0', end - current);
			if (OXT_UNLIKELY(
			     OXT_UNLIKELY(keyEnd == NULL)
			  || OXT_UNLIKELY(keyEnd == current))
			) {
				return false;
			}

			StaticString key(current, keyEnd - current);
			current = keyEnd + 1;
			if (OXT_UNLIKELY(current >= end)) {
				return false;
			}

			const char *valueEnd = (const char *) memchr(current, '\0', end - current);
			if (OXT_UNLIKELY(valueEnd == NULL)) {
				return false;
			}

			output[key] = StaticString(current, valueEnd - current);
			current = valueEnd + 1;
		}
		return true;
	}

public:
	/**
	 * Create a new ScgiRequestParser, ready to parse a request.
	 *
	 * @param maxSize The maximum size that the SCGI data is allowed to
	 *                be, or 0 if no limit is desired.
	 */
	ScgiRequestParser(size_t maxSize = 0) {
		this->maxSize = maxSize;
		reset();
	}

	void reset() {
		state = READING_LENGTH_STRING;
		errorReason = NONE;
		lengthStringBufferSize = 0;
		headerSize = 0;
		headerBuffer.clear();
		headers.clear();
		headerData = StaticString();
	}

	/**
	 * Feed SCGI request data to the parser.
	 *
	 * @param data The data to feed.
	 * @param size The size of the data, in bytes.
	 * @return The number of recognized SCGI header bytes. If this value
	 *         equals 'size', then it means all data in 'data' is part of
	 *         the SCGI header. If this value is less than size, then it
	 *         means only some data in 'data' is part of the SCGI header,
	 *         and the remaining 'size - result' bytes are part of the
	 *         request body.
	 * @pre size > 0
	 * @post result <= size
	 * @post if result <= size: getState() == DONE || getState() == ERROR
	 */
	size_t feed(const char *data, size_t size) {
		size_t consumed = 0;

		while (acceptingInput() && consumed < size) {
			switch (state) {
			case READING_LENGTH_STRING:
				while (consumed < size
				    && lengthStringBufferSize < sizeof(lengthStringBuffer)
				    && isDigit(data[consumed]))
				{
					lengthStringBuffer[lengthStringBufferSize] = data[consumed];
					lengthStringBufferSize++;
					consumed++;
				}
				if (consumed < size) {
					if (lengthStringBufferSize == sizeof(lengthStringBuffer)) {
						state = ERROR;
						errorReason = LENGTH_STRING_TOO_LARGE;
					} else if (data[consumed] == ':') {
						if (lengthStringBufferSize == 0) {
							state = ERROR;
							errorReason = INVALID_LENGTH_STRING;
						} else {
							consumed++;
							lengthStringBuffer[lengthStringBufferSize] = '\0';
							headerSize = atol(lengthStringBuffer);
							if (maxSize > 0 && headerSize > maxSize) {
								state = ERROR;
								errorReason = LIMIT_REACHED;
							} else if (headerSize == 0) {
								state = ERROR;
								errorReason = EMPTY_HEADER;
							} else {
								state = READING_HEADER_DATA;
							}
						}
					} else {
						state = ERROR;
						errorReason = INVALID_LENGTH_STRING;
					}
				}
				break;

			case READING_HEADER_DATA: {
				const char *localData = data + consumed;
				size_t localSize = std::min(
					headerSize - headerBuffer.size(),
					size - consumed);
				if (localSize == headerSize) {
					headerData = StaticString(localData, localSize);
					state = EXPECTING_COMMA;
				} else {
					if (headerBuffer.capacity() < headerSize) {
						headerBuffer.reserve(headerSize);
					}
					headerBuffer.append(localData, localSize);
					if (headerBuffer.size() == headerSize) {
						state = EXPECTING_COMMA;
						headerData = headerBuffer;
					}
				}
				consumed += localSize;
				break;
			}

			case EXPECTING_COMMA:
				if (data[consumed] == ',') {
					if (parseHeaderData(headerData, headers)) {
						state = DONE;
					} else {
						state = ERROR;
						errorReason = INVALID_HEADER_DATA;
					}
					consumed++;
				} else {
					state = ERROR;
					errorReason = HEADER_TERMINATOR_EXPECTED;
				}
				break;

			default:
				abort(); // Never reached.
			}
		}

		if (state == EXPECTING_COMMA && headerBuffer.empty()) {
			/* We got all the header data in a single round, except
			 * for the closing comma. The static header data isn't
			 * guaranteed to be around when we do get the comma so
			 * copy it into the buffer.
			 */
			headerBuffer.assign(headerData.c_str(), headerData.size());
			headerData = headerBuffer;
		}

		return consumed;
	}

	/**
	 * Get the raw header data that has been processed so far.
	 * Please read the zero-copy notes in the class description for
	 * important information about the life time of the data this
	 * StaticString points to.
	 */
	StaticString getHeaderData() const {
		return headerData;
	}

	const_iterator getHeaderIterator(const StaticString &name) const {
		return headers.find(name);
	}

	/**
	 * Get the value of the header with the given name.
	 * Lookup is case-sensitive.
	 * Please read the zero-copy notes in the class description for
	 * important information about the life time of the data this
	 * StaticString points to.
	 *
	 * Returns the empty string if there is no such header.
	 *
	 * @pre getState() == DONE
	 */
	StaticString getHeader(const StaticString &name) const {
		HeaderMap::const_iterator it(headers.find(name));
		if (it == headers.end()) {
			return "";
		} else {
			return it->second;
		}
	}

	/**
	 * Checks whether there is a header with the given name.
	 * Lookup is case-sensitive.
	 *
	 * @pre getState() == DONE
	 */
	bool hasHeader(const StaticString &name) const {
		return headers.find(name) != headers.end();
	}

	HeaderMap &getMap() {
		return headers;
	}

	unsigned int size() const {
		return headers.size();
	}

	const_iterator begin() const {
		return headers.begin();
	}

	const_iterator end() const {
		return headers.end();
	}

	/**
	 * Get the parser's current state.
	 */
	State getState() const {
		return state;
	}

	/**
	 * Returns the reason why the parser entered the error state.
	 *
	 * @pre getState() == ERROR
	 */
	ErrorReason getErrorReason() const {
		return errorReason;
	}

	/**
	 * Checks whether this parser is still capable of accepting input (that
	 * is, that this parser is not in a final/error state).
	 */
	bool acceptingInput() const {
		return state != DONE && state != ERROR;
	}

	/**
	 * If one has modified the headers in this ScgiRequestParser, then getHeaderData()
	 * still returns the original header data that doesn't contain any modifications.
	 * Call rebuildData(true) to synchronize that data with the new header map state.
	 *
	 * Calling rebuildData(false) will internalize the header data, if it wasn't
	 * already so.
	 */
	void rebuildData(bool modified) {
		if (modified) {
			string *newHeaderBuffer;
			const_iterator it, end = headers.end();

			if (headerData.data() == headerBuffer.data()) {
				// headerBuffer already used; allocate new temporary storage.
				newHeaderBuffer = new string();
			} else {
				// headerBuffer unused; use it directly.
				newHeaderBuffer = &headerBuffer;
			}
			newHeaderBuffer->reserve(headerSize);
			for (it = headers.begin(); it != end; it++) {
				newHeaderBuffer->append(it->first);
				newHeaderBuffer->append(1, '\0');
				newHeaderBuffer->append(it->second);
				newHeaderBuffer->append(1, '\0');
			}

			if (headerData.data() == headerBuffer.data()) {
				headerBuffer = *newHeaderBuffer;
				delete newHeaderBuffer;
			}
			headerData = headerBuffer;
			headers.clear();
			parseHeaderData(headerData, headers);

		} else if (headerData.data() != headerBuffer.data()) {
			headerBuffer.assign(headerData.data(), headerData.size());
			headerData = headerBuffer;
			headers.clear();
			parseHeaderData(headerData, headers);
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_SCGI_REQUEST_PARSER_H_ */
