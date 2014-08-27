/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2014 Phusion
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
#ifndef _PASSENGER_SERVER_KIT_ERRORS_H_
#define _PASSENGER_SERVER_KIT_ERRORS_H_

#include <cstring>

namespace Passenger {
namespace ServerKit {


enum Error {
	CHUNK_SIZE_TOO_LARGE        = -1000,
	CHUNK_SIZE_PARSE_ERROR      = -1001,
	CHUNK_FOOTER_PARSE_ERROR    = -1002,
	CHUNK_FINALIZER_PARSE_ERROR = -1003,
	UNEXPECTED_EOF              = -1004
};

inline const char *
getErrorDesc(int errcode) {
	switch (errcode) {
	case CHUNK_SIZE_TOO_LARGE:
		return "Chunked encoding size too large";
	case CHUNK_SIZE_PARSE_ERROR:
		return "Chunked encoding size string parse error";
	case CHUNK_FOOTER_PARSE_ERROR:
		return "Chunked encoding footer parse error";
	case CHUNK_FINALIZER_PARSE_ERROR:
		return "Chunked encoding final chunk parse error";
	case UNEXPECTED_EOF:
		return "Unexpected end-of-stream";
	default:
		return std::strerror(errcode);
	}
}


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_ERRORS_H_ */
