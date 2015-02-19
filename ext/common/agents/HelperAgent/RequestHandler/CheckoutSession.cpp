/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2015 Phusion
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

// This file is included inside the RequestHandler class.

private:

void
checkoutSession(Client *client, Request *req) {
	GetCallback callback;
	Options &options = req->options;

	RH_BENCHMARK_POINT(client, req, BM_BEFORE_CHECKOUT);
	SKC_TRACE(client, 2, "Checking out session: appRoot=" << options.appRoot);
	req->state = Request::CHECKING_OUT_SESSION;
	req->beginScopeLog(&req->scopeLogs.getFromPool, "get from pool");
	if (req->requestBodyBuffering) {
		assert(!req->bodyBuffer.isStarted());
	} else {
		assert(!req->bodyChannel.isStarted());
	}

	callback.func = sessionCheckedOut;
	callback.userData = req;

	options.currentTime = (unsigned long long) (ev_now(getLoop()) * 1000000);

	refRequest(req, __FILE__, __LINE__);
	#ifdef DEBUG_RH_EVENT_LOOP_BLOCKING
		req->timeBeforeAccessingApplicationPool = ev_now(getLoop());
	#endif
	appPool->asyncGet(options, callback);
	#ifdef DEBUG_RH_EVENT_LOOP_BLOCKING
		if (!req->timedAppPoolGet) {
			req->timedAppPoolGet = true;
			ev_now_update(getLoop());
			reportLargeTimeDiff(client, "ApplicationPool get until return",
				req->timeBeforeAccessingApplicationPool, ev_now(getLoop()));
		}
	#endif
}

static void
sessionCheckedOut(const SessionPtr &session, const ExceptionPtr &e,
	void *userData)
{
	Request *req = static_cast<Request *>(userData);
	Client *client = static_cast<Client *>(req->client);
	RequestHandler *self = static_cast<RequestHandler *>(getServerFromClient(client));

	if (self->getContext()->libev->onEventLoopThread()) {
		self->sessionCheckedOutFromEventLoopThread(client, req, session, e);
		self->unrefRequest(req, __FILE__, __LINE__);
	} else {
		self->getContext()->libev->runLater(
			boost::bind(&RequestHandler::sessionCheckedOutFromAnotherThread,
				self, client, req, session, e));
	}
}

void
sessionCheckedOutFromAnotherThread(Client *client, Request *req,
	SessionPtr session, ExceptionPtr e)
{
	SKC_LOG_EVENT(RequestHandler, client, "sessionCheckedOutFromAnotherThread");
	sessionCheckedOutFromEventLoopThread(client, req, session, e);
	unrefRequest(req, __FILE__, __LINE__);
}

void
sessionCheckedOutFromEventLoopThread(Client *client, Request *req,
	const SessionPtr &session, const ExceptionPtr &e)
{
	if (req->ended()) {
		return;
	}

	TRACE_POINT();
	RH_BENCHMARK_POINT(client, req, BM_AFTER_CHECKOUT);

	#ifdef DEBUG_RH_EVENT_LOOP_BLOCKING
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
		req->endScopeLog(&req->scopeLogs.getFromPool, false);
		reportSessionCheckoutError(client, req, e);
	}
}

void
maybeSend100Continue(Client *client, Request *req) {
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
initiateSession(Client *client, Request *req) {
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
	if (req->useUnionStation()) {
		req->endScopeLog(&req->scopeLogs.getFromPool);
		req->logMessage("Application PID: " +
			toString(req->session->getPid()) +
			" (GUPID: " + req->session->getGupid() + ")");
		req->beginScopeLog(&req->scopeLogs.requestProxying, "request proxying");
	}

	UPDATE_TRACE_POINT();
	SKC_DEBUG(client, "Session initiated: fd=" << req->session->fd());
	req->appSink.reinitialize(req->session->fd());
	req->appSource.reinitialize(req->session->fd());
	/***************/
	/***************/
	reinitializeAppResponse(client, req);
	sendHeaderToApp(client, req);
}

static void
checkoutSessionLater(Request *req) {
	Client *client = static_cast<Client *>(req->client);
	RequestHandler *self = static_cast<RequestHandler *>(
		RequestHandler::getServerFromClient(client));
	SKC_LOG_EVENT_FROM_STATIC(self, RequestHandler, client, "checkoutSessionLater");

	if (!req->ended()) {
		self->checkoutSession(client, req);
	}
	self->unrefRequest(req, __FILE__, __LINE__);
}

void
reportSessionCheckoutError(Client *client, Request *req, const ExceptionPtr &e) {
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
		boost::shared_ptr<SpawnException> e2 = dynamic_pointer_cast<SpawnException>(e);
		if (e2 != NULL) {
			writeSpawnExceptionErrorResponse(client, req, e2);
			return;
		}
	}
	writeOtherExceptionErrorResponse(client, req, e);
}

void
writeRequestQueueFullExceptionErrorResponse(Client *client, Request *req, const boost::shared_ptr<RequestQueueFullException> &e) {
	TRACE_POINT();
	const LString *value = req->secureHeaders.lookup("!~PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE");
	int requestQueueOverflowStatusCode = 503;
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		requestQueueOverflowStatusCode = stringToInt(
			StaticString(value->start->data, value->size));
	}

	SKC_WARN(client, "Returning HTTP " << requestQueueOverflowStatusCode << " due to: " << e->what());

	endRequestWithSimpleResponse(&client, &req,
		"<h1>This website is under heavy load</h1>"
		"<p>We're sorry, too many people are accessing this website at the same "
		"time. We're working on this problem. Please try again later.</p>",
		requestQueueOverflowStatusCode);
}

void
writeSpawnExceptionErrorResponse(Client *client, Request *req,
	const boost::shared_ptr<SpawnException> &e)
{
	TRACE_POINT();
	SKC_ERROR(client, "Cannot checkout session because a spawning error occurred. " <<
		"The identifier of the error is " << e->get("error_id") << ". Please see earlier logs for " <<
		"details about the error.");
	endRequestWithErrorResponse(&client, &req, e->getErrorPage(), e.get());
}

void
writeOtherExceptionErrorResponse(Client *client, Request *req, const ExceptionPtr &e) {
	TRACE_POINT();
	string typeName;
	#ifdef CXX_ABI_API_AVAILABLE
		int status;
		char *tmp = abi::__cxa_demangle(typeid(*e).name(), 0, 0, &status);
		if (tmp != NULL) {
			typeName = tmp;
			free(tmp);
		} else {
			typeName = typeid(*e).name();
		}
	#else
		typeName = typeid(*e).name();
	#endif

	SKC_WARN(client, "Cannot checkout session (exception type " <<
		typeName << "): " << e->what());

	const unsigned int exceptionMessageLen = strlen(e->what());
	string backtrace;
	boost::shared_ptr<tracable_exception> e3 = dynamic_pointer_cast<tracable_exception>(e);
	if (e3 != NULL) {
		backtrace = e3->backtrace();
	}

	const unsigned int BUFFER_SIZE = 512 + typeName.size() +
		exceptionMessageLen + backtrace.size();
	char *buf = (char *) psg_pnalloc(req->pool, BUFFER_SIZE);
	char *pos = buf;
	const char *end = buf + BUFFER_SIZE;

	pos = appendData(pos, end, "An internal error occurred while trying to spawn the application.\n");
	pos = appendData(pos, end, "Exception type: ");
	pos = appendData(pos, end, typeName);
	pos = appendData(pos, end, "\nError message: ");
	pos = appendData(pos, end, e->what(), exceptionMessageLen);
	if (!backtrace.empty()) {
		pos = appendData(pos, end, "\nBacktrace:\n");
		pos = appendData(pos, end, backtrace);
	}

	endRequestWithSimpleResponse(&client, &req, StaticString(buf, pos - buf), 500);
}

/**
 * `message` will be copied and doesn't need to outlive the request.
 */
void
endRequestWithErrorResponse(Client **c, Request **r, const StaticString &message,
	const SpawnException *e = NULL)
{
	TRACE_POINT();
	Client *client = *c;
	Request *req = *r;
	ErrorRenderer renderer(*resourceLocator);
	string data;

	if (friendlyErrorPagesEnabled(req)) {
		try {
			data = renderer.renderWithDetails(message, req->options, e);
		} catch (const SystemException &e2) {
			SKC_ERROR(client, "Cannot render an error page: " << e2.what() <<
				"\n" << e2.backtrace());
			data = message;
		}
	} else {
		try {
			data = renderer.renderWithoutDetails();
		} catch (const SystemException &e2) {
			SKC_ERROR(client, "Cannot render an error page: " << e2.what() <<
				"\n" << e2.backtrace());
			data = "Internal Server Error";
		}
	}

	endRequestWithSimpleResponse(c, r, psg_pstrdup(req->pool, data), 500);
}

bool
friendlyErrorPagesEnabled(Request *req) {
	bool defaultValue;
	string defaultStr = agentsOptions->get("friendly_error_pages");
	if (defaultStr == "auto") {
		defaultValue = req->options.environment != "staging"
			&& req->options.environment != "production";
	} else {
		defaultValue = defaultStr == "true";
	}
	return getBoolOption(req, "!~PASSENGER_FRIENDLY_ERROR_PAGES", defaultValue);
}

/***************/
