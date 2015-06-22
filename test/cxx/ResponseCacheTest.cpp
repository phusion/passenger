#include "TestSupport.h"
#include <time.h>
#include <ServerKit/HttpRequest.h>
#include <MemoryKit/palloc.h>
#include <agent/Core/RequestHandler/Request.h>
#include <agent/Core/RequestHandler/AppResponse.h>
#include <agent/Core/ResponseCache.h>

using namespace Passenger;
using namespace Passenger::ServerKit;
using namespace std;

namespace tut {
	typedef ResponseCache<Request> ResponseCacheType;

	struct ResponseCacheTest {
		ResponseCacheType responseCache;
		Request req;
		StaticString defaultVaryTurbocacheByCookie;

		ResponseCacheTest() {
			req.pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
			reset();
		}

		~ResponseCacheTest() {
			psg_destroy_pool(req.pool);
		}

		void reset() {
			req.headers.clear();
			req.secureHeaders.clear();
			req.httpMajor = 1;
			req.httpMinor = 0;
			req.httpState = Request::COMPLETE;
			req.bodyType  = Request::RBT_NO_BODY;
			req.method    = HTTP_GET;
			req.wantKeepAlive = false;
			req.responseBegun = false;
			req.client    = NULL;
			req.hooks.impl     = NULL;
			req.hooks.userData = NULL;
			psg_lstr_init(&req.path);
			psg_lstr_append(&req.path, req.pool, "/");
			req.bodyAlreadyRead = 0;
			req.queryStringIndex = -1;
			req.bodyError = 0;

			req.startedAt = 0;
			req.state     = Request::ANALYZING_REQUEST;
			req.dechunkResponse = false;
			req.requestBodyBuffering = false;
			req.https     = false;
			req.stickySession = false;
			req.halfCloseAppConnection = false;
			req.sessionCheckoutTry = 0;
			req.strip100ContinueHeader = false;
			req.hasPragmaHeader = false;
			req.host = createHostString();
			req.bodyBytesBuffered = 0;
			req.cacheControl = NULL;
			req.varyCookie = NULL;

			req.appResponse.headers.clear();
			req.appResponse.secureHeaders.clear();
			req.appResponse.httpMajor  = 1;
			req.appResponse.httpMinor  = 1;
			req.appResponse.httpState  = AppResponse::COMPLETE;
			req.appResponse.wantKeepAlive = false;
			req.appResponse.oneHundredContinueSent = false;
			req.appResponse.bodyType   = AppResponse::RBT_NO_BODY;
			req.appResponse.statusCode = 200;
			req.appResponse.bodyAlreadyRead = 0;
			req.appResponse.date       = NULL;
			req.appResponse.setCookie  = NULL;
			req.appResponse.cacheControl  = NULL;
			req.appResponse.expiresHeader = NULL;
			req.appResponse.lastModifiedHeader = NULL;
			req.appResponse.headerCacheBuffers = NULL;
			req.appResponse.nHeaderCacheBuffers = 0;
			psg_lstr_init(&req.appResponse.bodyCacheBuffer);

			insertAppResponseHeader(createHeader(
				"date", createTodayString(req.pool)),
				req.pool);
		}

		LString *createHostString() {
			LString *str = (LString *) psg_palloc(req.pool, sizeof(LString));
			psg_lstr_init(str);
			psg_lstr_append(str, req.pool, "foo.com");
			return str;
		}

		StaticString createTodayString(psg_pool_t *pool) {
			time_t the_time = time(NULL);
			struct tm the_tm;
			gmtime_r(&the_time, &the_tm);
			char *buf = (char *) psg_pnalloc(pool, 64);
			size_t size = strftime(buf, 64, "%a, %d %b %Y %H:%M:%S GMT", &the_tm);
			return StaticString(buf, size);
		}

		Header *createHeader(const HashedStaticString &key, const StaticString &val) {
			Header *header = (Header *) psg_palloc(req.pool, sizeof(Header));
			psg_lstr_init(&header->key);
			psg_lstr_init(&header->origKey);
			psg_lstr_init(&header->val);
			psg_lstr_append(&header->key, req.pool, key.data(), key.size());
			psg_lstr_append(&header->origKey, req.pool, key.data(), key.size());
			psg_lstr_append(&header->val, req.pool, val.data(), val.size());
			header->hash = key.hash();
			return header;
		}

		void insertReqHeader(Header *header, psg_pool_t *pool) {
			req.headers.insert(&header, pool);
		}

		void insertAppResponseHeader(Header *header, psg_pool_t *pool) {
			req.appResponse.headers.insert(&header, pool);
		}

		void initCacheableResponse() {
			insertAppResponseHeader(createHeader(
				"cache-control", "public,max-age=99999"),
				req.pool);
		}

		void initUncacheableResponse() {
			insertAppResponseHeader(createHeader(
				"cache-control", "private"),
				req.pool);
		}

		void initResponseBody(const string &body) {
			req.appResponse.bodyType = AppResponse::RBT_CONTENT_LENGTH;
			req.appResponse.aux.bodyInfo.contentLength = body.size();
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(ResponseCacheTest, 100);


	/***** Preparation *****/

	TEST_METHOD(1) {
		set_test_name("It works on a GET request with no body");
		ensure(responseCache.prepareRequest(this, &req));
	}

	TEST_METHOD(2) {
		set_test_name("It fails on upgraded requests");
		req.bodyType = Request::RBT_UPGRADE;
		ensure(!responseCache.prepareRequest(this, &req));
		ensure_equals(req.cacheKey.size(), 0u);
	}

	TEST_METHOD(3) {
		set_test_name("It fails on requests without a host name");
		req.host = NULL;
		ensure(!responseCache.prepareRequest(this, &req));
		ensure_equals(req.cacheKey.size(), 0u);
	}

	TEST_METHOD(4) {
		set_test_name("It fails if the path is too long");
		psg_lstr_append(&req.path, req.pool, "fooooooooooooooooooooooooooo"
			"ooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
			"ooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
			"ooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
			"ooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
			"ooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
			"ooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
			"ooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo");
		ensure(!responseCache.prepareRequest(this, &req));
		ensure_equals(req.cacheKey.size(), 0u);
	}

	TEST_METHOD(7) {
		set_test_name("It generates a cache key on success");
		ensure(responseCache.prepareRequest(this, &req));
		ensure(req.cacheKey.size() > 0);
	}


	/***** Storing and fetching *****/

	TEST_METHOD(10) {
		set_test_name("Storing and fetching works");
		string responseHeadersStr =
			"content-length: 5\r\n"
			"cache-control: public,max-age=99999\r\n";
		string responseBodyStr = "hello";
		initCacheableResponse();
		initResponseBody(responseBodyStr);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", responseCache.prepareRequestForStoring(&req));

		ResponseCacheType::Entry entry(responseCache.store(&req, time(NULL),
			responseHeadersStr.size(), responseBodyStr.size()));
		ensure("(5)", entry.valid());
		ensure_equals("(6)", entry.index, 0u);


		reset();
		ensure("(10)", responseCache.prepareRequest(this, &req));
		ensure("(11)", responseCache.requestAllowsFetching(&req));
		ResponseCacheType::Entry entry2(responseCache.fetch(&req, time(NULL)));
		ensure("(12)", entry2.valid());
		ensure_equals("(13)", entry2.index, 0u);
		ensure_equals<int>("(14)", entry2.body->httpHeaderSize, responseHeadersStr.size());
		ensure_equals<int>("(15)", entry2.body->httpBodySize, responseBodyStr.size());
	}

	TEST_METHOD(11) {
		set_test_name("Fetching fails if there is no entry with the given cache");
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsFetching(&req));
		ResponseCacheType::Entry entry2(responseCache.fetch(&req, time(NULL)));
		ensure("(3)", !entry2.valid());
	}


	/***** Checking whether request should be fetched from cache *****/

	TEST_METHOD(15) {
		set_test_name("It succeeds on GET requests");
		ensure(responseCache.prepareRequest(this, &req));
		ensure(responseCache.requestAllowsFetching(&req));
	}

	TEST_METHOD(16) {
		set_test_name("It succeeds on HEAD requests");
		req.method = HTTP_HEAD;
		ensure(responseCache.prepareRequest(this, &req));
		ensure(responseCache.requestAllowsFetching(&req));
	}

	TEST_METHOD(17) {
		set_test_name("It fails on POST requests");
		req.method = HTTP_POST;
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsFetching(&req));
	}

	TEST_METHOD(18) {
		set_test_name("It fails on non-GET and non-HEAD requests");
		req.method = HTTP_OPTIONS;
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsFetching(&req));
	}

	TEST_METHOD(19) {
		set_test_name("It fails if the request has a Cache-Control header");
		insertReqHeader(createHeader(
			"cache-control", "xyz"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsFetching(&req));
	}

	TEST_METHOD(20) {
		set_test_name("It fails if the request has a Pragma header");
		insertReqHeader(createHeader(
			"pragma", "xyz"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsFetching(&req));
	}


	/***** Checking whether response should be stored to cache *****/

	TEST_METHOD(30) {
		set_test_name("It fails on HEAD requests");
		initCacheableResponse();
		req.method = HTTP_HEAD;
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsStoring(&req));
	}

	TEST_METHOD(31) {
		set_test_name("It fails on all non-GET requests");
		initCacheableResponse();
		req.method = HTTP_POST;
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsStoring(&req));
	}

	TEST_METHOD(32) {
		set_test_name("It fails if the request's Cache-Control header contains no-store");
		initCacheableResponse();
		insertReqHeader(createHeader(
			"cache-control", "no-store"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsStoring(&req));
	}

	TEST_METHOD(33) {
		set_test_name("It fails if the request's Cache-Control header contains no-cache");
		initCacheableResponse();
		insertReqHeader(createHeader(
			"cache-control", "no-cache"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsStoring(&req));
	}

	TEST_METHOD(34) {
		set_test_name("It fails if the request is not default cacheable");
		initCacheableResponse();
		req.appResponse.statusCode = 205;
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(35) {
		set_test_name("It fails if the request is default cacheable, but the response has "
			"no Cache-Control and no Expires header that allow caching");
		ensure_equals(req.appResponse.statusCode, 200);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(36) {
		set_test_name("It succeeds if the response contains a Cache-Control header with public directive");
		insertAppResponseHeader(createHeader(
			"cache-control", "public"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(37) {
		set_test_name("It succeeds if the response contains a Cache-Control header with max-age directive");
		insertAppResponseHeader(createHeader(
			"cache-control", "max-age=999"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(38) {
		set_test_name("It succeeds if the response contains an Expires header");
		insertAppResponseHeader(createHeader(
			"expires", "Tue, 01 Jan 2030 00:00:00 GMT"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(39) {
		set_test_name("It fails if the response's Cache-Control header contains no-store");
		initCacheableResponse();
		insertAppResponseHeader(createHeader(
			"cache-control", "no-store"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(45) {
		set_test_name("It fails if the response's Cache-Control header contains private");
		initCacheableResponse();
		insertAppResponseHeader(createHeader(
			"cache-control", "private"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(46) {
		set_test_name("It fails if the response's Cache-Control header contains no-cache");
		initCacheableResponse();
		insertAppResponseHeader(createHeader(
			"cache-control", "no-cache"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(47) {
		set_test_name("It fails if the request has an Authorization header");
		initCacheableResponse();
		insertReqHeader(createHeader(
			"authorization", "foo"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(48) {
		set_test_name("It fails if the response has a Vary header");
		initCacheableResponse();
		insertAppResponseHeader(createHeader(
			"vary", "foo"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(49) {
		set_test_name("It fails if the response has a WWW-Authenticate header");
		initCacheableResponse();
		insertAppResponseHeader(createHeader(
			"www-authenticate", "foo"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(50) {
		set_test_name("It fails if the response has an X-Sendfile header");
		initCacheableResponse();
		insertAppResponseHeader(createHeader(
			"x-sendfile", "foo"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(51) {
		set_test_name("It fails if the response has an X-Accel-Redirect header");
		initCacheableResponse();
		insertAppResponseHeader(createHeader(
			"x-accel-redirect", "foo"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}


	/***** Invalidation *****/

	TEST_METHOD(60) {
		set_test_name("Direct invalidation");
		string responseHeadersStr =
			"content-length: 5\r\n"
			"cache-control: public,max-age=99999\r\n";
		string responseBodyStr = "hello";
		initCacheableResponse();
		initResponseBody(responseBodyStr);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", responseCache.prepareRequestForStoring(&req));

		ResponseCacheType::Entry entry(responseCache.store(&req, time(NULL),
			responseHeadersStr.size(), responseBodyStr.size()));
		ensure("(5)", entry.valid());
		ensure_equals("(6)", entry.index, 0u);


		reset();
		req.method = HTTP_POST;
		ensure("(10)", responseCache.prepareRequest(this, &req));
		ensure("(11)", !responseCache.requestAllowsStoring(&req));
		ensure("(12)", responseCache.requestAllowsInvalidating(&req));
		responseCache.invalidate(&req);


		reset();
		ensure("(20)", responseCache.prepareRequest(this, &req));
		ensure("(21)", responseCache.requestAllowsFetching(&req));
		ResponseCacheType::Entry entry2(responseCache.fetch(&req, time(NULL)));
		ensure("(22)", !entry2.valid());
	}

	TEST_METHOD(61) {
		set_test_name("Invalidation via Location response header");
		string responseHeadersStr =
			"content-length: 5\r\n"
			"cache-control: public,max-age=99999\r\n";
		string responseBodyStr = "hello";
		initCacheableResponse();
		initResponseBody(responseBodyStr);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", responseCache.prepareRequestForStoring(&req));

		ResponseCacheType::Entry entry(responseCache.store(&req, time(NULL),
			responseHeadersStr.size(), responseBodyStr.size()));
		ensure("(5)", entry.valid());
		ensure_equals("(6)", entry.index, 0u);


		reset();
		req.method = HTTP_POST;
		psg_lstr_init(&req.path);
		psg_lstr_append(&req.path, req.pool, "/foo");
		insertAppResponseHeader(createHeader(
			"location", "/"),
			req.pool);
		ensure("(10)", responseCache.prepareRequest(this, &req));
		ensure("(11)", !responseCache.requestAllowsStoring(&req));
		ensure("(12)", responseCache.requestAllowsInvalidating(&req));
		responseCache.invalidate(&req);


		reset();
		ensure("(20)", responseCache.prepareRequest(this, &req));
		ensure("(21)", responseCache.requestAllowsFetching(&req));
		ResponseCacheType::Entry entry2(responseCache.fetch(&req, time(NULL)));
		ensure("(22)", !entry2.valid());
	}

	TEST_METHOD(62) {
		set_test_name("Invalidation via Content-Location response header");
		string responseHeadersStr =
			"content-length: 5\r\n"
			"cache-control: public,max-age=99999\r\n";
		string responseBodyStr = "hello";
		initCacheableResponse();
		initResponseBody(responseBodyStr);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", responseCache.prepareRequestForStoring(&req));

		ResponseCacheType::Entry entry(responseCache.store(&req, time(NULL),
			responseHeadersStr.size(), responseBodyStr.size()));
		ensure("(5)", entry.valid());
		ensure_equals("(6)", entry.index, 0u);


		reset();
		req.method = HTTP_POST;
		psg_lstr_init(&req.path);
		psg_lstr_append(&req.path, req.pool, "/foo");
		insertAppResponseHeader(createHeader(
			"content-location", "/"),
			req.pool);
		ensure("(10)", responseCache.prepareRequest(this, &req));
		ensure("(11)", !responseCache.requestAllowsStoring(&req));
		ensure("(12)", responseCache.requestAllowsInvalidating(&req));
		responseCache.invalidate(&req);


		reset();
		ensure("(20)", responseCache.prepareRequest(this, &req));
		ensure("(21)", responseCache.requestAllowsFetching(&req));
		ResponseCacheType::Entry entry2(responseCache.fetch(&req, time(NULL)));
		ensure("(22)", !entry2.valid());
	}
}
