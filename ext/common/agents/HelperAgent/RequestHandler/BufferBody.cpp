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

private:

void
beginBufferingBody(Client *client, Request *req) {
	TRACE_POINT();
	req->state = Request::BUFFERING_REQUEST_BODY;
	req->bodyChannel.start();
	req->bodyBuffer.reinitialize();
	req->bodyBuffer.stop();
}

Channel::Result
whenBufferingBody_onRequestBody(Client *client, Request *req,
	const MemoryKit::mbuf &buffer, int errcode)
{
	TRACE_POINT();

	if (buffer.size() > 0) {
		// Data
		SKC_TRACE(client, 3, "Buffering " << buffer.size() <<
			" bytes of client request body: \"" <<
			cEscapeString(StaticString(buffer.start, buffer.size())) <<
			"\"");
		req->bodyBuffer.feed(buffer);
		return Channel::Result(buffer.size(), false);
	} else if (errcode == 0 || errcode == ECONNRESET) {
		// EOF
		SKC_TRACE(client, 2, "End of request body encountered");
		req->bodyBuffer.feed(MemoryKit::mbuf());
		checkoutSession(client, req);
		return Channel::Result(0, true);
	} else {
		const unsigned int BUFSIZE = 1024;
		char *message = (char *) psg_pnalloc(req->pool, BUFSIZE);
		int size = snprintf(message, BUFSIZE,
			"error reading request body: %s (errno=%d)",
			ServerKit::getErrorDesc(errcode), errcode);
		disconnectWithError(&client, StaticString(message, size));
		return Channel::Result(0, true);
	}
}

void
whenBufferingBody_onAppSinkError(Client *client, Request *req, int errcode) {
	TRACE_POINT();
	assert(!req->responseBegun);
	SKC_WARN(client, "Cannot write to application socket: " <<
		ServerKit::getErrorDesc(errcode) << " (errcode=" << errcode << ")");
	endRequestAsBadGateway(&client, &req);
}
