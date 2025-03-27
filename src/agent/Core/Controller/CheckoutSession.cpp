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
#include <DataStructures/LString.h>
#include <Core/Controller.h>
#include <Core/SpawningKit/ErrorRenderer.h>

#if defined(__GLIBCXX__) || defined(__APPLE__)
	#include <cxxabi.h>
	#define CXX_ABI_API_AVAILABLE
#endif

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
Controller::handleSessionInitiationError(Client *client, Request *req, const std::exception &e) {
	// TODO: only retry the session initiation if error is recoverable
	if (req->sessionCheckoutTry < MAX_SESSION_CHECKOUT_TRY) {
		SKC_DEBUG(client, "Error initiating session (" << e.what() << "); retrying (attempt " << int(req->sessionCheckoutTry) << ")");
		refRequest(req, __FILE__, __LINE__);
		getContext()->libev->runLater(boost::bind(checkoutSessionLater, req));
	} else {
		SKC_ERROR(client, "Error initiating session (" << e.what() << "); giving up after too many retries");
		endRequestAsGatewayTimeout(&client, &req);
	}
}

int
Controller::getSessionSocketConnectIoWatchConditions() const {
	return EV_WRITE;
}

double
Controller::getSessionSocketEffectiveConnectTimeout(Request *req) const {
	return req->connectTimeout;
}

void
Controller::initiateSession(Client *client, Request *req) {
	TRACE_POINT();
	req->sessionCheckoutTry++;
	try {
		if (!req->session->initiate()) {
			SKC_DEBUG(client, "Session socket connection in progress");

			req->connectedWatcher.data = req;
			req->connectedWatcherTimout.data = req;

			ev_io_init(&req->connectedWatcher, onSessionSocketConnected, req->session->fd(), getSessionSocketConnectIoWatchConditions());
			ev_timer_init(&req->connectedWatcherTimout, onSessionSocketConnectTimeout, getSessionSocketEffectiveConnectTimeout(req), 0);

			ev_io_start(getLoop(), &req->connectedWatcher);
			ev_timer_start(getLoop(), &req->connectedWatcherTimout);

			return;
		}
	} catch (const oxt::tracable_exception &e) {
		UPDATE_TRACE_POINT();
		handleSessionInitiationError(client, req, e);
		return;
	}

	UPDATE_TRACE_POINT();
	SKC_DEBUG(client, "Session socket immediately connected");
	finishInitiatingSession(client, req);
}

void
Controller::finishInitiatingSession(Client *client, Request *req) {
	TRACE_POINT();
	SKC_DEBUG(client, "Session initiated: fd=" << req->session->fd());
	req->appSink.reinitialize(req->session->fd());
	req->appSource.reinitialize(req->session->fd());
	/***************/
	/***************/
	reinitializeAppResponse(client, req);
	sendHeaderToApp(client, req);
}

void
Controller::onSessionSocketConnected(EV_P_ ev_io *io, int revents) {
	Request *req = static_cast<Request *>(io->data);
	Client *client = static_cast<Client *>(req->client);
	Controller *self = static_cast<Controller *>(Controller::getServerFromClient(client));

	TRACE_POINT();

    ev_io_stop(self->getLoop(), io);
	if (req->connectedWatcherTimout.active) {
		ev_timer_stop(self->getLoop(), &req->connectedWatcherTimout);
	}

	if (revents & EV_WRITE) {
		// Socket connect finished
		int connectError = 0;
		socklen_t connectErrorLen = sizeof(connectError);
		if (-1 == getsockopt(req->session->fd(), SOL_SOCKET, SO_ERROR, &connectError, &connectErrorLen)) {
			int err = errno;
			SystemException e("Cannot check socket status", err);
			self->handleSessionInitiationError(client, req, e);
			return;
		} else if (connectError != 0) {
			// connectError uses the same error codes as errno
			SystemException e("Cannot connect socket", connectError);
			self->handleSessionInitiationError(client, req, e);
			return;
		}

		self->finishInitiatingSession(client, req);
	} else {
		// Something went very wrong
		RuntimeException e("Unknown error occurred while waiting for socket connect to finish");
		self->handleSessionInitiationError(client, req, e);
	}
}

void
Controller::onSessionSocketConnectTimeout(EV_P_ ev_timer *io, int flag) {
	Request *req = static_cast<Request *>(io->data);
	Client *client = static_cast<Client *>(req->client);
	Controller *self = static_cast<Controller *>(Controller::getServerFromClient(client));

	TRACE_POINT();
	ev_io_stop(self->getLoop(), &req->connectedWatcher);
	SystemException e("Waiting on socket connect timed out", ETIMEDOUT);
	self->handleSessionInitiationError(client, req, e);
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

int
Controller::lookupCodeFromHeader(Request *req, const char* header, int statusCode)
{
	const LString *value = req->secureHeaders.lookup(header);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		statusCode = stringToInt(
			StaticString(value->start->data, value->size));
	}
	return statusCode;
}

void
Controller::writeRequestQueueFullExceptionErrorResponse(Client *client, Request *req,
	const boost::shared_ptr<RequestQueueFullException> &e)
{
	TRACE_POINT();

	int requestQueueOverflowStatusCode = lookupCodeFromHeader(req, "!~PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE", 503);

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
	int spawnExceptionStatusCode = lookupCodeFromHeader(req, "!~PASSENGER_SPAWN_EXCEPTION_STATUS_CODE", 500);
	SKC_ERROR(client, "Cannot checkout session because a spawning error occurred. " <<
		"The identifier of the error is " << e->getId() << ". Please see earlier logs for " <<
		"details about the error.");
	endRequestWithErrorResponse(&client, &req, *e, spawnExceptionStatusCode);
}

void
Controller::writeOtherExceptionErrorResponse(Client *client, Request *req, const ExceptionPtr &e) {
	TRACE_POINT();

	// ATM "other" exceptions always return a spawn exception error message, so use the matching status code
	int otherExceptionStatusCode = lookupCodeFromHeader(req, "!~PASSENGER_SPAWN_EXCEPTION_STATUS_CODE", 500);

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

		endRequestWithSimpleResponse(&client, &req, StaticString(buf, pos - buf), otherExceptionStatusCode);
	} else {
		endRequestWithSimpleResponse(&client, &req, "<h2>Internal server error</h2>"
			"Application could not be started. Please try again later.", otherExceptionStatusCode);
	}
}

void
Controller::endRequestWithErrorResponse(Client **c, Request **r,
	const SpawningKit::SpawnException &e, int statusCode)
{
	TRACE_POINT();
	Client *client = *c;
	Request *req = *r;
	SpawningKit::ErrorRenderer renderer(*appPool->getSpawningKitContext());
	string data;
	const LString *path = customErrorPageEnabled(req);

	if (!psg_lstr_cmp(path, StaticString(""))) {
		try {
			data = renderer.renderCustom(StaticString(path->start->data, path->size));
		} catch (const SystemException &e2) {
			SKC_ERROR(client, "Cannot render an error page: " << e2.what() <<
				"\n" << e2.backtrace());
			data = "<h2>Internal server error</h2>";
		}
	} else if (friendlyErrorPagesEnabled(req)) {
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

	endRequestWithSimpleResponse(c, r, psg_pstrdup(req->pool, data), statusCode);
}

const LString*
Controller::customErrorPageEnabled(Request *req) {
	const StaticString name = "!~PASSENGER_CUSTOM_ERROR_PAGE";
	LString* customErrorPagePath = req->secureHeaders.lookup(name);
    if (customErrorPagePath != NULL && customErrorPagePath->size > 0) {
        return psg_lstr_make_contiguous(customErrorPagePath, req->pool);
    } else {
        customErrorPagePath = (LString *) psg_palloc(req->pool, sizeof(LString));
        psg_lstr_init(customErrorPagePath);
		return customErrorPagePath;
    }
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
