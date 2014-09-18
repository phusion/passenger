/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2014 Phusion
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

protected:

virtual void
onClientAccepted(Client *client) {
	ParentClass::onClientAccepted(client);
	client->connectedAt = ev_now(getLoop());
}

virtual void
onRequestObjectCreated(Client *client, Request *req) {
	ParentClass::onRequestObjectCreated(client, req);

	req->appInput.setContext(getContext());
	req->appInput.setHooks(&req->hooks);
	req->appInput.errorCallback = onAppInputError;

	req->appOutput.setContext(getContext());
	req->appOutput.setHooks(&req->hooks);
	req->appOutput.setDataCallback(_onAppOutputData);

	req->bodyBuffer.setContext(getContext());
	req->bodyBuffer.setHooks(&req->hooks);
	req->bodyBuffer.setDataCallback(onBodyBufferData);
}

virtual void deinitializeClient(Client *client) {
	ParentClass::deinitializeClient(client);
	client->output.setBuffersFlushedCallback(NULL);
}

virtual void reinitializeRequest(Client *client, Request *req) {
	ParentClass::reinitializeRequest(client, req);

	// bodyBuffer is initialized in RequestHandler::beginBufferingBody().
	// appOutput is initialized in RequestHandler::checkoutSession().

	req->startedAt = 0;
	req->state = Request::ANALYZING_REQUEST;
	req->dechunkResponse = false;
	req->requestBodyBuffering = false;
	req->https = false;
	req->stickySession = false;
	req->halfCloseAppConnection = false;
	req->sessionCheckoutTry = 0;
	req->host = NULL;
	req->appInput.reinitialize();
}

virtual void deinitializeRequest(Client *client, Request *req) {
	req->session.reset();

	req->endScopeLog(&req->scopeLogs.requestProxying, false);
	req->endScopeLog(&req->scopeLogs.getFromPool, false);
	req->endScopeLog(&req->scopeLogs.bufferingRequestBody, false);
	req->endScopeLog(&req->scopeLogs.requestProcessing, false);

	req->appInput.deinitialize();
	req->appInput.setBuffersFlushedCallback(NULL);
	req->appInput.setDataFlushedCallback(NULL);
	req->appOutput.deinitialize();
	req->bodyBuffer.deinitialize();

	deinitializeAppResponse(client, req);

	ParentClass::deinitializeRequest(client, req);
}

void reinitializeAppResponse(Client *client, Request *req) {
	AppResponse *resp = &req->appResponse;

	resp->httpMajor = 1;
	resp->httpMinor = 0;
	resp->httpState = AppResponse::PARSING_HEADERS;
	resp->bodyType  = AppResponse::RBT_NO_BODY;
	resp->wantKeepAlive = false;
	resp->oneHundredContinueSent = false;
	resp->hasDateHeader = false;
	resp->statusCode = 0;
	resp->parserState.headerParser = getHeaderParserStatePool().construct();
	createAppResponseHeaderParser(getContext(), req).initialize();
	resp->aux.bodyInfo.contentLength = 0; // Sets the entire union to 0.
	resp->bodyAlreadyRead = 0;
}

void deinitializeAppResponse(Client *client, Request *req) {
	AppResponse *resp = &req->appResponse;

	if (resp->httpState == AppResponse::PARSING_HEADERS
	 && resp->parserState.headerParser != NULL)
	{
		getHeaderParserStatePool().destroy(resp->parserState.headerParser);
		resp->parserState.headerParser = NULL;
	}

	ServerKit::HeaderTable::Iterator it(resp->headers);
	while (*it != NULL) {
		psg_lstr_deinit(&it->header->key);
		psg_lstr_deinit(&it->header->val);
		it.next();
	}

	it = ServerKit::HeaderTable::Iterator(resp->secureHeaders);
	while (*it != NULL) {
		psg_lstr_deinit(&it->header->key);
		psg_lstr_deinit(&it->header->val);
		it.next();
	}

	resp->headers.clear();
	resp->secureHeaders.clear();
}

virtual Channel::Result
onRequestBody(Client *client, Request *req, const MemoryKit::mbuf &buffer,
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

private:

static void
onAppInputError(FileBufferedFdOutputChannel *channel, int errcode) {
	Request *req = static_cast<Request *>(static_cast<
		ServerKit::BaseHttpRequest *>(channel->getHooks()->userData));
	Client *client = static_cast<Client *>(req->client);
	RequestHandler *self = static_cast<RequestHandler *>(getServerFromClient(client));

	switch (req->state) {
	case Request::BUFFERING_REQUEST_BODY:
		self->whenBufferingBody_onAppInputError(client, req, errcode);
		break;
	case Request::SENDING_HEADER_TO_APP:
	case Request::FORWARDING_BODY_TO_APP:
	case Request::WAITING_FOR_APP_OUTPUT:
		self->whenOtherCases_onAppInputError(client, req, errcode);
		break;
	default:
		P_BUG("Unknown state " << req->state);
		break;
	}
}

static Channel::Result
onBodyBufferData(Channel *_channel, const MemoryKit::mbuf &buffer, int errcode) {
	FileBufferedChannel *channel = reinterpret_cast<FileBufferedChannel *>(_channel);
	Request *req = static_cast<Request *>(static_cast<
		ServerKit::BaseHttpRequest *>(channel->getHooks()->userData));
	Client *client = static_cast<Client *>(req->client);
	RequestHandler *self = static_cast<RequestHandler *>(getServerFromClient(client));

	assert(req->requestBodyBuffering);
	return self->whenSendingRequest_onRequestBody(client, req, buffer, errcode);
}
