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
#ifndef _PASSENGER_TURBO_CACHING_H_
#define _PASSENGER_TURBO_CACHING_H_

#include <oxt/backtrace.hpp>
#include <ev++.h>
#include <ctime>
#include <cstddef>
#include <cassert>
#include <MemoryKit/mbuf.h>
#include <ServerKit/Context.h>
#include <Constants.h>
#include <LoggingKit/LoggingKit.h>
#include <StrIntTools/StrIntUtils.h>
#include <Core/ResponseCache.h>

namespace Passenger {
namespace Core {


using namespace std;


template<typename Request>
class TurboCaching {
public:
	/** The interval of the timer while we're in the ENABLED state. */
	static const unsigned int ENABLED_TIMEOUT = 2;
	/** The interval of the timer while we're in the TEMPORARILY_DISABLED state. */
	static const unsigned int TEMPORARY_DISABLE_TIMEOUT = 10;
	/** Only consider temporarily disabling turbocaching if the number of
	 * fetches/stores in the current interval have reached these thresholds.
	 */
	static const unsigned int FETCH_THRESHOLD = 20;
	static const unsigned int STORE_THRESHOLD = 20;

	OXT_FORCE_INLINE static double MIN_HIT_RATIO() { return 0.5; }
	OXT_FORCE_INLINE static double MIN_STORE_SUCCESS_RATIO() { return 0.5; }

	enum State {
		/**
		 * Turbocaching is permanently disabled.
		 */
		DISABLED,
		/**
		 * Turbocaching is enabled.
		 */
		ENABLED,
		/**
		 * In case turbocaching is enabled, and poor cache hit ratio
		 * is detected, this state will be entered. It will stay
		 * in this state for TEMPORARY_DISABLE_TIMEOUT seconds before
		 * transitioning back to ENABLED.
		 */
		TEMPORARILY_DISABLED
	};

	typedef ResponseCache<Request> ResponseCacheType;
	typedef typename ResponseCache<Request>::Entry ResponseCacheEntryType;

private:
	State state;
	ev_tstamp lastTimeout, nextTimeout;

	struct ResponsePreparation {
		Request *req;
		const ResponseCacheEntryType *entry;

		time_t now;
		time_t age;
		unsigned int ageValueSize;
		unsigned int contentLengthStrSize;
		bool showVersionInHeader;
	};

	template<typename Server>
	void prepareResponseHeader(ResponsePreparation &prep, Server *server,
		Request *req, const ResponseCacheEntryType &entry)
	{
		prep.req   = req;
		prep.entry = &entry;
		prep.now   = (time_t) ev_now(server->getLoop());

		if (prep.now >= entry.header->date) {
			prep.age = prep.now - entry.header->date;
		} else {
			prep.age = 0;
		}

		prep.ageValueSize = integerSizeInOtherBase<time_t, 10>(prep.age);
		prep.contentLengthStrSize = uintSizeAsString(entry.body->httpBodySize);
		prep.showVersionInHeader = req->config->showVersionInHeader;
	}

	template<typename Server>
	unsigned int buildResponseHeader(const ResponsePreparation &prep, Server *server,
		char *output, unsigned int outputSize)
	{
		#define PUSH_STATIC_STRING(str) \
			do { \
				result += sizeof(str) - 1; \
				if (output != NULL) { \
					pos = appendData(pos, end, str, sizeof(str) - 1); \
				} \
			} while (false)

		const ResponseCacheEntryType *entry = prep.entry;
		Request *req = prep.req;

		unsigned int httpVersion = req->httpMajor * 1000 + req->httpMinor * 10;
		unsigned int result = 0;
		char *pos = output;
		const char *end = output + outputSize;

		result += entry->body->httpHeaderSize;
		if (output != NULL) {
			pos = appendData(pos, end, entry->body->httpHeaderData,
				entry->body->httpHeaderSize);
		}

		PUSH_STATIC_STRING("Content-Length: ");
		result += prep.contentLengthStrSize;
		if (output != NULL) {
			uintToString(entry->body->httpBodySize, pos, end - pos);
			pos += prep.contentLengthStrSize;
		}
		PUSH_STATIC_STRING("\r\n");

		PUSH_STATIC_STRING("Age: ");
		result += prep.ageValueSize;
		if (output != NULL) {
			integerToOtherBase<time_t, 10>(prep.age, pos, end - pos);
			pos += prep.ageValueSize;
		}
		PUSH_STATIC_STRING("\r\n");

		if (prep.showVersionInHeader) {
			PUSH_STATIC_STRING("X-Powered-By: " PROGRAM_NAME " " PASSENGER_VERSION "\r\n");
		} else {
			PUSH_STATIC_STRING("X-Powered-By: " PROGRAM_NAME "\r\n");
		}

		if (server->canKeepAlive(req)) {
			if (httpVersion < 1010) {
				// HTTP < 1.1 defaults to "Connection: close", but we want keep-alive
				PUSH_STATIC_STRING("Connection: keep-alive\r\n");
			}
		} else {
			if (httpVersion >= 1010) {
				// HTTP 1.1 defaults to "Connection: keep-alive", but we don't want it
				PUSH_STATIC_STRING("Connection: close\r\n");
			}
		}

		PUSH_STATIC_STRING("\r\n");

		#ifndef NDEBUG
			if (output != NULL) {
				assert(size_t(pos - output) == size_t(result));
				assert(size_t(pos - output) <= size_t(outputSize));
			}
		#endif
		return result;

		#undef PUSH_STATIC_STRING
	}

public:
	ResponseCache<Request> responseCache;

	TurboCaching()
		: state(ENABLED),
		  lastTimeout(0),
		  nextTimeout(0)
		{ }

	void initialize(bool initiallyEnabled) {
		state = initiallyEnabled ? ENABLED : DISABLED;
		lastTimeout = (ev_tstamp) time(NULL);
		nextTimeout = (ev_tstamp) time(NULL) + ENABLED_TIMEOUT;
	}

	bool isEnabled() const {
		return state == ENABLED;
	}

	// Call when the event loop multiplexer returns.
	void updateState(ev_tstamp now) {
		if (OXT_UNLIKELY(state == DISABLED)) {
			return;
		}

		if (OXT_LIKELY(now < nextTimeout)) {
			return;
		}

		switch (state) {
		case ENABLED:
			if (responseCache.getFetches() >= FETCH_THRESHOLD
				&& responseCache.getHitRatio() < MIN_HIT_RATIO())
			{
				P_INFO("Poor turbocaching hit ratio detected (" <<
					responseCache.getHits() << " hits, " <<
					responseCache.getFetches() << " fetches, " <<
					(int) (responseCache.getHitRatio() * 100) <<
					"%). Temporarily disabling turbocaching "
					"for " << TEMPORARY_DISABLE_TIMEOUT << " seconds");
				state = TEMPORARILY_DISABLED;
				nextTimeout = now + TEMPORARY_DISABLE_TIMEOUT;
			} else if (responseCache.getStores() >= STORE_THRESHOLD
				&& responseCache.getStoreSuccessRatio() < MIN_STORE_SUCCESS_RATIO())
			{
				P_INFO("Poor turbocaching store success ratio detected (" <<
					responseCache.getStoreSuccesses() << " store successes, " <<
					responseCache.getStores() << " stores, " <<
					(int) (responseCache.getStoreSuccessRatio() * 100) <<
					"%). Temporarily disabling turbocaching "
					"for " << TEMPORARY_DISABLE_TIMEOUT << " seconds");
				state = TEMPORARILY_DISABLED;
				nextTimeout = now + TEMPORARY_DISABLE_TIMEOUT;
			} else {
				P_DEBUG("Clearing turbocache");
				nextTimeout = now + ENABLED_TIMEOUT;
			}
			responseCache.resetStatistics();
			responseCache.clear();
			break;
		case TEMPORARILY_DISABLED:
			P_INFO("Re-enabling turbocaching");
			state = ENABLED;
			nextTimeout = now + ENABLED_TIMEOUT;
			break;
		default:
			P_BUG("Unknown state " << (int) state);
			break;
		}

		lastTimeout = now;
	}

	template<typename Server, typename Client>
	void writeResponse(Server *server, Client *client, Request *req, ResponseCacheEntryType &entry) {
		MemoryKit::mbuf_pool &mbuf_pool = server->getContext()->mbuf_pool;
		const unsigned int MBUF_MAX_SIZE = mbuf_pool_data_size(&mbuf_pool);
		ResponsePreparation prep;
		unsigned int headerSize;

		prepareResponseHeader(prep, server, req, entry);
		headerSize = buildResponseHeader(prep, server, NULL, 0);

		if (headerSize + entry.body->httpBodySize <= MBUF_MAX_SIZE) {
			// Header and body fit inside a single mbuf
			MemoryKit::mbuf buffer(MemoryKit::mbuf_get(&mbuf_pool));
			buffer = MemoryKit::mbuf(buffer, 0, headerSize + entry.body->httpBodySize);

			buildResponseHeader(prep, server, buffer.start, buffer.size());
			memcpy(buffer.start + headerSize, entry.body->httpBodyData, entry.body->httpBodySize);

			server->writeResponse(client, buffer);
		} else {
			char *buffer = (char *) psg_pnalloc(req->pool, headerSize + entry.body->httpBodySize);
			buildResponseHeader(prep, server, buffer,
				headerSize + entry.body->httpBodySize);
			memcpy(buffer + headerSize, entry.body->httpBodyData, entry.body->httpBodySize);

			server->writeResponse(client, buffer, headerSize + entry.body->httpBodySize);
		}
	}
};


} // namespace Core
} // namespace Passenger

#endif /* _PASSENGER_TURBO_CACHING_H_ */
