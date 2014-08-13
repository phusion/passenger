private:

void
checkoutSession(Client *client, Request *req) {
	GetCallback callback;
	Options &options = req->options;

	SKC_TRACE(client, 2, "Checking out session: appRoot=" << options.appRoot);
	req->state = Request::CHECKING_OUT_SESSION;
	req->beginScopeLog(&req->scopeLogs.getFromPool, "get from pool");

	callback.func = sessionCheckedOut;
	callback.userData = req;

	options.currentTime = (unsigned long long) (ev_now(getLoop()) * 1000000);

	refRequest(req);
	pool->asyncGet(options, callback);
}

static void
sessionCheckedOut(const SessionPtr &session, const ExceptionPtr &e,
	void *userData)
{
	Request *req = static_cast<Request *>(userData);
	Client *client = static_cast<Client *>(req->client);
	RequestHandler *self = static_cast<RequestHandler *>(client->getServer());

	if (self->getContext()->libev->onEventLoopThread()) {
		self->sessionCheckedOutFromEventLoopThread(client, req, session, e);
		self->unrefRequest(req);
	} else {
		self->getContext()->libev->runLater(
			boost::bind(&RequestHandler::sessionCheckedOutFromAnotherThread,
				self, client, RequestRef(req), session, e));
		self->unrefRequest(req);
	}
}

void
sessionCheckedOutFromAnotherThread(Client *client, RequestRef req,
	SessionPtr session, ExceptionPtr e)
{
	sessionCheckedOutFromEventLoopThread(client, req.get(), session, e);
}

void
sessionCheckedOutFromEventLoopThread(Client *client, Request *req,
	const SessionPtr &session, const ExceptionPtr &e)
{
	if (e != NULL) {
		SKC_ERROR(client, e->what());
	}
	writeResponse(client,
		"HTTP/1.1 200 OK\r\n"
		"Content-Length: 3\r\n"
		"Content-Type: text/plain\r\n"
		"Connection: keep-alive\r\n"
		"\r\n"
		"ok\n"
	);
	endRequest(&client, &req);
}
