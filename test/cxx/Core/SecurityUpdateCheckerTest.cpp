/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2016 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  See LICENSE file for license information.
 */

#include <TestSupport.h>
#include <Core/SecurityUpdateChecker.h>
#include <modp_b64.h>

using namespace Passenger;
using namespace std;

namespace tut {

	void failNiceWhenSubstringMismatch(string substring, string s) {
		string failMessage = "expected [" + substring + "] in [" + s + "]";
		ensure_equals(failMessage.c_str(), containsSubstring(s, substring), true);
	}

	class TestChecker: public SecurityUpdateChecker {

	private:
		CURLcode mockResponseCurlCode;
		int mockResponseHttpCode;
		string mockResponseData;
		string mockNonce;

	public:
		string lastError;
		string lastSuccess;
		string lastSuccessAdditional;

		TestChecker(ResourceLocator locator, string proxy)
			: SecurityUpdateChecker(locator, proxy),
			  mockResponseCurlCode(CURLE_FAILED_INIT),
			  mockResponseHttpCode(0),
			  mockResponseData(""),
			  mockNonce(""),
			  lastSuccessAdditional("")
			  { }

		virtual ~TestChecker() {
		}

		virtual void logUpdateFail(string error) {
			lastError = error; // store for checking
		}

		virtual void logUpdateFailAdditional(string additional) {
			// no op
		}

		virtual void logUpdateSuccess(int update, string success) {
			lastSuccess = success; // store for checking
		}

		virtual void logUpdateSuccessAdditional(string additional) {
			lastSuccessAdditional = additional;
		}

		virtual CURLcode sendAndReceive(CURL *curl, string *responseData, long *responseCode) {
			responseData->append(mockResponseData);
			*responseCode = (int) mockResponseHttpCode;
			return mockResponseCurlCode;
		}

		virtual void fillNonce(string &nonce) {
			nonce.append(mockNonce);
		}

		int testRaw(CURLcode responseCurlCode, int responseHttpCode, string responseData, string nonce) {
			lastError = "";
			lastSuccess = "";
			lastSuccessAdditional = "";
			mockResponseCurlCode = responseCurlCode;
			mockResponseHttpCode = responseHttpCode;
			mockResponseData = responseData;
			mockNonce = nonce;
			return checkAndLogSecurityUpdate();
		}

		void testContentFail(string expectedError, CURLcode responseCurlCode, int responseHttpCode, string data, string signature, string nonce) {
			testRaw(responseCurlCode, responseHttpCode, "{\"data\":\"" + data + "\", \"signature\":\"" + signature + "\"}", nonce);
			failNiceWhenSubstringMismatch(expectedError, lastError);
		}

	};

	struct Core_SecurityUpdateCheckerTest {
		boost::shared_ptr<TestChecker> checker;

		Core_SecurityUpdateCheckerTest() {
			setLogLevel(0);
		}

		~Core_SecurityUpdateCheckerTest() {
			setLogLevel(DEFAULT_LOG_LEVEL);
		}

		void init(string proxyAddress) {
			checker = boost::make_shared<TestChecker>(*resourceLocator, proxyAddress);
		}
	};

	DEFINE_TEST_GROUP(Core_SecurityUpdateCheckerTest);

	TEST_METHOD(1) {
		set_test_name("succeeds with good signature, nonce and data, for update and no update");

		init("");

		checker->testRaw(CURLE_OK, 200, "{\"data\":\"eyJ1cGRhdGUiOjAsImxvZyI6IiIsImJhY2tvZmYiOjAsIm5vbmNlIjoiMTQ3NDkwMDg4MTY3OTQzNDVHc2dOZXJxMU04akdPak9Pd05pTGc9PSJ9\","
				"\"signature\":\"FopgXeV0cfvf4ekwR4e9EqOMxqAyQXC7kErf6Lz3sn0GhFG0FEauKtpiElEBvSyoeXi+UeGWhCXHbT449aOdfA0LIH7Bp4clBrF5P+CNUI1HK7C5Y8X2hjEsBi56OkfHF1uT0R8Z7SF/dYgW8LNKLo09hBfzP7RHX7HDrFGhbBuEAOxo+fYpmKmbHduk0FOciNeElJTTyusqtMcN5/QvSalIbRPR04Au61awG9R3ArWK7ocIkKBkyDfPAnmOjnRrEjS7byo/Yw3GBAhQQ+24pzwwMytn0WbZXekk89mgXs/B4OUCTp+TfkVcDJSMA76YMv1wqfQEO9hvlIUCNrUyR1lHRqRP3ZgAXmdX5e6lg+fTiIx35vpS8l4GQ90wk0wzJJLETDURKk97gmATb61Opn8J7kZxtN4itdphqZa9zx8IhpdtRluLBXrlsYj9oolyOL/vOpMD///Gx9hmcShLxJ/qq+taGhpEoqadWnZsQljkSnlfopX9Q1cxQf/Grte+YNOe7FItVguoJBrfg6g8NISFODdMpnigHsFUtsWLtC1HfL0fN7GmOc4F+fhJbmDY0kCcXEIb/N1z65eQDs/MzeoMlzp/9Qsih2i5HtXtaAuV50UGRd4LguOgWSkXENIcRQPB37etOHchC/Q0lDS44merm99q8VCU197SJpDP+Fw=\"}",
				"14749008816794345GsgNerq1M8jGOjOOwNiLg==");

		ensure_equals(checker->lastError.c_str(), checker->lastError.empty(), true);
		failNiceWhenSubstringMismatch("no update found", checker->lastSuccess);

		checker->testRaw(CURLE_OK, 200, "{\"data\":\"eyJ1cGRhdGUiOjEsInZlcnNpb24iOiI1LjEuMCIsImxvZyI6IiIsImJhY2tvZmYiOjAsIm5vbmNlIjoiMTQ3NDkwNDE1MDU5MjM5M0FKd1VMcElGWkY3d3VyLzFWMHNBZFE9PSJ9\","
						"\"signature\":\"ivK80A0f4ZOOUw3XlbCCnTZJ7CvJ4dQQrvcyMrBDSZXQ4DGoLIV/y39QHE0eh6bj22iGLps0vYups7ZL8FAcpGz3lzAwgSoSCtUUl71zQtJfLreElSBGmtu0zQywlsgvoWgkOxADRStVzY53TaX+1T+VTpx3E5F8aGG93fCC5ilEOM2+yVtpOSOLNAtONqcQ+nq8sIiJSKCljD7sFJLZ3dLu0UlV/lEmw8S/UzoQuTgk8yigkD0D4Gf7FYGuJ4gG5tCst0F3MYYdV9wfl7ZGqJRhE0O2asbH0a7ja1kXlY7nSdO1/MEMVcYVvwNVHDYPO2Jdf4UTTRzXd00b+XvqlmsjXP2lwafZt8854xnbI0DFuEPOFF3rUzzxe7vvadnFjkOt12TM7QqezVfyZkR7NOQ7XiT5KV7m3Iq+K9bFq1OsfCc/MDsqJ6fQZUtWsyfhsWcof0mgZllu/TPYajl/Bz+C4jPP8J+oW215NXz2Q8UuHm3a1IEE36nSlZ8KTilCKIojF3gq/fwS5AiYK7gbdHlQkYkKNowvPgfdegIjr371aW3OMuTB1mUxm8IagjCIe0hZ9udHA1rXGqpH2MkjtU99EJlf9ThL8pH8p+3Gtj3vVFFSjlaNx2LNRf5XHFIh4RJwr6d8HLnvZSCxZyq0bDHqtWsnoQe7LTap97rqGno=\"}",
						"1474904150592393AJwULpIFZF7wur/1V0sAdQ==");

		ensure_equals(checker->lastError.c_str(), checker->lastError.empty(), true);
		failNiceWhenSubstringMismatch("we strongly recommend upgrading", checker->lastSuccess);
	}

	TEST_METHOD(2) {
		set_test_name("correctly reports various signature field errors");

		init("");

		checker->testRaw(CURLE_OK, 200, "{\"data\":\"invalid_base64\"}", "");
		failNiceWhenSubstringMismatch("missing response fields", checker->lastError);

		checker->testContentFail("corrupted signature", CURLE_OK, 200, "invalid_base64", "invalid_base64", "");

		checker->testContentFail("forged signature", CURLE_OK, 200, "invalid_base64", "yyyy", "");
	}

	TEST_METHOD(3) {
		set_test_name("catches replay attack (nonce mismatch)");

		init("");

		checker->testContentFail("replay attack", CURLE_OK, 200, "eyJ1cGRhdGUiOjAsImxvZyI6IiIsImJhY2tvZmYiOjAsIm5vbmNlIjoiMTQ3NDkwMDg4MTY3OTQzNDVHc2dOZXJxMU04akdPak9Pd05pTGc9PSJ9",
			"FopgXeV0cfvf4ekwR4e9EqOMxqAyQXC7kErf6Lz3sn0GhFG0FEauKtpiElEBvSyoeXi+UeGWhCXHbT449aOdfA0LIH7Bp4clBrF5P+CNUI1HK7C5Y8X2hjEsBi56OkfHF1uT0R8Z7SF/dYgW8LNKLo09hBfzP7RHX7HDrFGhbBuEAOxo+fYpmKmbHduk0FOciNeElJTTyusqtMcN5/QvSalIbRPR04Au61awG9R3ArWK7ocIkKBkyDfPAnmOjnRrEjS7byo/Yw3GBAhQQ+24pzwwMytn0WbZXekk89mgXs/B4OUCTp+TfkVcDJSMA76YMv1wqfQEO9hvlIUCNrUyR1lHRqRP3ZgAXmdX5e6lg+fTiIx35vpS8l4GQ90wk0wzJJLETDURKk97gmATb61Opn8J7kZxtN4itdphqZa9zx8IhpdtRluLBXrlsYj9oolyOL/vOpMD///Gx9hmcShLxJ/qq+taGhpEoqadWnZsQljkSnlfopX9Q1cxQf/Grte+YNOe7FItVguoJBrfg6g8NISFODdMpnigHsFUtsWLtC1HfL0fN7GmOc4F+fhJbmDY0kCcXEIb/N1z65eQDs/MzeoMlzp/9Qsih2i5HtXtaAuV50UGRd4LguOgWSkXENIcRQPB37etOHchC/Q0lDS44merm99q8VCU197SJpDP+Fw=",
			"non-matching nonce");
	}

	TEST_METHOD(4) {
		set_test_name("additional log is logged whether update=0 or 1");

		init("");

		// update = 0
		checker->testRaw(CURLE_OK, 200, "{\"data\":\"eyJ1cGRhdGUiOjAsImxvZyI6ImFkZGl0aW9uYWxpbmZvIiwiYmFja29mZiI6MCwibm9uY2UiOiIxNDc0OTA1MTkzNDg4Mjc3Y2c4ZmNMdDJDOWZ3dDAweDc3enYvdz09In0=\","
			"\"signature\":\"PwbbOmnL7g7hydKKlSDxfUpTLFqaKe4DXLn46kNjQoy0GeP2iMkKsNWRfyDoUm8TFzvlj1bczL6ZMBFKqZjDhnS3u95OUTDb3BvK0S8lvRQcf4EsM7JB7aQ9T2QulU+L5sfqQXF+zGtseqIIkqPJbTb5Wy15QyzD4Jo75FwMvehGokWy1rNx7fPCQTUM4AhyqEebGOt2beuN3ZH4LmPlu/mEyD+2YxI/draczVAIpSH29TRh5vCYLyLzpXsZIkMFgxloG19IGzI5SCWGA7k5s2bBXt4tXk0P7sCKHhtvanO5gp75JEvLd4Kzz/jZN3A9ymjRWuPFWNAExdddzfr7YrwS0uBHaC4kyh1FtSlV52kdngHW5ciLTIg+45gh276Ic/WSEy3B52n0GZ0kJDKP8xZ6fdO/3iXi0xQ8Te7jDh0T78L9jyOQg2p4br9fQUanTGQeSQsN4XTDql/jzegW4cvbz3/tlKGi1xywCKCYglhSAJVw1rWcWyIRRW14qhLlV0081iBHgHIhagD0Ssl0ncI9YJPUtbotvNXLd4DBUmvjnhJmS5jQFgiKbJO+ZYaBJpltXdB0WCexR1EOK0VuJM561mv/FP3c7tmsFReqYaZ+UQNmx0hTA6vel4Uv5XI0qiOcAOsgrxxsVCBdCYA2tyfyhRMQl7x2wZZ6M5/dhso=\"}",
			"1474905193488277cg8fcLt2C9fwt00x77zv/w==");

		ensure_equals(checker->lastError.c_str(), checker->lastError.empty(), true);
		failNiceWhenSubstringMismatch("additionalinfo", checker->lastSuccessAdditional);

		checker->testRaw(CURLE_OK, 200, "{\"data\":\"eyJ1cGRhdGUiOjEsInZlcnNpb24iOiI1LjEuMCIsImxvZyI6ImFkZGl0aW9uYWxpbmZvIiwiYmFja29mZiI6MCwibm9uY2UiOiIxNDc0OTA1NjI3MTcwNDY1RGsybE9LcUl6MVdLVlJqYUF3RUtMdz09In0=\","
			"\"signature\":\"v/05dhOnw4wi/cS2Emlmki0aLG3Og+t3QkHdnYm9sGmI3/wIl1Pqsng1CQ2utei2eM6ROoDjZLyKtG58NjrnYAG8b7jfo85LiFvaibrej9FC0uDHsbdZuODlpHUuWmBi1uCKKdJ+1dL26W2+nPvExlwQTyEoNhuIbW2Ji7QnY33vbE5dV4luf5aWdwuPtaWKm+NvDBY2mgKxfeeXfPOhTU+H8LQCo59fNIQwBb7vvaTUtIFCwWHGRqJ0asM1yCM7bfT+zyP7J+tvebvFmAX9MVtl5rkvXkkkyiTPFfpZ+EiD9fROipy8ubMB6hxJQnW3xcXPZXiE88Bpssidb0vzLIxpAfz7HjfO2Tt6sl7Ekks4ql4B7GSy/Cw4S3HgoIjD9gq1pI1PMdjrktHCh5TDRLiV2s14mbLFJvxsayn6okO/s4lASt8GQSXYY6Rea0RvPHplbX6HDjGVthydu7+YG/rBTfcT6wKJM9btfsZX6T59n7uZG6EMJEW2TE4C7aIN1v0ztRNCBxKanSGtkvrUIRXtp5bq+lTSDST/4JwCZnyFCB6i3ju4iKsJOmRxjgp5OoS4aEGOMJGcUwokSXTcx072rGoaK13dW8bg7sK1PgXQaFboC/NP5feQlj9fhJkLeOsMJobJwfv6cHwF73HleJSa047KvdNR+iHheegHV1c=\"}",
			"1474905627170465Dk2lOKqIz1WKVRjaAwEKLw==");

		// update = 1
		ensure_equals(checker->lastError.c_str(), checker->lastError.empty(), true);
		failNiceWhenSubstringMismatch("additionalinfo", checker->lastSuccessAdditional);
	}

	TEST_METHOD(5) {
		set_test_name("enriches CURL errors");

		init("");

		checker->testContentFail("check your connection security", CURLE_COULDNT_CONNECT, 0, "", "", "");
		checker->testContentFail("try upgrading or reinstalling", CURLE_SSL_CERTPROBLEM, 0, "", "", "");
		checker->testContentFail("truststore", CURLE_PEER_FAILED_VERIFICATION, 0, "", "", "");
	}

	TEST_METHOD(6) {
		set_test_name("enriches HTTP errors");

		init("");

		checker->testContentFail("not found", CURLE_OK, 404, "", "", "");
		checker->testContentFail("rate limit", CURLE_OK, 429, "", "", "");
		checker->testContentFail("HTTP 500", CURLE_OK, 500, "", "", "");
		checker->testContentFail("try again later", CURLE_OK, 503, "", "", "");
	}
}
