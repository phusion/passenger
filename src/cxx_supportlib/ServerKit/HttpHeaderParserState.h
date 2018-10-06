/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SERVER_KIT_HTTP_HEADER_PARSER_STATE_H_
#define _PASSENGER_SERVER_KIT_HTTP_HEADER_PARSER_STATE_H_

#include <DataStructures/LString.h>
#include <Algorithms/Hasher.h>

namespace Passenger {
namespace ServerKit {


struct HttpHeaderParserState {
	enum State {
		PARSING_NOT_STARTED,
		PARSING_URL,
		PARSING_FIRST_HEADER_FIELD,
		PARSING_FIRST_HEADER_VALUE,
		PARSING_HEADER_FIELD,
		PARSING_HEADER_VALUE,
		ERROR_SECURITY_PASSWORD_MISMATCH,
		ERROR_SECURITY_PASSWORD_DUPLICATE,
		ERROR_SECURE_HEADER_NOT_ALLOWED,
		ERROR_NORMAL_HEADER_NOT_ALLOWED_AFTER_SECURITY_PASSWORD
	};

	State state;
	bool secureMode;
	http_parser parser;
	Header *currentHeader;
	Hasher hasher;
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_HTTP_HEADER_PARSER_STATE_H_ */
