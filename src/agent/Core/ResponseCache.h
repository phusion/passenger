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
#ifndef _PASSENGER_RESPONSE_CACHE_H_
#define _PASSENGER_RESPONSE_CACHE_H_

#include <boost/cstdint.hpp>
#include <time.h>
#include <cassert>
#include <cstring>
#include <DataStructures/HashedStaticString.h>
#include <ServerKit/http_parser.h>
#include <ServerKit/CookieUtils.h>
#include <StaticString.h>
#include <StrIntTools/DateParsing.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {

/**
 * Relevant RFCs:
 * https://tools.ietf.org/html/rfc7234    HTTP 1.1 Caching
 * https://tools.ietf.org/html/rfc2109    HTTP State Management Mechanism
 */
template<typename Request>
class ResponseCache {
public:
	static const unsigned int MAX_ENTRIES     = 8; // Fits in exactly 2 cache lines
	static const unsigned int MAX_KEY_LENGTH  = 256;
	static const unsigned int MAX_HEADER_SIZE = 4096;
	static const unsigned int MAX_BODY_SIZE   = 1024 * 32;
	static const unsigned int DEFAULT_HEURISTIC_FRESHNESS = 10;
	static const unsigned int MIN_HEURISTIC_FRESHNESS = 1;

	struct Header {
		bool valid;
		unsigned short keySize;
		boost::uint32_t hash;
		time_t date;

		Header()
			: valid(false),
			  keySize(0),
			  hash(0),
			  date(0)
			{ }
	};

	struct Body {
		unsigned short httpHeaderSize;
		unsigned short httpBodySize;
		time_t expiryDate;
		char key[MAX_KEY_LENGTH];
		char httpHeaderData[MAX_HEADER_SIZE];
		// This data is dechunked.
		char httpBodyData[MAX_BODY_SIZE];

		Body()
			: httpHeaderSize(0),
			  httpBodySize(0),
			  expiryDate(0)
		{
			key[0] = httpHeaderData[0] = httpBodyData[0] = '\0';
		}
	};

	struct Entry {
		unsigned int index;
		Header *header;
		Body *body;
		enum {
			NOT_FOUND,
			NOT_FRESH
		} cacheMissReason;

		Entry()
			: index(0),
			  header(NULL),
			  body(NULL)
			{ }

		Entry(unsigned int i, Header *h, Body *b)
			: index(i),
			  header(h),
			  body(b)
			{ }

		OXT_FORCE_INLINE
		bool valid() const {
			return header != NULL;
		}

		const char *getCacheMissReasonString() const {
			switch (cacheMissReason) {
			case NOT_FOUND:
				return "NOT_FOUND";
			case NOT_FRESH:
				return "NOT_FRESH";
			default:
				return "UNKNOWN";
			}
		}
	};

private:
	HashedStaticString HOST;
	HashedStaticString CACHE_CONTROL;
	HashedStaticString PRAGMA_CONST;
	HashedStaticString AUTHORIZATION;
	HashedStaticString VARY;
	HashedStaticString WWW_AUTHENTICATE;
	HashedStaticString X_SENDFILE;
	HashedStaticString X_ACCEL_REDIRECT;
	HashedStaticString EXPIRES;
	HashedStaticString LAST_MODIFIED;
	HashedStaticString LOCATION;
	HashedStaticString CONTENT_LOCATION;
	HashedStaticString COOKIE;
	HashedStaticString PASSENGER_VARY_TURBOCACHE_BY_COOKIE;

	unsigned int fetches, hits, stores, storeSuccesses;

	Header headers[MAX_ENTRIES];
	Body bodies[MAX_ENTRIES];

	unsigned int calculateKeyLength(const LString * restrict host,
		const LString * restrict varyCookie,
		const StaticString &path)
	{
		unsigned int size =
			1  // protocol flag
			+ ((host != NULL) ? host->size : 0)
			+ 1  // '\n'
			+ path.size()
			+ ((varyCookie != NULL) ? (varyCookie->size + 1) : 0);
		if (size > MAX_KEY_LENGTH) {
			return 0;
		} else {
			return size;
		}
	}

	void generateKey(bool https, const StaticString &path,
		const LString * restrict host,
		const LString * restrict varyCookie,
		char * restrict output,
		unsigned int size)
	{
		char *pos = output;
		const char *end = output + size;
		const LString::Part *part;

		if (https) {
			pos = appendData(pos, end, "S", 1);
		} else {
			pos = appendData(pos, end, "H", 1);
		}

		if (host != NULL) {
			part = host->start;
			while (part != NULL) {
				pos = appendData(pos, end, part->data, part->size);
				part = part->next;
			}
		}

		pos = appendData(pos, end, "\n", 1);
		pos = appendData(pos, end, path);

		if (varyCookie != NULL) {
			pos = appendData(pos, end, "\n", 1);
			part = varyCookie->start;
			while (part != NULL) {
				pos = appendData(pos, end, part->data, part->size);
				part = part->next;
			}
		}
	}

	bool statusCodeIsCacheableByDefault(unsigned int code) const {
		if (code / 100 == 2) {
			return code == 200 || code == 203 || code == 204;
		} else {
			switch (code / 100) {
			case 3:
				return code == 300 || code == 301;
			case 4:
				return code == 404 || code == 405 || code == 410 || code == 414;
			case 5:
				return code == 501;
			default:
				return false;
			}
		}
	}

	Entry lookup(const HashedStaticString &cacheKey) {
		for (unsigned int i = 0; i < MAX_ENTRIES; i++) {
			if (headers[i].valid
			 && headers[i].hash == cacheKey.hash()
			 && cacheKey == StaticString(bodies[i].key, headers[i].keySize))
			{
				return Entry(i, &headers[i], &bodies[i]);
			}
		}
		return Entry();
	}

	Entry lookupInvalidOrOldest() {
		int oldest = -1;

		for (unsigned int i = 0; i < MAX_ENTRIES; i++) {
			if (!headers[i].valid) {
				return Entry(i, &headers[i], &bodies[i]);
			} else if (oldest == -1 || headers[i].date < headers[oldest].date) {
				oldest = i;
			}
		}

		return Entry(oldest, &headers[oldest], &bodies[oldest]);
	}

	OXT_FORCE_INLINE
	void erase(unsigned int index) {
		headers[index].valid = false;
	}

	time_t parseDate(psg_pool_t *pool, const LString *date, ev_tstamp now) const {
		if (date == NULL || date->size == 0) {
			return (time_t) now;
		}

		struct tm tm;
		int zone;

		// Try to parse it as an IMF-fixdate.
		// We don't support any other formats. It's too much hassle.
		date = psg_lstr_make_contiguous(date, pool);
		if (parseImfFixdate(date->start->data, date->start->data + date->size, tm, zone)) {
			return parsedDateToTimestamp(tm, zone);
		} else {
			return (time_t) -1;
		}
	}

	time_t determineExpiryDate(const Request *req, time_t responseDate, ev_tstamp now) const {
		const LString *value = req->appResponse.expiresHeader;
		if (value != NULL) {
			struct tm tm;
			int zone;

			if (parseImfFixdate(value->start->data, value->start->data + value->size, tm, zone)) {
				return parsedDateToTimestamp(tm, zone);
			} else {
				return (time_t) -1;
			}
		}

		value = req->appResponse.cacheControl;
		if (value != NULL) {
			StaticString cacheControl(value->start->data, value->size);
			string::size_type pos = cacheControl.find(P_STATIC_STRING("max-age"));
			if (pos != string::npos && cacheControl.size() > pos + 1) {
				unsigned int maxAge = stringToUint(cacheControl.substr(
					pos + (sizeof("max-age") - 1) + 1));
				if (maxAge == 0) {
					// Parse error or max-age=0
					return (time_t) - 1;
				} else {
					return (time_t) now + maxAge;
				}
			}
		}

		value = req->appResponse.lastModifiedHeader;
		if (value != NULL) {
			struct tm tm;
			int zone;

			if (parseImfFixdate(value->start->data, value->start->data + value->size, tm, zone)) {
				time_t lastModified = parsedDateToTimestamp(tm, zone);
				if (lastModified < now) {
					time_t diff = (time_t) now - lastModified;
					return time_t(now + std::max<double>(diff * 0.1, MIN_HEURISTIC_FRESHNESS));
				}
			} else {
				return (time_t) now + 1;
			}
		}

		return now + DEFAULT_HEURISTIC_FRESHNESS;
	}

	bool isFresh(const Entry &entry, ev_tstamp now) const {
		return entry.body->expiryDate > now;
	}

	StaticString extractHostNameWithPortFromParsedUrl(struct http_parser_url &url,
		const LString *value) const
	{
		assert(url.field_set & (1 << UF_HOST));
		if (url.field_set & (1 << UF_PORT)) {
			unsigned int portEnd = url.field_data[UF_PORT].off + url.field_data[UF_PORT].len;
			return StaticString(value->start->data + url.field_data[UF_HOST].off,
				portEnd - url.field_data[UF_HOST].off);
		} else {
			return StaticString(value->start->data + url.field_data[UF_HOST].off,
				url.field_data[UF_HOST].len);
		}
	}

	void invalidateLocation(Request *req, const HashedStaticString &header) {
		const LString *value = req->appResponse.headers.lookup(header);
		if (value == NULL || value->size == 0) {
			return;
		}

		StaticString path;
		bool https;
		value = psg_lstr_make_contiguous(value, req->pool);

		if (psg_lstr_first_byte(value) != '/') {
			// Maybe it is a full URL. Parse the host name.
			struct http_parser_url url;
			int ret = http_parser_parse_url(value->start->data, value->size,
				0, &url);
			if (ret != 0) {
				// Invalid URL.
				return;
			}
			if (!(url.field_set & (1 << UF_HOST))) {
				// Invalid URL.
				return;
			}

			StaticString host = extractHostNameWithPortFromParsedUrl(url, value);
			if (host.size() != req->host->size) {
				// The host names don't match.
				return;
			}

			char *lowercaseHost = (char *) psg_pnalloc(req->pool, host.size());
			convertLowerCase((const unsigned char *) host.data(),
				(unsigned char *) lowercaseHost, host.size());
			host = StaticString(lowercaseHost, host.size());

			char *lowercaseReqHost = (char *) psg_pnalloc(req->pool, req->host->size);
			convertLowerCase((const unsigned char *) req->host->start->data,
				(unsigned char *) lowercaseReqHost, req->host->size);

			if (memcmp(host.data(), lowercaseReqHost, req->host->size) != 0) {
				// The host names don't match.
				return;
			}

			if (url.field_set & (1 << UF_PATH)) {
				path = StaticString(value->start->data + url.field_data[UF_PATH].off,
					url.field_data[UF_PATH].len);
			} else {
				path = P_STATIC_STRING("/");
			}

			if (url.field_set & (1 << UF_SCHEMA)) {
				StaticString schema(value->start->data + url.field_data[UF_SCHEMA].off,
					url.field_data[UF_SCHEMA].len);
				https = schema == "https";
			} else {
				https = req->https;
			}
		} else {
			path = StaticString(value->start->data, value->size);
			https = req->https;
		}

		unsigned int keySize = calculateKeyLength(req->host, req->varyCookie, path);
		if (keySize == 0) {
			return;
		}

		char *key = (char *) psg_pnalloc(req->pool, keySize);
		generateKey(https, path, req->host, req->varyCookie, key, keySize);

		Entry entry(lookup(StaticString(key, keySize)));
		if (entry.valid()) {
			entry.header->valid = false;
		}
	}

public:
	ResponseCache()
		: CACHE_CONTROL("cache-control"),
		  PRAGMA_CONST("pragma"),
		  AUTHORIZATION("authorization"),
		  VARY("vary"),
		  WWW_AUTHENTICATE("www-authenticate"),
		  X_SENDFILE("x-sendfile"),
		  X_ACCEL_REDIRECT("x-accel-redirect"),
		  EXPIRES("expires"),
		  LAST_MODIFIED("last-modified"),
		  LOCATION("location"),
		  CONTENT_LOCATION("content-location"),
		  COOKIE("cookie"),
		  PASSENGER_VARY_TURBOCACHE_BY_COOKIE("!~PASSENGER_VARY_TURBOCACHE_COOKIE"),
		  fetches(0),
		  hits(0),
		  stores(0),
		  storeSuccesses(0)
		{ }

	OXT_FORCE_INLINE
	unsigned int getFetches() const {
		return fetches;
	}

	OXT_FORCE_INLINE
	unsigned int getHits() const {
		return hits;
	}

	OXT_FORCE_INLINE
	double getHitRatio() const {
		return hits / (double) fetches;
	}

	OXT_FORCE_INLINE
	unsigned int getStores() const {
		return fetches;
	}

	OXT_FORCE_INLINE
	unsigned int getStoreSuccesses() const {
		return storeSuccesses;
	}

	OXT_FORCE_INLINE
	double getStoreSuccessRatio() const {
		return storeSuccesses / (double) stores;
	}

	// For decreasing the store success ratio without calling store().
	OXT_FORCE_INLINE
	void incStores() {
		stores++;
	}

	void resetStatistics() {
		fetches = 0;
		hits = 0;
		stores = 0;
		storeSuccesses = 0;
	}

	void clear() {
		for (unsigned int i = 0; i < MAX_ENTRIES; i++) {
			headers[i].valid = false;
		}
	}


	/**
	 * Prepares the request for caching operations (fetching and storing).
	 * Returns whether caching operations are available for this request.
	 *
	 * @post result == !req->cacheKey.empty()
	 */
	template<typename Controller>
	bool prepareRequest(Controller *controller, Request *req) {
		if (req->upgraded() || req->host == NULL) {
			return false;
		}

		LString *varyCookieName = req->secureHeaders.lookup(PASSENGER_VARY_TURBOCACHE_BY_COOKIE);
		if (varyCookieName == NULL && !req->config->defaultVaryTurbocacheByCookie.empty()) {
			varyCookieName = (LString *) psg_palloc(req->pool, sizeof(LString));
			psg_lstr_init(varyCookieName);
			psg_lstr_append(varyCookieName, req->pool,
				req->config->defaultVaryTurbocacheByCookie.data(),
				req->config->defaultVaryTurbocacheByCookie.size());
		}
		if (varyCookieName != NULL) {
			LString *cookieHeader = req->headers.lookup(COOKIE);
			if (cookieHeader != NULL) {
				req->varyCookie = ServerKit::findCookie(req->pool, cookieHeader, varyCookieName);
			}
		}

		unsigned int size = calculateKeyLength(req->host,
			req->varyCookie,
			StaticString(req->path.start->data, req->path.size));
		if (size == 0) {
			req->cacheKey = HashedStaticString();
			return false;
		}

		req->cacheControl = req->headers.lookup(CACHE_CONTROL);
		if (req->cacheControl == NULL) {
			// hasPragmaHeader is only used by requestAllowsFetching(),
			// so if there is no Cache-Control header then it's not
			// necessary to check for the Pragma header.
			req->hasPragmaHeader = req->headers.lookup(PRAGMA_CONST) != NULL;
		}

		char *key = (char *) psg_pnalloc(req->pool, size);
		generateKey(req->https, StaticString(req->path.start->data, req->path.size),
			req->host, req->varyCookie, key, size);
		req->cacheKey = HashedStaticString(key, size);
		return true;
	}


	// @pre prepareRequest() returned true
	bool requestAllowsFetching(Request *req) const {
		return (req->method == HTTP_GET || req->method == HTTP_HEAD)
			&& req->cacheControl == NULL
			&& !req->hasPragmaHeader;
	}

	// @pre requestAllowsFetching()
	Entry fetch(Request *req, ev_tstamp now) {
		fetches++;
		if (OXT_UNLIKELY(fetches == 0)) {
			// Value rolled over
			fetches = 1;
			hits = 0;
		}

		Entry entry(lookup(req->cacheKey));
		if (entry.valid()) {
			hits++;
			if (isFresh(entry, now)) {
				return entry;
			} else {
				erase(entry.index);
				Entry result;
				result.cacheMissReason = Entry::NOT_FRESH;
				return result;
			}
		} else {
			entry.cacheMissReason = Entry::NOT_FOUND;
			return entry;
		}
	}


	// @pre prepareRequest() returned true
	OXT_FORCE_INLINE
	bool requestAllowsStoring(Request *req) const {
		return req->method != HTTP_HEAD && requestAllowsFetching(req);
	}

	// @pre prepareRequest() returned true
	bool prepareRequestForStoring(Request *req) {
		if (!statusCodeIsCacheableByDefault(req->appResponse.statusCode)) {
			return false;
		}

		ServerKit::HeaderTable &respHeaders = req->appResponse.headers;

		req->appResponse.cacheControl = respHeaders.lookup(CACHE_CONTROL);
		if (req->appResponse.cacheControl != NULL && req->appResponse.cacheControl->size > 0) {
			req->appResponse.cacheControl = psg_lstr_make_contiguous(
				req->appResponse.cacheControl,
				req->pool);
			StaticString cacheControl = StaticString(
				req->appResponse.cacheControl->start->data,
				req->appResponse.cacheControl->size);
			if (cacheControl.find(P_STATIC_STRING("no-store")) != string::npos
			 || cacheControl.find(P_STATIC_STRING("private")) != string::npos
			 || cacheControl.find(P_STATIC_STRING("no-cache")) != string::npos)
			{
				return false;
			}
		}

		if (req->headers.lookup(AUTHORIZATION) != NULL
		 || respHeaders.lookup(VARY) != NULL
		 || respHeaders.lookup(WWW_AUTHENTICATE) != NULL
		 || respHeaders.lookup(X_SENDFILE) != NULL
		 || respHeaders.lookup(X_ACCEL_REDIRECT) != NULL)
		{
			return false;
		}

		req->appResponse.expiresHeader = respHeaders.lookup(EXPIRES);
		if (req->appResponse.expiresHeader == NULL) {
			// lastModifiedHeader is only used in determineExpiryDate(),
			// and only if expiresHeader is not present, and Cache-Control
			// does not contain max-age.
			req->appResponse.lastModifiedHeader =
				respHeaders.lookup(LAST_MODIFIED);
			if (req->appResponse.lastModifiedHeader != NULL) {
				req->appResponse.lastModifiedHeader =
					psg_lstr_make_contiguous(req->appResponse.lastModifiedHeader,
						req->pool);
			}
		} else {
			req->appResponse.expiresHeader =
				psg_lstr_make_contiguous(req->appResponse.expiresHeader,
					req->pool);
		}

		return req->appResponse.cacheControl != NULL
			|| req->appResponse.expiresHeader != NULL;
	}

	// @pre requestAllowsStoring()
	// @pre prepareRequestForStoring()
	Entry store(Request *req, ev_tstamp now, unsigned int headerSize, unsigned int bodySize) {
		stores++;

		if (headerSize > MAX_HEADER_SIZE || bodySize > MAX_BODY_SIZE) {
			return Entry();
		}

		time_t responseDate = parseDate(req->pool, req->appResponse.date, now);
		if (responseDate == (time_t) -1) {
			return Entry();
		}

		time_t expiryDate = determineExpiryDate(req, responseDate, now);
		if (expiryDate == (time_t) -1) {
			return Entry();
		}

		const HashedStaticString &cacheKey = req->cacheKey;
		Entry entry(lookup(cacheKey));
		if (!entry.valid()) {
			entry = lookupInvalidOrOldest();
			entry.header->valid   = true;
			entry.header->hash    = cacheKey.hash();
			entry.header->keySize = cacheKey.size();
			memcpy(entry.body->key, cacheKey.data(), cacheKey.size());
		}
		entry.header->date     = responseDate;
		entry.body->expiryDate = expiryDate;
		entry.body->httpHeaderSize = headerSize;
		entry.body->httpBodySize   = bodySize;
		storeSuccesses++;
		return entry;
	}


	// @pre prepareRequest() returned true
	// @pre !requestAllowsStoring() || !prepareRequestForStoring()
	bool requestAllowsInvalidating(Request *req) const {
		return req->method != HTTP_GET;
	}

	// @pre requestAllowsInvalidating()
	void invalidate(Request *req) {
		Entry entry(lookup(req->cacheKey));
		if (entry.valid()) {
			entry.header->valid = false;
		}

		invalidateLocation(req, LOCATION);
		invalidateLocation(req, CONTENT_LOCATION);
	}


	string inspect() const {
		stringstream stream;
		for (unsigned int i = 0; i < MAX_ENTRIES; i++) {
			time_t expiryDate = bodies[i].expiryDate;
			stream << " #" << i << ": valid=" << headers[i].valid
				<< ", hash=" << headers[i].hash
				<< ", expiryDate=" << expiryDate
				<< ", keySize=" << headers[i].keySize << ", key=\""
				<< cEscapeString(StaticString(bodies[i].key, headers[i].keySize)) << "\"\n";
		}
		return stream.str();
	}
};


} // namespace Passenger

#endif /* _PASSENGER_RESPONSE_CACHE_H_ */
