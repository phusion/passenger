/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2012 Phusion
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
#ifndef _PASSENGER_HTTP_CONSTANTS_H_
#define _PASSENGER_HTTP_CONSTANTS_H_

namespace Passenger {

inline const char *
getStatusCodeAndReasonPhrase(int statusCode) {
	switch (statusCode) {
	case 100:
		return "100 Continue";
	case 101:
		return "101 Switching Protocols";
		break;
	case 102:
		return "102 Processing";
		break;
	case 200:
		return "200 OK";
		break;
	case 201:
		return "201 Created";
		break;
	case 202:
		return "202 Accepted";
		break;
	case 203:
		return "203 Non-Authoritative Information";
		break;
	case 204:
		return "204 No Content";
		break;
	case 205:
		return "205 Reset Content";
		break;
	case 206:
		return "206 Partial Content";
		break;
	case 207:
		return "207 Multi-Status";
		break;
	case 300:
		return "300 Multiple Choices";
		break;
	case 301:
		return "301 Moved Permanently";
		break;
	case 302:
		return "302 Found";
		break;
	case 303:
		return "303 See Other";
		break;
	case 304:
		return "304 Not Modified";
		break;
	case 305:
		return "305 Use Proxy";
		break;
	case 306:
		return "306 Switch Proxy";
		break;
	case 307:
		return "307 Temporary Redirect";
		break;
	case 308:
		// Google Gears: http://code.google.com/p/gears/wiki/ResumableHttpRequestsProposal
		return "308 Resume Incomplete";
		break;
	case 400:
		return "400 Bad Request";
		break;
	case 401:
		return "401 Unauthorized";
		break;
	case 402:
		return "402 Payment Required";
		break;
	case 403:
		return "403 Forbidden";
		break;
	case 404:
		return "404 Not Found";
		break;
	case 405:
		return "405 Method Not Allowed";
		break;
	case 406:
		return "406 Not Acceptable";
		break;
	case 407:
		return "407 Proxy Authentication Required";
		break;
	case 408:
		return "408 Request Timeout";
		break;
	case 409:
		return "409 Conflict";
		break;
	case 410:
		return "410 Gone";
		break;
	case 411:
		return "411 Length Required";
		break;
	case 412:
		return "412 Precondition Failed";
		break;
	case 413:
		return "413 Request Entity Too Large";
		break;
	case 414:
		return "414 Request-URI Too Long";
		break;
	case 415:
		return "415 Unsupported Media Type";
		break;
	case 416:
		return "416 Requested Range Not Satisfiable";
		break;
	case 417:
		return "417 Expectation Failed";
		break;
	case 418:
		return "418 Not A Funny April Fools Joke";
		break;
	case 422:
		return "422 Unprocessable Entity";
		break;
	case 423:
		return "423 Locked";
		break;
	case 424:
		return "424 Unordered Collection";
		break;
	case 426:
		return "426 Upgrade Required";
		break;
	case 449:
		return "449 Retry With";
		break;
	case 450:
		return "450 Blocked";
		break;
	case 500:
		return "500 Internal Server Error";
		break;
	case 501:
		return "501 Not Implemented";
		break;
	case 502:
		return "502 Bad Gateway";
		break;
	case 503:
		return "503 Service Unavailable";
		break;
	case 504:
		return "504 Gateway Timeout";
		break;
	case 505:
		return "505 HTTP Version Not Supported";
		break;
	case 506:
		return "506 Variant Also Negotiates";
		break;
	case 507:
		return "507 Insufficient Storage";
		break;
	case 509:
		return "509 Bandwidth Limit Exceeded";
		break;
	case 510:
		return "510 Not Extended";
		break;
	default:
		return (const char *) 0;
	}
}

} // namespace Passenger

#endif /* _PASSENGER_HTTP_CONSTANTS_H_ */
