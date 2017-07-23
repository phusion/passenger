/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_HTTP_CONSTANTS_H_
#define _PASSENGER_HTTP_CONSTANTS_H_

namespace Passenger {

inline const char *
getStatusCodeAndReasonPhrase(int statusCode) {
	// http://en.wikipedia.org/wiki/List_of_HTTP_status_codes
	switch (statusCode) {
	case 100:
		return "100 Continue";
	case 101:
		return "101 Switching Protocols";
	case 102:
		return "102 Processing";
	case 200:
		return "200 OK";
	case 201:
		return "201 Created";
	case 202:
		return "202 Accepted";
	case 203:
		return "203 Non-Authoritative Information";
	case 204:
		return "204 No Content";
	case 205:
		return "205 Reset Content";
	case 206:
		return "206 Partial Content";
	case 207:
		return "207 Multi-Status";
	case 208:
		return "208 Already Reported";
	case 226:
		return "226 IM Used";
	case 300:
		return "300 Multiple Choices";
	case 301:
		return "301 Moved Permanently";
	case 302:
		return "302 Found";
	case 303:
		return "303 See Other";
	case 304:
		return "304 Not Modified";
	case 305:
		return "305 Use Proxy";
	case 306:
		return "306 Switch Proxy";
	case 307:
		return "307 Temporary Redirect";
	case 308:
		// Google Gears: http://code.google.com/p/gears/wiki/ResumableHttpRequestsProposal
		return "308 Resume Incomplete";
	case 400:
		return "400 Bad Request";
	case 401:
		return "401 Unauthorized";
	case 402:
		return "402 Payment Required";
	case 403:
		return "403 Forbidden";
	case 404:
		return "404 Not Found";
	case 405:
		return "405 Method Not Allowed";
	case 406:
		return "406 Not Acceptable";
	case 407:
		return "407 Proxy Authentication Required";
	case 408:
		return "408 Request Timeout";
	case 409:
		return "409 Conflict";
	case 410:
		return "410 Gone";
	case 411:
		return "411 Length Required";
	case 412:
		return "412 Precondition Failed";
	case 413:
		return "413 Request Entity Too Large";
	case 414:
		return "414 Request-URI Too Long";
	case 415:
		return "415 Unsupported Media Type";
	case 416:
		return "416 Requested Range Not Satisfiable";
	case 417:
		return "417 Expectation Failed";
	case 418:
		return "418 Not A Funny April Fools Joke";
	case 420:
		// https://dev.twitter.com/docs/error-codes-responses
		return "420 Enhance Your Calm";
	case 422:
		return "422 Unprocessable Entity";
	case 423:
		return "423 Locked";
	case 424:
		return "424 Unordered Collection";
	case 426:
		return "426 Upgrade Required";
	case 428:
		return "428 Precondition Required";
	case 429:
		return "429 Too Many Requests";
	case 431:
		return "431 Request Header Fields Too Large";
	case 444:
		// Nginx specific, used for timeouts.
		return "444 No Response";
	case 449:
		return "449 Retry With";
	case 450:
		return "450 Blocked";
	case 500:
		return "500 Internal Server Error";
	case 501:
		return "501 Not Implemented";
	case 502:
		return "502 Bad Gateway";
	case 503:
		return "503 Service Unavailable";
	case 504:
		return "504 Gateway Timeout";
	case 505:
		return "505 HTTP Version Not Supported";
	case 506:
		return "506 Variant Also Negotiates";
	case 507:
		return "507 Insufficient Storage";
	case 509:
		return "509 Bandwidth Limit Exceeded";
	case 510:
		return "510 Not Extended";
	case 511:
		return "511 Network Authentication Required";
	default:
		return (const char *) 0;
	}
}

} // namespace Passenger

#endif /* _PASSENGER_HTTP_CONSTANTS_H_ */
