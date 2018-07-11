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

/*************************************************************************
 *
 * Hook functions for Core::Controller. This pertains the hooks that the
 * parent classes (ServerKit::HttpServer and ServerKit::Server) provide,
 * as well as hooks by libraries such as libev.
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


ServerKit::Channel::Result
Controller::onBodyBufferData(Channel *_channel, const MemoryKit::mbuf &buffer, int errcode) {
	FileBufferedChannel *channel = reinterpret_cast<FileBufferedChannel *>(_channel);
	Request *req = static_cast<Request *>(static_cast<
		ServerKit::BaseHttpRequest *>(channel->getHooks()->userData));
	Client *client = static_cast<Client *>(req->client);
	Controller *self = static_cast<Controller *>(getServerFromClient(client));
	SKC_LOG_EVENT_FROM_STATIC(self, Controller, client, "onBodyBufferData");

	assert(req->requestBodyBuffering);
	return self->whenSendingRequest_onRequestBody(client, req, buffer, errcode);
}

#ifdef DEBUG_CC_EVENT_LOOP_BLOCKING
	void
	Controller::onEventLoopPrepare(EV_P_ struct ev_prepare *w, int revents) {
		Controller *self = static_cast<Controller *>(w->data);
		ev_now_update(EV_A);
		self->timeBeforeBlocking = ev_now(EV_A);
	}
#endif

void
Controller::onEventLoopCheck(EV_P_ struct ev_check *w, int revents) {
	Controller *self = static_cast<Controller *>(w->data);
	self->turboCaching.updateState(ev_now(EV_A));
	#ifdef DEBUG_CC_EVENT_LOOP_BLOCKING
		self->reportLargeTimeDiff(NULL, "Event loop slept",
			self->timeBeforeBlocking, ev_now(EV_A));
	#endif
}


/****************************
 *
 * Protected methods
 *
 ****************************/


void
Controller::onClientAccepted(Client *client) {
	ParentClass::onClientAccepted(client);
	client->connectedAt = ev_now(getLoop());
}

void
Controller::onRequestObjectCreated(Client *client, Request *req) {
	ParentClass::onRequestObjectCreated(client, req);

	req->appSink.setContext(getContext());
	req->appSink.setHooks(&req->hooks);

	req->appSource.setContext(getContext());
	req->appSource.setHooks(&req->hooks);
	req->appSource.setDataCallback(_onAppSourceData);

	req->bodyBuffer.setContext(getContext());
	req->bodyBuffer.setHooks(&req->hooks);
	req->bodyBuffer.setDataCallback(onBodyBufferData);
}

void
Controller::deinitializeClient(Client *client) {
	ParentClass::deinitializeClient(client);
	client->output.clearBuffersFlushedCallback();
	client->output.setDataFlushedCallback(getClientOutputDataFlushedCallback());
}

void
Controller::reinitializeRequest(Client *client, Request *req) {
	ParentClass::reinitializeRequest(client, req);

	// bodyBuffer is initialized in Controller::beginBufferingBody().
	// appSink and appSource are initialized in Controller::checkoutSession().

	req->startedAt = 0;
	req->state = Request::ANALYZING_REQUEST;
	req->dechunkResponse = false;
	req->requestBodyBuffering = false;
	req->https = false;
	req->stickySession = false;
	req->sessionCheckoutTry = 0;
	req->halfClosePolicy = Request::HALF_CLOSE_POLICY_UNINITIALIZED;
	req->appResponseInitialized = false;
	req->strip100ContinueHeader = false;
	req->hasPragmaHeader = false;
	req->host = NULL;
	req->config = requestConfig;
	req->bodyBytesBuffered = 0;
	req->cacheKey = HashedStaticString();
	req->cacheControl = NULL;
	req->varyCookie = NULL;
	req->envvars = NULL;

	#ifdef DEBUG_CC_EVENT_LOOP_BLOCKING
		req->timedAppPoolGet = false;
		req->timeBeforeAccessingApplicationPool = 0;
		req->timeOnRequestHeaderSent = 0;
		req->timeOnResponseBegun = 0;
	#endif

	/***************/
}

void
Controller::deinitializeRequest(Client *client, Request *req) {
	req->session.reset();
	req->config.reset();

	req->appSink.setConsumedCallback(NULL);
	req->appSink.deinitialize();
	req->appSource.deinitialize();
	req->bodyBuffer.clearBuffersFlushedCallback();
	req->bodyBuffer.deinitialize();

	/***************/
	/***************/

	if (req->appResponseInitialized) {
		deinitializeAppResponse(client, req);
	}

	ParentClass::deinitializeRequest(client, req);
}

void
Controller::reinitializeAppResponse(Client *client, Request *req) {
	AppResponse *resp = &req->appResponse;

	req->appResponseInitialized = true;

	resp->httpMajor = 1;
	resp->httpMinor = 0;
	resp->httpState = AppResponse::PARSING_HEADERS;
	resp->bodyType  = AppResponse::RBT_NO_BODY;
	resp->wantKeepAlive = false;
	resp->oneHundredContinueSent = false;
	resp->statusCode = 0;
	resp->parserState.headerParser = getHeaderParserStatePool().construct();
	createAppResponseHeaderParser(getContext(), req).initialize();
	resp->aux.bodyInfo.contentLength = 0; // Sets the entire union to 0.
	resp->bodyAlreadyRead = 0;
	resp->date = NULL;
	resp->setCookie = NULL;
	resp->cacheControl = NULL;
	resp->expiresHeader = NULL;
	resp->lastModifiedHeader = NULL;

	resp->headerCacheBuffers = NULL;
	resp->nHeaderCacheBuffers = 0;
	psg_lstr_init(&resp->bodyCacheBuffer);
}

void
Controller::deinitializeAppResponse(Client *client, Request *req) {
	AppResponse *resp = &req->appResponse;

	req->appResponseInitialized = false;

	if (resp->httpState == AppResponse::PARSING_HEADERS
	 && resp->parserState.headerParser != NULL)
	{
		getHeaderParserStatePool().destroy(resp->parserState.headerParser);
		resp->parserState.headerParser = NULL;
	}

	ServerKit::HeaderTable::Iterator it(resp->headers);
	while (*it != NULL) {
		psg_lstr_deinit(&it->header->key);
		psg_lstr_deinit(&it->header->origKey);
		psg_lstr_deinit(&it->header->val);
		it.next();
	}

	it = ServerKit::HeaderTable::Iterator(resp->secureHeaders);
	while (*it != NULL) {
		psg_lstr_deinit(&it->header->key);
		psg_lstr_deinit(&it->header->origKey);
		psg_lstr_deinit(&it->header->val);
		it.next();
	}

	resp->headers.clear();
	resp->secureHeaders.clear();

	if (resp->setCookie != NULL) {
		psg_lstr_deinit(resp->setCookie);
	}
	psg_lstr_deinit(&resp->bodyCacheBuffer);
}

ServerKit::Channel::Result
Controller::onRequestBody(Client *client, Request *req, const MemoryKit::mbuf &buffer,
	int errcode)
{
	switch (req->state) {
	case Request::BUFFERING_REQUEST_BODY:
		return whenBufferingBody_onRequestBody(client, req, buffer, errcode);
	case Request::FORWARDING_BODY_TO_APP:
		return whenSendingRequest_onRequestBody(client, req, buffer, errcode);
	default:
		P_BUG("Unknown state " << req->state);
		return Channel::Result(0, false);
	}
}

void
Controller::onNextRequestEarlyReadError(Client *client, Request *req, int errcode) {
	ParentClass::onNextRequestEarlyReadError(client, req, errcode);
	if (req->halfClosePolicy == Request::HALF_CLOSE_UPON_NEXT_REQUEST_EARLY_READ_ERROR) {
		SKC_TRACE(client, 3, "Half-closing application socket with SHUT_WR"
			" because the next request's early read error has been detected: "
			<< ServerKit::getErrorDesc(errcode) << " (errno=" << errcode << ")");
		req->halfClosePolicy = Request::HALF_CLOSE_PERFORMED;
		assert(req->session != NULL);
		::shutdown(req->session->fd(), SHUT_WR);
	}
}

bool
Controller::shouldDisconnectClientOnShutdown(Client *client) {
	return ParentClass::shouldDisconnectClientOnShutdown(client)
		|| !mainConfig.gracefulExit;
}

bool
Controller::shouldAutoDechunkBody(Client *client, Request *req) {
	// When buffering the body, we'll want to buffer the dechunked data,
	// (and when passing the request to the app we'll also add Content-Length
	// and remove Transfer-Encoding) so turn auto-dechunking on in that case.
	//
	// Otherwise we'll want to disable auto-dechunking because we'll
	// pass the raw chunked body to the app.
	return req->requestBodyBuffering;
}

bool
Controller::supportsUpgrade(Client *client, Request *req) {
	return true;
}


/****************************
 *
 * Public methods
 *
 ****************************/


unsigned int
Controller::getClientName(const Client *client, char *buf, size_t size) const {
	char *pos = buf;
	const char *end = buf + size - 1;
	// WARNING: If you change the format, be sure to change
	// ApiServer::extractThreadNumberFromClientName() too.
	pos += uintToString(mainConfig.threadNumber, pos, end - pos);
	pos = appendData(pos, end, "-", 1);
	pos += uintToString(client->number, pos, end - pos);
	*pos = '\0';
	return pos - buf;
}

StaticString
Controller::getServerName() const {
	return mainConfig.serverLogName;
}


} // namespace Core
} // namespace Passenger
