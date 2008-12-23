#include <string>
#include <map>

using namespace std;

namespace Passenger {

/**
 * A parser for SCGI requests. It parses the request header and ignores the
 * body data.
 *
 * You can use it by constructing a parser object, then feeding data to the
 * parser until it has reached a final state.
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
 *    // Parser is done when its return value isn't equal to the input size.
 *    
 *    // Check whether a parse error occured.
 *    if (parser.getState() == ScgiRequestParser::ERROR) {
 *        bailOut();
 *    } else {
 *        // All good! Do something with the SCGI header that the parser parsed.
 *        processHeader(parser.getHeaderData());
 *        
 *        // If the last buffer passed to the parser also contains body data,
 *        // then the body data starts at 'buf + bytesAccepted'.
 *        if (bytesAccepted < size) {
 *            processBody(buf + bytesAccepted);
 *        }
 *        while (!end_of_file(fd)) {
 *            ... read(...) ...
 *            processBody(...);
 *        }
 *    }
 * @endcode
 *
 * Parser properties:
 * - A parser object can only process a single SCGI request. You must discard
 *   the existing parser object and create a new one if you want to process
 *   another SCGI request.
 * - This parser checks whether the header netstring is valid. It will enter
 *   the error state if it encounters a parse error.
 * - However, this parser does not perform any validation of the actual header
 *   contents. For example, it doesn't check that CONTENT_LENGTH is the first
 *   header, or that the SCGI header is present.
 */
class ScgiRequestParser {
public:
	enum State {
		READING_LENGTH_STRING,
		READING_HEADER_DATA,
		EXPECTING_COMMA,
		DONE,
		ERROR
	};
	
private:
	State state;
	char lengthStringBuffer[sizeof("4294967296")];
	unsigned int lengthStringBufferSize;
	unsigned long headerSize;
	string headerBuffer;
	map<string, string> headers;
	
	static inline bool isDigit(char byte) {
		return byte >= '0' && byte <= '9';
	}
	
	bool parseHeaderData(const string &data, map<string, string> &output) {
		bool isName = true;
		string key, value;
		
		for (unsigned int i = 0; i < data.size(); i++) {
			char byte = data[i];
			
			if (isName) {
				if (byte == '\0') {
					isName = false;
				} else {
					key.append(1, byte);
				}
			} else {
				if (byte == '\0') {
					isName = true;
					output[key] = value;
					key.clear();
					value.clear();
				} else {
					value.append(1, byte);
				}
			}
		}
		return isName && key.empty() && value.empty();
	}
	
	unsigned int readHeaderData(const char *data, unsigned int size) {
		unsigned int bytesToRead;
		
		if (size < headerSize - headerBuffer.size()) {
			bytesToRead = size;
		} else {
			bytesToRead = headerSize - headerBuffer.size();
		}
		headerBuffer.append(data, bytesToRead);
		if (headerBuffer.size() == headerSize) {
			if (bytesToRead < size) {
				if (data[bytesToRead] == ',') {
					if (parseHeaderData(headerBuffer, headers)) {
						state = DONE;
						return bytesToRead + 1;
					} else {
						state = ERROR;
						return bytesToRead;
					}
				} else {
					state = ERROR;
					return bytesToRead;
				}
			} else {
				if (parseHeaderData(headerBuffer, headers)) {
					state = EXPECTING_COMMA;
				} else {
					state = ERROR;
				}
				return bytesToRead;
				
			}
		} else {
			return bytesToRead;
		}
	}
	
public:
	/**
	 * Create a new ScgiRequestParser, ready to parse a request.
	 */
	ScgiRequestParser() {
		state = READING_LENGTH_STRING;
		lengthStringBufferSize = 0;
		headerSize = 0;
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
	unsigned int feed(const char *data, unsigned int size) {
		unsigned int i;
		
		switch (state) {
		case READING_LENGTH_STRING:
			for (i = 0; i < size; i++) {
				char byte = data[i];
				
				if (lengthStringBufferSize == sizeof(lengthStringBuffer) - 1) {
					state = ERROR;
					return i;
				} else if (!isDigit(byte)) {
					if (byte == ':') {
						state = READING_HEADER_DATA;
						lengthStringBuffer[lengthStringBufferSize] = '\0';
						headerSize = atol(lengthStringBuffer);
						headerBuffer.reserve(headerSize);
						return readHeaderData(data + i + 1, size - i - 1) + i + 1;
					} else {
						state = ERROR;
						return i;
					}
				} else {
					lengthStringBuffer[lengthStringBufferSize] = byte;
					lengthStringBufferSize++;
				}
			}
			return i;
		
		case READING_HEADER_DATA:
			return readHeaderData(data, size);
		
		case EXPECTING_COMMA:
			if (data[0] == ',') {
				state = DONE;
				return 1;
			} else {
				state = ERROR;
				return 0;
			}
		
		default:
			return 0;
		}
	}
	
	/**
	 * Get the raw header data that has been processed so far.
	 */
	string getHeaderData() const {
		return headerBuffer;
	}
	
	/**
	 * Get the value of the header with the given name.
	 * Lookup is case-sensitive.
	 *
	 * Returns the empty string if there is no such header.
	 *
	 * @pre getState() == DONE
	 */
	string getHeader(const string &name) const {
		map<string, string>::const_iterator it(headers.find(name));
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
	bool hasHeader(const string &name) const {
		return headers.find(name) != headers.end();
	}
	
	/**
	 * Get the parser's current state.
	 */
	State getState() const {
		return state;
	}
	
	/**
	 * Checks whether this parser is still capable of accepting input (that
	 * is, that this parser is not in a final state).
	 */
	bool acceptingInput() const {
		return state != DONE && state != ERROR;
	}
};

} // namespace Passenger
