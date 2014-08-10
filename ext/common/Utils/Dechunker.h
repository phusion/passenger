#ifndef _PASSENGER_DECHUNKER_H_
#define _PASSENGER_DECHUNKER_H_

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <StaticString.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {

using namespace std;


/**
 * Parses data in HTTP/1.1 chunked transfer encoding.
 *
 * Feed data into this parser by calling feed(). Do this until acceptingInput()
 * is false. Any data chunks it has parsed will be emitted through the onData
 * callback. This parser is zero-copy so the callback arguments point to the
 * fed data.
 *
 * Dechunker parses until the terminating chunk or until a parse error occurs.
 * After that it will refuse to accept new data until reset() is called.
 */
class Dechunker {
public:
	typedef void (*DataCallback)(const char *data, size_t size, void *userData);
	typedef void (*Callback)(void *userData);

private:
	static const char CR = '\x0D';
	static const char LF = '\x0A';

	char sizeBuffer[10];
	unsigned int sizeBufferLen;
	unsigned int remainingDataSize;
	const char *errorMessage;

	enum {
		EXPECTING_SIZE,
		EXPECTING_CHUNK_EXTENSION,
		EXPECTING_HEADER_LF,
		EXPECTING_DATA,
		EXPECTING_NON_FINAL_CR,
		EXPECTING_NON_FINAL_LF,
		EXPECTING_FINAL_CR,
		EXPECTING_FINAL_LF,
		DONE,
		ERROR
	} state;

	void setError(const char *message) {
		errorMessage = message;
		state = ERROR;
	}

	bool isDigit(char ch) const {
		return (ch >= '0' && ch <= '9')
			|| (ch >= 'a' && ch <= 'f')
			|| (ch >= 'A' && ch <= 'Z');
	}

	void parseSizeBuffer() {
		remainingDataSize = hexToUint(StaticString(sizeBuffer, sizeBufferLen));
	}

	void emitDataEvent(const char *data, size_t size) const {
		if (onData != NULL) {
			onData(data, size, userData);
		}
	}

	void emitEndEvent() const {
		if (onEnd != NULL) {
			onEnd(userData);
		}
	}

public:
	DataCallback onData;
	Callback onEnd;
	void *userData;

	Dechunker() {
		onData = NULL;
		onEnd = NULL;
		userData = NULL;
		reset();
	}

	/**
	 * Resets the internal state so that this Dechunker can be reused
	 * for parsing new data.
	 *
	 * @post acceptingInput()
	 * @post !hasError()
	 */
	void reset() {
		state = EXPECTING_SIZE;
		sizeBufferLen = 0;
		remainingDataSize = 0;
		errorMessage = NULL;
	}

	/**
	 * Feeds data into this parser. Any data chunks it has parsed will be emitted
	 * through the onData callback. Returns the number of bytes that have been
	 * accepted. Any data not recognized as part of the chunked transfer encoding
	 * stream will be rejected.
	 */
	size_t feed(const char *data, size_t size) {
		const char *current = data;
		const char *end     = data + size;
		const char *needle;
		size_t dataSize;

		while (current < end && state != DONE && state != ERROR) {
			switch (state) {
			case EXPECTING_DATA:
				dataSize = std::min(size_t(remainingDataSize), size_t(end - current));
				if (dataSize == 0) {
					state = EXPECTING_FINAL_CR;
				} else {
					emitDataEvent(current, dataSize);
					current += dataSize;
					remainingDataSize -= (unsigned int) dataSize;
					if (remainingDataSize == 0) {
						state = EXPECTING_NON_FINAL_CR;
					}
				}
				break;

			case EXPECTING_SIZE:
				while (current < end
				    && sizeBufferLen < sizeof(sizeBuffer)
				    && state == EXPECTING_SIZE)
				{
					if (*current == CR) {
						parseSizeBuffer();
						state = EXPECTING_HEADER_LF;
					} else if (*current == ';') {
						parseSizeBuffer();
						state = EXPECTING_CHUNK_EXTENSION;
					} else if (isDigit(*current)) {
						sizeBuffer[sizeBufferLen] = *current;
						sizeBufferLen++;
					} else {
						setError("Parse error: invalid chunk size character.");
						current--;
					}
					current++;
				}

				if (sizeBufferLen == sizeof(sizeBuffer) && state == EXPECTING_SIZE) {
					setError("The chunk size header is too large.");
				}
				break;

			case EXPECTING_CHUNK_EXTENSION:
				needle = (const char *) memchr(current, CR, end - current);
				if (needle == NULL) {
					current = end;
				} else {
					current = needle + 1;
					state = EXPECTING_HEADER_LF;
				}
				break;

			case EXPECTING_HEADER_LF:
				if (*current == LF) {
					state = EXPECTING_DATA;
					current++;
				} else {
					setError("Parse error: expected a chunk header LF.");
				}
				break;

			case EXPECTING_NON_FINAL_CR:
				if (*current == CR) {
					state = EXPECTING_NON_FINAL_LF;
					current++;
				} else {
					setError("Parse error: expected a chunk finalizing CR.");
				}
				break;

			case EXPECTING_NON_FINAL_LF:
				if (*current == LF) {
					reset();
					current++;
				} else {
					setError("Parse error: expected a chunk finalizing LF.");
				}
				break;

			case EXPECTING_FINAL_CR:
				if (*current == CR) {
					state = EXPECTING_FINAL_LF;
					current++;
				} else {
					setError("Parse error: expected a final CR.");
				}
				break;

			case EXPECTING_FINAL_LF:
				if (*current == LF) {
					emitEndEvent();
					state = DONE;
					current++;
				} else {
					setError("Parse error: expected a final LF.");
				}
				break;

			default:
				abort();
			}
		}

		return current - data;
	}

	bool acceptingInput() const {
		return state != DONE && state != ERROR;
	}

	bool hasError() const {
		return state == ERROR;
	}

	const char *getErrorMessage() const {
		return errorMessage;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_DECHUNKER_H_ */
