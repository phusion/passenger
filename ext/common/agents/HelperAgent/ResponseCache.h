/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014 Phusion
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
#ifndef _PASSENGER_RESPONSE_CACHE_H_
#define _PASSENGER_RESPONSE_CACHE_H_

#include <boost/cstdint.hpp>
#include <time.h>
#include <cassert>
#include <cstring>
#include <DataStructures/HashedStaticString.h>
#include <StaticString.h>
#include <Utils/DateParsing.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {


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
			: valid(false)
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
	};

	struct Entry {
		unsigned int index;
		Header *header;
		Body *body;

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
	};

private:
	HashedStaticString HOST;
	HashedStaticString CACHE_CONTROL;
	HashedStaticString PRAGMA_CONST;
	HashedStaticString VARY;
	HashedStaticString EXPIRES;
	HashedStaticString LAST_MODIFIED;

	unsigned int fetches, hits, stores, storeSuccesses;

	Header headers[MAX_ENTRIES];
	Body bodies[MAX_ENTRIES];

	unsigned int calculateKeyLength(Request *req, const LString *host) {
		unsigned int size =
			1  // protocol flag
			+ ((host != NULL) ? host->size : 0)
			+ 1  // ':'
			+ req->path.size;
		if (size > MAX_KEY_LENGTH) {
			return 0;
		} else {
			return size;
		}
	}

	void generateKey(Request *req, const LString *host, char *output, unsigned int size) {
		char *pos = output;
		const char *end = output + size;
		const LString::Part *part;

		if (req->https) {
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

		pos = appendData(pos, end, ":", 1);
		pos = appendData(pos, end, req->path.start->data, req->path.size);
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

	time_t parsedDateToTimestamp(struct tm &tm, int zone) const {
		return mktime(&tm) - zone / 100 * 60 * 60 - zone % 100 * 60;
	}

	time_t parseDate(psg_pool_t *pool, const LString *date, ev_tstamp now) const {
		if (date == NULL) {
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
			if (pos != string::npos) {
				unsigned int maxAge = stringToUint(cacheControl.substr(
					pos + sizeof("max-age") - 1));
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

public:
	ResponseCache()
		: HOST("host"),
		  CACHE_CONTROL("cache-control"),
		  PRAGMA_CONST("pragma"),
		  VARY("vary"),
		  EXPIRES("expires"),
		  LAST_MODIFIED("last-modified"),
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
	bool prepareRequest(Request *req) {
		if (req->upgraded()) {
			return false;
		}

		const LString *host = req->headers.lookup(HOST);
		unsigned int size = calculateKeyLength(req, host);
		if (size == 0) {
			req->cacheKey = HashedStaticString();
			return false;
		}

		req->cacheControl = req->headers.lookup(CACHE_CONTROL);
		if (req->cacheControl != NULL) {
			// hasPragmaHeader is only used by requestAllowsFetching(),
			// so if there is no Cache-Control header then it's not
			// necessary to check for the Pragma header.
			req->hasPragmaHeader = req->headers.lookup(PRAGMA_CONST) != NULL;
		}

		char *key = (char *) psg_pnalloc(req->pool, size);
		generateKey(req, host, key, size);
		req->cacheKey = HashedStaticString(key, size);
		return true;
	}


	// @pre prepareRequest() returned true
	bool requestAllowsFetching(Request *req) const {
		return req->method == HTTP_GET
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
				return Entry();
			}
		} else {
			return entry;
		}
	}


	// @pre prepareRequest() returned true
	OXT_FORCE_INLINE
	bool requestAllowsStoring(Request *req) const {
		return requestAllowsFetching(req);
	}

	// @pre prepareRequest() returned true
	bool prepareRequestForStoring(Request *req) {
		if (!statusCodeIsCacheableByDefault(req->appResponse.statusCode)) {
			return false;
		}

		ServerKit::HeaderTable &respHeaders = req->appResponse.headers;

		if (req->cacheControl != NULL) {
			req->cacheControl = psg_lstr_make_contiguous(req->cacheControl,
				req->pool);
			StaticString cacheControl = StaticString(req->cacheControl->start->data,
				req->cacheControl->size);
			if (cacheControl.find(P_STATIC_STRING("no-store")) != string::npos) {
				return false;
			}
		}

		req->appResponse.cacheControl = respHeaders.lookup(CACHE_CONTROL);
		if (req->appResponse.cacheControl != NULL) {
			req->appResponse.cacheControl = psg_lstr_make_contiguous(
				req->appResponse.cacheControl,
				req->pool);
			StaticString cacheControl = StaticString(
				req->appResponse.cacheControl->start->data,
				req->appResponse.cacheControl->size);
			if (cacheControl.find(P_STATIC_STRING("no-store")) != string::npos) {
				return false;
			}
		}

		if (respHeaders.lookup(VARY) != NULL) {
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

		return true;
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
		return entry;
	}


	// @pre prepareRequest() returned true
	// @pre !requestAllowsStoring() || !prepareRequestForStoring()
	bool requestAllowsInvalidating(Request *req) const {
		return req->method != HTTP_GET;
	}

	// @pre requestAllowsInvalidating()
	void invalidate(Request *req) {
		// TODO: invalidate Location and Content-Location too
		Entry entry(lookup(req->cacheKey));
		if (entry.valid()) {
			entry.header->valid = false;
		}
	}


	string inspect() const {
		stringstream stream;
		for (unsigned int i = 0; i < MAX_ENTRIES; i++) {
			stream << " #" << i << ": valid=" << headers[i].valid
				<< ", hash=" << headers[i].hash << ", keySize="
				<< headers[i].keySize << ", key="
				<< StaticString(bodies[i].key, headers[i].keySize) << "\n";
		}
		return stream.str();
	}
};


} // namespace Passenger

#endif /* _PASSENGER_RESPONSE_CACHE_H_ */
