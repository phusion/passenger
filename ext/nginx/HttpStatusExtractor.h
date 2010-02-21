/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2009 Phusion
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

#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Passenger {

using namespace std;

/**
 * Utility class for extracting the HTTP status value from an HTTP response.
 *
 * This class is used for generating a proper HTTP response. The response data
 * that Passenger backend processes generate are like CGI responses, and do not
 * include an initial "HTTP/1.1 [status here]" line, so this class used to
 * extract the status from the response in order to generate a proper initial
 * HTTP response line.
 *
 * This class is supposed to be used as follows:
 * - Keep feeding HTTP response data until feed() returns true. feed()
 *   buffers all fed data until it is able to extract the HTTP status.
 * - Call getStatusLine() to retrieve the status line, and use this to generate
 *   an HTTP response line.
 * - Call getBuffer() to retrieve all fed data so far. This data can be sent to
 *   the HTTP client.
 *
 * This class will also ensure that the status line contains a status text, e.g.
 * if the HTTP data's status value is only "200" then "OK" will be automatically
 * appended.
 *
 * @note
 * When the API documentation for this class refers to "\r\n", we actually
 * mean "\x0D\x0A" (the HTTP line termination string). "\r\n" is only written
 * out of convenience.
 */
class HttpStatusExtractor {
private:
	static const char CR = '\x0D';
	static const char LF = '\x0A';

	string buffer;
	unsigned int searchStart;
	bool fullHeaderReceived;
	string statusLine;
	
	bool extractStatusLine() {
		static const char statusHeaderName[] = "Status: ";
		string::size_type start_pos, newline_pos;
		
		if (buffer.size() > sizeof(statusHeaderName) - 1
		 && memcmp(buffer.c_str(), statusHeaderName, sizeof(statusHeaderName) - 1) == 0) {
			// Status line starts at beginning of the header.
			start_pos = sizeof(statusHeaderName) - 1;
			newline_pos = buffer.find("\x0D\x0A", 0, 2) + 2;
		} else {
			// Status line is not at the beginning of the header.
			// Look for it.
			start_pos = buffer.find("\x0D\x0AStatus: ");
			if (start_pos != string::npos) {
				start_pos += 2 + sizeof(statusHeaderName) - 1;
				newline_pos = buffer.find("\x0D\x0A", start_pos, 2) + 2;
			}
		}
		if (start_pos != string::npos) {
			// Status line has been found. Extract it.
			statusLine = buffer.substr(start_pos, newline_pos - start_pos);
			addStatusTextIfNecessary();
			return true;
		} else {
			// Status line is not found. Do not change default
			// status line value.
			return false;
		}
	}
	
	void addStatusTextIfNecessary() {
		if (statusLine.find(' ') == string::npos) {
			// The status line doesn't contain a status text, so add it.
			int statusCode = atoi(statusLine.c_str());
			switch (statusCode) {
			case 100:
				statusLine = "100 Continue\x0D\x0A";
				break;
			case 101:
				statusLine = "101 Switching Protocols\x0D\x0A";
				break;
			case 102:
				statusLine = "102 Processing\x0D\x0A";
				break;
			case 200:
				statusLine = "200 OK\x0D\x0A";
				break;
			case 201:
				statusLine = "201 Created\x0D\x0A";
				break;
			case 202:
				statusLine = "202 Accepted\x0D\x0A";
				break;
			case 203:
				statusLine = "203 Non-Authoritative Information\x0D\x0A";
				break;
			case 204:
				statusLine = "204 No Content\x0D\x0A";
				break;
			case 205:
				statusLine = "205 Reset Content\x0D\x0A";
				break;
			case 206:
				statusLine = "206 Partial Content\x0D\x0A";
				break;
			case 207:
				statusLine = "207 Multi-Status\x0D\x0A";
				break;
			case 300:
				statusLine = "300 Multiple Choices\x0D\x0A";
				break;
			case 301:
				statusLine = "301 Moved Permanently\x0D\x0A";
				break;
			case 302:
				statusLine = "302 Found\x0D\x0A";
				break;
			case 303:
				statusLine = "303 See Other\x0D\x0A";
				break;
			case 304:
				statusLine = "304 Not Modified\x0D\x0A";
				break;
			case 305:
				statusLine = "305 Use Proxy\x0D\x0A";
				break;
			case 306:
				statusLine = "306 Switch Proxy\x0D\x0A";
				break;
			case 307:
				statusLine = "307 Temporary Redirect\x0D\x0A";
				break;
			case 308:
				// Google Gears: http://code.google.com/p/gears/wiki/ResumableHttpRequestsProposal
				statusLine = "308 Resume Incomplete\x0D\x0A";
				break;
			case 400:
				statusLine = "400 Bad Request\x0D\x0A";
				break;
			case 401:
				statusLine = "401 Unauthorized\x0D\x0A";
				break;
			case 402:
				statusLine = "402 Payment Required\x0D\x0A";
				break;
			case 403:
				statusLine = "403 Forbidden\x0D\x0A";
				break;
			case 404:
				statusLine = "404 Not Found\x0D\x0A";
				break;
			case 405:
				statusLine = "405 Method Not Allowed\x0D\x0A";
				break;
			case 406:
				statusLine = "406 Not Acceptable\x0D\x0A";
				break;
			case 407:
				statusLine = "407 Proxy Authentication Required\x0D\x0A";
				break;
			case 408:
				statusLine = "408 Request Timeout\x0D\x0A";
				break;
			case 409:
				statusLine = "409 Conflict\x0D\x0A";
				break;
			case 410:
				statusLine = "410 Gone\x0D\x0A";
				break;
			case 411:
				statusLine = "411 Length Required\x0D\x0A";
				break;
			case 412:
				statusLine = "412 Precondition Failed\x0D\x0A";
				break;
			case 413:
				statusLine = "413 Request Entity Too Large\x0D\x0A";
				break;
			case 414:
				statusLine = "414 Request-URI Too Long\x0D\x0A";
				break;
			case 415:
				statusLine = "415 Unsupported Media Type\x0D\x0A";
				break;
			case 416:
				statusLine = "416 Requested Range Not Satisfiable\x0D\x0A";
				break;
			case 417:
				statusLine = "417 Expectation Failed\x0D\x0A";
				break;
			case 418:
				statusLine = "418 Not A Funny April Fools Joke\x0D\x0A";
				break;
			case 422:
				statusLine = "422 Unprocessable Entity\x0D\x0A";
				break;
			case 423:
				statusLine = "423 Locked\x0D\x0A";
				break;
			case 424:
				statusLine = "424 Unordered Collection\x0D\x0A";
				break;
			case 426:
				statusLine = "426 Upgrade Required\x0D\x0A";
				break;
			case 449:
				statusLine = "449 Retry With\x0D\x0A";
				break;
			case 450:
				statusLine = "450 Blocked\x0D\x0A";
				break;
			case 500:
				statusLine = "500 Internal Server Error\x0D\x0A";
				break;
			case 501:
				statusLine = "501 Not Implemented\x0D\x0A";
				break;
			case 502:
				statusLine = "502 Bad Gateway\x0D\x0A";
				break;
			case 503:
				statusLine = "503 Service Unavailable\x0D\x0A";
				break;
			case 504:
				statusLine = "504 Gateway Timeout\x0D\x0A";
				break;
			case 505:
				statusLine = "505 HTTP Version Not Supported\x0D\x0A";
				break;
			case 506:
				statusLine = "506 Variant Also Negotiates\x0D\x0A";
				break;
			case 507:
				statusLine = "507 Insufficient Storage\x0D\x0A";
				break;
			case 509:
				statusLine = "509 Bandwidth Limit Exceeded\x0D\x0A";
				break;
			case 510:
				statusLine = "510 Not Extended\x0D\x0A";
				break;
			default:
				char temp[32];
				snprintf(temp, sizeof(temp),
					"%d Unknown Status Code\x0D\x0A",
					statusCode);
				temp[sizeof(temp) - 1] = '\0';
				statusLine = temp;
			}
		}
	}
	
public:
	HttpStatusExtractor() {
		searchStart = 0;
		fullHeaderReceived = false;
		statusLine = "200 OK\r\n";
	}
	
	/**
	 * Feed HTTP response data to this HttpStatusExtractor.
	 *
	 * One is to keep feeding data until this method returns true.
	 * When a sufficient amount of data has been fed, this method will
	 * extract the status line from the data that has been fed so far,
	 * and return true.
	 *
	 * Do not call this method again once it has returned true.
	 *
	 * It is safe to feed excess data. That is, it is safe if the 'data'
	 * argument contains a part of the HTTP response body.
	 * HttpStatusExtractor will only look for the status line in the HTTP
	 * response header, not in the HTTP response body. All fed data is
	 * buffered and will be available via getBuffer(), so no data will be
	 * lost.
	 *
	 * @return Whether the HTTP status has been extracted yet.
	 * @pre feed() did not previously return true.
	 * @pre data != NULL
	 * @pre size > 0
	 */
	bool feed(const char *data, unsigned int size) {
		if (fullHeaderReceived) {
			return true;
		}
		buffer.append(data, size);
		for (; buffer.size() >= 3 && searchStart < buffer.size() - 3; searchStart++) {
			if (buffer[searchStart] == CR &&
			    buffer[searchStart + 1] == LF &&
			    buffer[searchStart + 2] == CR &&
			    buffer[searchStart + 3] == LF) {
				fullHeaderReceived = true;
				extractStatusLine();
				return true;
			}
		}
		return false;
	}
	
	/**
	 * Returns the HTTP status line that has been determined.
	 *
	 * The default value is "200 OK\r\n", which is returned if the HTTP
	 * response data that has been fed so far does not include a status
	 * line.
	 *
	 * @note The return value includes a trailing CRLF, e.g. "404 Not Found\r\n".
	 */
	string getStatusLine() const {
		return statusLine;
	}
	
	/**
	 * Get the data that has been fed so far.
	 */
	string getBuffer() const {
		return buffer;
	}
};

} // namespace Passenger
