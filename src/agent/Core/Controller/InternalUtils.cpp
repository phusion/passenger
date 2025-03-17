/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2025 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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
#include <Core/Controller.h>

/*************************************************************************
 *
 * Internal utility functions for Core::Controller
 *
 *************************************************************************/

namespace Passenger {
namespace Core {

using namespace std;
using namespace boost;


/****************************
 *
 * Private methods
 *
 ****************************/


void
Controller::disconnectWithClientSocketWriteError(Client **client, int e) {
	stringstream message;
	LoggingKit::Level logLevel;
	message << "client socket write error: ";
	message << ServerKit::getErrorDesc(e);
	message << " (errno=" << e << ")";
	if (e == EPIPE || e == ECONNRESET) {
		logLevel = LoggingKit::INFO;
	} else {
		logLevel = LoggingKit::WARN;
	}
	disconnectWithError(client, message.str(), logLevel);
}

void
Controller::disconnectWithAppSocketIncompleteResponseError(Client **client) {
	disconnectWithError(client, "application did not send a complete response");
}

void
Controller::disconnectWithAppSocketReadError(Client **client, int e) {
	stringstream message;
	message << "app socket read error: ";
	message << ServerKit::getErrorDesc(e);
	message << " (errno=" << e << ")";
	disconnectWithError(client, message.str());
}

void
Controller::disconnectWithAppSocketWriteError(Client **client, int e) {
	stringstream message;
	message << "app socket write error: ";
	message << ServerKit::getErrorDesc(e);
	message << " (errno=" << e << ")";
	disconnectWithError(client, message.str());
}

void
Controller::endRequestWithAppSocketIncompleteResponse(Client **client, Request **req) {
	if (!(*req)->responseBegun) {
		// The application might have decided to abort the response because it thinks the client
		// is already gone (Passenger relays socket half-close events from clients), so don't
		// make a big warning out of that situation.
		if ((*req)->halfClosePolicy == Request::HALF_CLOSE_PERFORMED) {
			SKC_DEBUG(*client, "Sending 502 response: application did not send a complete response"
				" (likely because client half-closed)");
		} else {
			SKC_WARN(*client, "Sending 502 response: application did not send a complete response");
		}
		endRequestWithSimpleResponse(client, req,
			getFormattedMessage(*req, "Incomplete response received from application"), 502);
	} else {
		disconnectWithAppSocketIncompleteResponseError(client);
	}
}

void
Controller::endRequestWithAppSocketReadError(Client **client, Request **req, int e) {
	Client *c = *client;
	if (!(*req)->responseBegun) {
		SKC_WARN(*client, "Sending 502 response: application socket read error");
		endRequestWithSimpleResponse(client, req,
			getFormattedMessage(*req, "Application socket read error"), 502);
	} else {
		disconnectWithAppSocketReadError(&c, e);
	}
}

ServerKit::HeaderTable
Controller::getHeadersWithContentType(Request *req)
{
	ServerKit::HeaderTable headers;
	const LString *value = req->headers.lookup(P_STATIC_STRING("content-type"));
	if (value != NULL) {
		if (psg_lstr_cmp(value, P_STATIC_STRING("application/json"))) {
			headers.insert(req->pool, "content-type", "application/json");
		}
		// Here we can extend setting `content-type` with more supported formats, ie: xml,...
	}
	return headers;
}

const string
Controller::getFormattedMessage(Request *req, const StaticString &body)
{
	const LString *value = req->headers.lookup(P_STATIC_STRING("content-type"));
	if (value != NULL) {
		if (psg_lstr_cmp(value, P_STATIC_STRING("application/json"))) {
			return "{\"status\":\"error\", \"message\": \""+body+"\"}";
		}
	}
	return "<h1>"+body+"</h1>";
}

/**
 * `data` must outlive the request.
 */
void
Controller::endRequestWithSimpleResponse(Client **c, Request **r,
	const StaticString &body, int code)
{
	Client *client = *c;
	Request *req = *r;
	ServerKit::HeaderTable headers = getHeadersWithContentType(req);
	headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
	writeSimpleResponse(client, code, &headers, body);
	endRequest(c, r);
}

void
Controller::endRequestAsBadGateway(Client **client, Request **req) {
	if ((*req)->responseBegun) {
		disconnectWithError(client, "bad gateway");
	} else {
		ServerKit::HeaderTable headers = getHeadersWithContentType(*req);
		headers.insert((*req)->pool, "cache-control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(*client, 502, &headers, getFormattedMessage(*req, "Bad Gateway"));
		endRequest(client, req);
	}
}

void
Controller::endRequestAsGatewayTimeout(Client **client, Request **req) {
	if ((*req)->responseBegun) {
		disconnectWithError(client, "gateway timeout");
	} else {
		ServerKit::HeaderTable headers = getHeadersWithContentType(*req);
		headers.insert((*req)->pool, "cache-control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(*client, 504, &headers, getFormattedMessage(*req, "Gateway Timeout"));
		endRequest(client, req);
	}
}

void
Controller::writeBenchmarkResponse(Client **client, Request **req, bool end) {
	if (canKeepAlive(*req)) {
		writeResponse(*client, P_STATIC_STRING(
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Date: Wed, 15 Nov 1995 06:25:24 GMT\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: 3\r\n"
			"Connection: keep-alive\r\n"
			"\r\n"
			"ok\n"));
	} else {
		writeResponse(*client, P_STATIC_STRING(
			"HTTP/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Date: Wed, 15 Nov 1995 06:25:24 GMT\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: 3\r\n"
			"Connection: close\r\n"
			"\r\n"
			"ok\n"));
	}
	if (end && !(*req)->ended()) {
		endRequest(client, req);
	}
}

bool
Controller::getBoolOption(Request *req, const HashedStaticString &name,
	bool defaultValue)
{
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		return psg_lstr_first_byte(value) == 't';
	} else {
		return defaultValue;
	}
}

template<typename Number>
Number
Controller::clamp(Number value, Number min, Number max) {
	return std::max(std::min(value, max), min);
}

void
Controller::gatherBuffers(char * restrict dest, unsigned int size,
	const struct iovec *buffers, unsigned int nbuffers)
{
	const char *end = dest + size;
	char *pos = dest;

	for (unsigned int i = 0; i < nbuffers; i++) {
		assert(pos + buffers[i].iov_len <= end);
		memcpy(pos, buffers[i].iov_base, buffers[i].iov_len);
		pos += buffers[i].iov_len;
	}
}

// `path` MUST be NULL-terminated. Returns a contiguous LString.
LString *
Controller::resolveSymlink(const StaticString &path, psg_pool_t *pool) {
	char linkbuf[PATH_MAX + 1];
	ssize_t size;

	size = readlink(path.data(), linkbuf, PATH_MAX);
	if (size == -1) {
		if (errno == EINVAL) {
			return psg_lstr_create(pool, path);
		} else {
			int e = errno;
			string message = "Cannot resolve possible symlink '";
			message.append(path.data(), path.size());
			message.append("'");
			throw FileSystemException(message, e, path.data());
		}
	} else {
		linkbuf[size] = '\0';
		if (linkbuf[0] == '\0') {
			string message = "The file '";
			message.append(path.data(), path.size());
			message.append("' is a symlink, and it refers to an empty filename. This is not allowed.");
			throw FileSystemException(message, ENOENT, path.data());
		} else if (linkbuf[0] == '/') {
			// Symlink points to an absolute path.
			size_t len = strlen(linkbuf);
			char *data = (char *) psg_pnalloc(pool, len + 1);
			memcpy(data, linkbuf, len);
			data[len] = '\0';
			return psg_lstr_create(pool, data, len);
		} else {
			// Symlink points to a relative path.
			// We do not use absolutizePath() because it's too slow.
			// This version doesn't handle all the edge cases but is
			// much faster.
			StaticString workingDir = extractDirNameStatic(path);
			size_t linkbuflen = strlen(linkbuf);
			size_t resultlen = linkbuflen + 1 + workingDir.size();
			char *data = (char *) psg_pnalloc(pool, resultlen);
			char *pos  = data;
			char *end  = data + resultlen;

			pos = appendData(pos, end, workingDir);
			*pos = '/';
			pos++;
			pos = appendData(pos, end, linkbuf, linkbuflen);

			return psg_lstr_create(pool, data, resultlen);
		}
	}
}

void
Controller::parseCookieHeader(psg_pool_t *pool, const LString *headerValue,
	vector< pair<StaticString, StaticString> > &cookies) const
{
	// See http://stackoverflow.com/questions/6108207/definite-guide-to-valid-cookie-values
	// for syntax grammar.
	vector<StaticString> parts;
	vector<StaticString>::const_iterator it, it_end;

	assert(headerValue->size > 0);
	headerValue = psg_lstr_make_contiguous(headerValue, pool);
	split(StaticString(headerValue->start->data, headerValue->size),
		';', parts);
	cookies.reserve(parts.size());
	it_end = parts.end();

	for (it = parts.begin(); it != it_end; it++) {
		const char *begin = it->data();
		const char *end = it->data() + it->size();
		const char *sep;

		skipLeadingWhitespaces(&begin, end);
		skipTrailingWhitespaces(begin, &end);

		// Find the separator ('=').
		sep = (const char *) memchr(begin, '=', end - begin);
		if (sep != NULL) {
			// Valid cookie. Otherwise, ignore it.
			const char *nameEnd = sep;
			const char *valueBegin = sep + 1;

			skipTrailingWhitespaces(begin, &nameEnd);
			skipLeadingWhitespaces(&valueBegin, end);

			cookies.push_back(make_pair(
				StaticString(begin, nameEnd - begin),
				StaticString(valueBegin, end - valueBegin)
			));
		}
	}
}

#ifdef DEBUG_CC_EVENT_LOOP_BLOCKING
	void
	Controller::reportLargeTimeDiff(Client *client, const char *name,
		ev_tstamp fromTime, ev_tstamp toTime)
	{
		if (fromTime != 0 && toTime != 0) {
			ev_tstamp blockTime = toTime - fromTime;
			if (blockTime > 0.01) {
				char buf[1024];
				int size = snprintf(buf, sizeof(buf), "%s: %.1f msec",
					name, blockTime * 1000);
				if (client != NULL) {
					SKC_NOTICE(client, StaticString(buf, size));
				} else {
					SKS_NOTICE(StaticString(buf, size));
				}
			}
		}
	}
#endif


} // namespace Core
} // namespace Passenger
