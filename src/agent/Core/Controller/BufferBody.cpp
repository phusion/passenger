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
 * Implements Core::Controller methods pertaining buffering the request
 * body.
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
Controller::beginBufferingBody(Client *client, Request *req) {
	TRACE_POINT();
	req->state = Request::BUFFERING_REQUEST_BODY;
	req->bodyChannel.start();
	req->bodyBuffer.reinitialize();
	req->bodyBuffer.stop();
}

/**
 * Relevant when our body data source (bodyChannel) was throttled (by whenBufferingBody_onRequestBody).
 * Called when our data sink (bodyBuffer) in-memory part is drained and ready for more data.
 */
void
Controller::_bodyBufferFlushed(FileBufferedChannel *channel) {
	Request *req = static_cast<Request *>(static_cast<
		ServerKit::BaseHttpRequest *>(channel->getHooks()->userData));

	req->bodyBuffer.clearBuffersFlushedCallback();
	req->bodyChannel.start();
}

/**
 * Receives data (buffer) originating from the bodyChannel, to be passed on to the bodyBuffer.
 * Backpressure is applied when the bodyBuffer in-memory part exceeds a threshold.
 */
ServerKit::Channel::Result
Controller::whenBufferingBody_onRequestBody(Client *client, Request *req,
	const MemoryKit::mbuf &buffer, int errcode)
{
	TRACE_POINT();

	if (buffer.size() > 0) {
		// Data
		req->bodyBytesBuffered += buffer.size();
		SKC_TRACE(client, 3, "Buffering " << buffer.size() <<
			" bytes of client request body: \"" <<
			cEscapeString(StaticString(buffer.start, buffer.size())) <<
			"\"; " << req->bodyBytesBuffered << " bytes buffered so far");
		req->bodyBuffer.feed(buffer);

		if (req->bodyBuffer.passedThreshold()) {
			// Apply backpressure..
			req->bodyChannel.stop();
			// ..until the in-memory part of our bodyBuffer is drained.
			assert(req->bodyBuffer.getBuffersFlushedCallback() == NULL);
			req->bodyBuffer.setBuffersFlushedCallback(_bodyBufferFlushed);
		}

		return Channel::Result(buffer.size(), false);
	} else if (errcode == 0 || errcode == ECONNRESET) {
		// EOF
		SKC_TRACE(client, 2, "End of request body encountered");
		req->bodyBuffer.feed(MemoryKit::mbuf());
		if (req->bodyType == Request::RBT_CHUNKED) {
			// The data that we've stored in the body buffer is dechunked, so when forwarding
			// the buffered body to the app we must advertise it as being a fixed-length,
			// non-chunked body.
			const unsigned int UINT64_STRSIZE = sizeof("18446744073709551615");
			SKC_TRACE(client, 2, "Adjusting forwarding headers as fixed-length, non-chunked");
			ServerKit::Header *header = (ServerKit::Header *)
				psg_palloc(req->pool, sizeof(ServerKit::Header));
			char *contentLength = (char *) psg_pnalloc(req->pool, UINT64_STRSIZE);
			unsigned int size = integerToOtherBase<boost::uint64_t, 10>(
				req->bodyBytesBuffered, contentLength, UINT64_STRSIZE);

			psg_lstr_init(&header->key);
			psg_lstr_append(&header->key, req->pool, "content-length",
				sizeof("content-length") - 1);
			psg_lstr_init(&header->origKey);
			psg_lstr_append(&header->origKey, req->pool, "Content-Length",
				sizeof("Content-Length") - 1);
			psg_lstr_init(&header->val);
			psg_lstr_append(&header->val, req->pool, contentLength, size);

			header->hash = HashedStaticString("content-length",
				sizeof("content-length") - 1).hash();

			req->headers.erase(HTTP_TRANSFER_ENCODING);
			req->headers.insert(&header, req->pool);
		}
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


} // namespace Core
} // namespace Passenger
