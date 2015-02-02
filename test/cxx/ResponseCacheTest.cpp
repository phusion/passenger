#include "TestSupport.h"
#include <ServerKit/HttpRequest.h>
#include <MemoryKit/palloc.h>
#include <agents/HelperAgent/RequestHandler/Request.h>
#include <agents/HelperAgent/RequestHandler/AppResponse.h>
#include <agents/HelperAgent/ResponseCache.h>

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
			req.httpMajor = 1;
			req.httpMinor = 0;
			req.httpState = Request::COMPLETE;
			req.bodyType  = Request::RBT_NO_BODY;
			req.method    = HTTP_GET;
			req.wantKeepAlive = false;
			req.responseBegun = false;
			req.client    = NULL;
			req.pool      = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
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
		}

		~ResponseCacheTest() {
			psg_destroy_pool(req.pool);
		}

		LString *createHostString() {
			LString *str = (LString *) psg_palloc(req.pool, sizeof(LString));
			psg_lstr_init(str);
			psg_lstr_append(str, req.pool, "foo.com");
			return str;
		}

		Header *createHeader(const HashedStaticString &key, const StaticString &val) {
			Header *header = (Header *) psg_palloc(req.pool, sizeof(Header));
			psg_lstr_init(&header->key);
			psg_lstr_init(&header->val);
			psg_lstr_append(&header->key, req.pool, key.data(), key.size());
			psg_lstr_append(&header->val, req.pool, val.data(), val.size());
			header->hash = key.hash();
			return header;
		}

		void initCacheableResponse() {
			req.appResponse.headers.insert(createHeader(
				"cache-control", "public,max-age=99999"),
				req.pool);
		}

		void initUncacheableResponse() {
			req.appResponse.headers.insert(createHeader(
				"cache-control", "private"),
				req.pool);
		}
	};

	DEFINE_TEST_GROUP(ResponseCacheTest);


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
		req.headers.insert(createHeader(
			"cache-control", "xyz"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsFetching(&req));
	}

	TEST_METHOD(20) {
		set_test_name("It fails if the request has a Pragma header");
		req.headers.insert(createHeader(
			"pragma", "xyz"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsFetching(&req));
	}


	/***** Checking whether request should be stored to cache *****/

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
		req.headers.insert(createHeader(
			"cache-control", "no-store"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", !responseCache.requestAllowsStoring(&req));
	}

	TEST_METHOD(33) {
		set_test_name("It fails if the request is not default cacheable");
		req.appResponse.statusCode = 205;
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(34) {
		set_test_name("It fails if the request is default cacheable, but the response has "
			"no Cache-Control and no Expires header that allow caching");
		ensure_equals(req.appResponse.statusCode, 200);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(35) {
		set_test_name("It succeeds if the response contains a Cache-Control header with public directive");
		req.appResponse.headers.insert(createHeader(
			"cache-control", "public"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(36) {
		set_test_name("It succeeds if the response contains a Cache-Control header with max-age directive");
		req.appResponse.headers.insert(createHeader(
			"cache-control", "max-age=999"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(37) {
		set_test_name("It succeeds if the response contains an Expires header");
		req.appResponse.headers.insert(createHeader(
			"expires", "Tue, 01 Jan 2030 00:00:00 GMT"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(38) {
		set_test_name("It fails if the response's Cache-Control header contains no-store");
		req.appResponse.headers.insert(createHeader(
			"cache-control", "no-store"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(45) {
		set_test_name("It fails if the response's Cache-Control header contains private");
		req.appResponse.headers.insert(createHeader(
			"cache-control", "private"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(46) {
		set_test_name("It fails if the request has a Authorization header");
		req.headers.insert(createHeader(
			"authorization", "foo"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(47) {
		set_test_name("It fails if the response has a Vary header");
		req.appResponse.headers.insert(createHeader(
			"vary", "foo"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}

	TEST_METHOD(48) {
		set_test_name("It fails if the response has a WWW-Authenticate header");
		req.appResponse.headers.insert(createHeader(
			"www-authenticate", "foo"),
			req.pool);
		ensure("(1)", responseCache.prepareRequest(this, &req));
		ensure("(2)", responseCache.requestAllowsStoring(&req));
		ensure("(3)", !responseCache.prepareRequestForStoring(&req));
	}
}
