/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2017 Phusion Holding B.V.
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
#include <Core/Controller.h>
#include <Core/SpawningKit/ErrorRenderer.h>

/*************************************************************************
 *
 * Implements Core::Controller methods pertaining selecting an application
 * process to handle the current request.
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
Controller::checkoutSession(Client *client, Request *req) {
	GetCallback callback;
	Options &options = req->options;

	CC_BENCHMARK_POINT(client, req, BM_BEFORE_CHECKOUT);
	SKC_TRACE(client, 2, "Checking out session: appRoot=" << options.appRoot);
	req->state = Request::CHECKING_OUT_SESSION;

	if (req->requestBodyBuffering) {
		assert(!req->bodyBuffer.isStarted());
	} else {
		assert(!req->bodyChannel.isStarted());
	}

	callback.func = sessionCheckedOut;
	callback.userData = req;

	options.currentTime = SystemTime::getUsec();

	refRequest(req, __FILE__, __LINE__);
	#ifdef DEBUG_CC_EVENT_LOOP_BLOCKING
		req->timeBeforeAccessingApplicationPool = ev_now(getLoop());
	#endif
	asyncGetFromApplicationPool(req, callback);
	#ifdef DEBUG_CC_EVENT_LOOP_BLOCKING
		if (!req->timedAppPoolGet) {
			req->timedAppPoolGet = true;
			ev_now_update(getLoop());
			reportLargeTimeDiff(client, "ApplicationPool get until return",
				req->timeBeforeAccessingApplicationPool, ev_now(getLoop()));
		}
	#endif
}

void
Controller::asyncGetFromApplicationPool(Request *req, ApplicationPool2::GetCallback callback) {
	appPool->asyncGet(req->options, callback, true);
}

void
Controller::sessionCheckedOut(const AbstractSessionPtr &session, const ExceptionPtr &e,
	void *userData)
{
	Request *req = static_cast<Request *>(userData);
	Client *client = static_cast<Client *>(req->client);
	Controller *self = static_cast<Controller *>(getServerFromClient(client));

	if (self->getContext()->libev->onEventLoopThread()) {
		self->sessionCheckedOutFromEventLoopThread(client, req, session, e);
		self->unrefRequest(req, __FILE__, __LINE__);
	} else {
		self->getContext()->libev->runLater(
			boost::bind(&Controller::sessionCheckedOutFromAnotherThread,
				self, client, req, session, e));
	}
}

void
Controller::sessionCheckedOutFromAnotherThread(Client *client, Request *req,
	AbstractSessionPtr session, ExceptionPtr e)
{
	SKC_LOG_EVENT(Controller, client, "sessionCheckedOutFromAnotherThread");
	sessionCheckedOutFromEventLoopThread(client, req, session, e);
	unrefRequest(req, __FILE__, __LINE__);
}

void
Controller::sessionCheckedOutFromEventLoopThread(Client *client, Request *req,
	const AbstractSessionPtr &session, const ExceptionPtr &e)
{
	if (req->ended()) {
		return;
	}

	TRACE_POINT();
	CC_BENCHMARK_POINT(client, req, BM_AFTER_CHECKOUT);

	#ifdef DEBUG_CC_EVENT_LOOP_BLOCKING
		if (!req->timedAppPoolGet) {
			req->timedAppPoolGet = true;
			ev_now_update(getLoop());
			reportLargeTimeDiff(client, "ApplicationPool get until return",
				req->timeBeforeAccessingApplicationPool, ev_now(getLoop()));
		}
	#endif

	if (e == NULL) {
		SKC_DEBUG(client, "Session checked out: pid=" << session->getPid() <<
			", gupid=" << session->getGupid());
		req->session = session;
		UPDATE_TRACE_POINT();
		maybeSend100Continue(client, req);
		UPDATE_TRACE_POINT();
		initiateSession(client, req);
	} else {
		UPDATE_TRACE_POINT();
		reportSessionCheckoutError(client, req, e);
	}
}

void
Controller::maybeSend100Continue(Client *client, Request *req) {
	int httpVersion = req->httpMajor * 1000 + req->httpMinor * 10;
	if (httpVersion >= 1010 && req->hasBody() && !req->strip100ContinueHeader) {
		// Apps with the "session" protocol don't respond with 100-Continue,
		// so we do it for them.
		const LString *value = req->headers.lookup(HTTP_EXPECT);
		if (value != NULL
		 && psg_lstr_cmp(value, P_STATIC_STRING("100-continue"))
		 && req->session->getProtocol() == P_STATIC_STRING("session"))
		{
			const unsigned int BUFSIZE = 32;
			char *buf = (char *) psg_pnalloc(req->pool, BUFSIZE);
			int size = snprintf(buf, BUFSIZE, "HTTP/%d.%d 100 Continue\r\n",
				(int) req->httpMajor, (int) req->httpMinor);
			writeResponse(client, buf, size);
			if (!req->ended()) {
				// Allow sending more response headers.
				req->responseBegun = false;
			}
		}
	}
}

void
Controller::initiateSession(Client *client, Request *req) {
	TRACE_POINT();
	req->sessionCheckoutTry++;
	try {
		req->session->initiate(false);
	} catch (const SystemException &e2) {
		if (req->sessionCheckoutTry < MAX_SESSION_CHECKOUT_TRY) {
			SKC_DEBUG(client, "Error checking out session (" << e2.what() <<
				"); retrying (attempt " << req->sessionCheckoutTry << ")");
			refRequest(req, __FILE__, __LINE__);
			getContext()->libev->runLater(boost::bind(checkoutSessionLater, req));
		} else {
			string message = "could not initiate a session (";
			message.append(e2.what());
			message.append(")");
			disconnectWithError(&client, message);
		}
		return;
	}

	UPDATE_TRACE_POINT();

	UPDATE_TRACE_POINT();
	SKC_DEBUG(client, "Session initiated: fd=" << req->session->fd());
	req->appSink.reinitialize(req->session->fd());
	req->appSource.reinitialize(req->session->fd());
	/***************/
	/***************/
	reinitializeAppResponse(client, req);
	sendHeaderToApp(client, req);
}

void
Controller::checkoutSessionLater(Request *req) {
	Client *client = static_cast<Client *>(req->client);
	Controller *self = static_cast<Controller *>(
		Controller::getServerFromClient(client));
	SKC_LOG_EVENT_FROM_STATIC(self, Controller, client, "checkoutSessionLater");

	if (!req->ended()) {
		self->checkoutSession(client, req);
	}
	self->unrefRequest(req, __FILE__, __LINE__);
}

void
Controller::reportSessionCheckoutError(Client *client, Request *req,
	const ExceptionPtr &e)
{
	TRACE_POINT();
	{
		boost::shared_ptr<RequestQueueFullException> e2 =
			dynamic_pointer_cast<RequestQueueFullException>(e);
		if (e2 != NULL) {
			writeRequestQueueFullExceptionErrorResponse(client, req, e2);
			return;
		}
	}
	{
		boost::shared_ptr<SpawningKit::SpawnException> e2 =
			dynamic_pointer_cast<SpawningKit::SpawnException>(e);
		if (e2 != NULL) {
			writeSpawnExceptionErrorResponse(client, req, e2);
			return;
		}
	}
	writeOtherExceptionErrorResponse(client, req, e);
}

void
Controller::writeRequestQueueFullExceptionErrorResponse(Client *client, Request *req,
	const boost::shared_ptr<RequestQueueFullException> &e)
{
	TRACE_POINT();
	const LString *value = req->secureHeaders.lookup(
		"!~PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE");
	int requestQueueOverflowStatusCode = 503;
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		requestQueueOverflowStatusCode = stringToInt(
			StaticString(value->start->data, value->size));
	}

	SKC_WARN(client, "Returning HTTP " << requestQueueOverflowStatusCode <<
		" due to: " << e->what());

	endRequestWithSimpleResponse(&client, &req,
		"<h2>This website is under heavy load (queue full)</h2>"
		"<p>We're sorry, too many people are accessing this website at the same "
		"time. We're working on this problem. Please try again later.</p>",
		requestQueueOverflowStatusCode);
}

void
Controller::writeSpawnExceptionErrorResponse(Client *client, Request *req,
	const boost::shared_ptr<SpawningKit::SpawnException> &e)
{
	TRACE_POINT();
	SKC_ERROR(client, "Cannot checkout session because a spawning error occurred. " <<
		"The identifier of the error is " << e->getId() << ". Please see earlier logs for " <<
		"details about the error.");
	endRequestWithErrorResponse(&client, &req, *e);
}

void
Controller::writeOtherExceptionErrorResponse(Client *client, Request *req, const ExceptionPtr &e) {
	TRACE_POINT();
	string typeName;
	const oxt::tracable_exception &eptr = *e;
	#ifdef CXX_ABI_API_AVAILABLE
		int status;
		char *tmp = abi::__cxa_demangle(typeid(eptr).name(), 0, 0, &status);
		if (tmp != NULL) {
			typeName = tmp;
			free(tmp);
		} else {
			typeName = typeid(eptr).name();
		}
	#else
		typeName = typeid(eptr).name();
	#endif

	const unsigned int exceptionMessageLen = strlen(e->what());
	string backtrace;
	boost::shared_ptr<tracable_exception> e3 = dynamic_pointer_cast<tracable_exception>(e);
	if (e3 != NULL) {
		backtrace = e3->backtrace();
	}

	SKC_WARN(client, "Cannot checkout session due to " <<
		typeName << ": " << e->what() << (!backtrace.empty() ? "\n" + backtrace : ""));

	if (friendlyErrorPagesEnabled(req)) {
		const unsigned int BUFFER_SIZE = 512 + typeName.size() +
			exceptionMessageLen + backtrace.size();
		char *buf = (char *) psg_pnalloc(req->pool, BUFFER_SIZE);
		char *pos = buf;
		const char *end = buf + BUFFER_SIZE;

		pos = appendData(pos, end, "<h2>Internal server error</h2>");
		pos = appendData(pos, end, "<p>Application could not be started.</p>");
		pos = appendData(pos, end, "<p>Exception type: ");
		pos = appendData(pos, end, typeName);
		pos = appendData(pos, end, "<br>Error message: ");
		pos = appendData(pos, end, e->what(), exceptionMessageLen);
		if (!backtrace.empty()) {
			pos = appendData(pos, end, "<br>Backtrace:<br>");
			pos = appendData(pos, end, backtrace);
		}
		pos = appendData(pos, end, "</p>");

		endRequestWithSimpleResponse(&client, &req, StaticString(buf, pos - buf), 500);
	} else {
		endRequestWithSimpleResponse(&client, &req, "<h2>Internal server error</h2>"
			"Application could not be started. Please try again later.", 500);
	}
}

void
Controller::endRequestWithErrorResponse(Client **c, Request **r,
	const SpawningKit::SpawnException &e)
{
	TRACE_POINT();
	Client *client = *c;
	Request *req = *r;
	SpawningKit::ErrorRenderer renderer(*appPool->getSpawningKitContext());
	string data;

	if (friendlyErrorPagesEnabled(req)) {
		try {
			data = renderer.renderWithDetails(e);
		} catch (const SystemException &e2) {
			SKC_ERROR(client, "Cannot render an error page: " << e2.what() <<
				"\n" << e2.backtrace());
			data = e.getSummary();
		}
	} else {
		try {
			data = renderer.renderWithoutDetails(e);
		} catch (const SystemException &e2) {
			SKC_ERROR(client, "Cannot render an error page: " << e2.what() <<
				"\n" << e2.backtrace());
			data = "<h2>Internal server error</h2>";
		}
	}

	endRequestWithSimpleResponse(c, r, psg_pstrdup(req->pool, data), 500);
}

bool
Controller::friendlyErrorPagesEnabled(Request *req) {
	bool defaultValue;
	const StaticString &defaultStr = req->config->defaultFriendlyErrorPages;
	if (defaultStr == "auto") {
		defaultValue = (req->options.environment == "development");
	} else {
		defaultValue = defaultStr == "true";
	}
	return getBoolOption(req, "!~PASSENGER_FRIENDLY_ERROR_PAGES", defaultValue);
}

/***************/

/***************/


} // namespace Core
} // namespace Passenger
