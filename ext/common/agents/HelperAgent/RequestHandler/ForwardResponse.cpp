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
// It handles app --> client data forwarding.

private:

static Channel::Result
_onAppSourceData(Channel *_channel, const MemoryKit::mbuf &buffer, int errcode) {
	FdSourceChannel *channel = reinterpret_cast<FdSourceChannel *>(_channel);
	Request *req = static_cast<Request *>(static_cast<
		ServerKit::BaseHttpRequest *>(channel->getHooks()->userData));
	Client *client = static_cast<Client *>(req->client);
	RequestHandler *self = static_cast<RequestHandler *>(getServerFromClient(client));
	return self->onAppSourceData(client, req, buffer, errcode);
}

Channel::Result
onAppSourceData(Client *client, Request *req, const MemoryKit::mbuf &buffer, int errcode) {
	SKC_LOG_EVENT(RequestHandler, client, "onAppSourceData");
	AppResponse *resp = &req->appResponse;

	switch (resp->httpState) {
	case AppResponse::PARSING_HEADERS:
		if (buffer.size() > 0) {
			// Data
			size_t ret;
			SKC_TRACE(client, 3, "Processing " << buffer.size() <<
				" bytes of application data: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			{
				ret = createAppResponseHeaderParser(getContext(), req).
					feed(buffer);
			}
			if (resp->httpState == AppResponse::PARSING_HEADERS) {
				// Not yet done parsing.
				return Channel::Result(buffer.size(), false);
			}

			// Done parsing.
			SKC_TRACE(client, 2, "Application response headers received");
			getHeaderParserStatePool().destroy(resp->parserState.headerParser);
			resp->parserState.headerParser = NULL;

			switch (resp->httpState) {
			case AppResponse::COMPLETE:
				req->appSource.stop();
				onAppResponseBegin(client, req);
				return Channel::Result(ret, false);
			case AppResponse::PARSING_BODY_WITH_LENGTH:
			case AppResponse::PARSING_BODY_UNTIL_EOF:
				SKC_TRACE(client, 2,
					((resp->httpState == AppResponse::PARSING_BODY_WITH_LENGTH)
					? "Expecting an app response body with fixed length"
					: "Expecting app response body until end of stream"));
				onAppResponseBegin(client, req);
				return Channel::Result(ret, false);
			case AppResponse::PARSING_CHUNKED_BODY:
				SKC_TRACE(client, 2, "Expecting a chunked app response body");
				prepareAppResponseChunkedBodyParsing(client, req);
				onAppResponseBegin(client, req);
				return Channel::Result(ret, false);
			case AppResponse::UPGRADED:
				SKC_TRACE(client, 2, "Application upgraded connection");
				req->wantKeepAlive = false;
				onAppResponseBegin(client, req);
				return Channel::Result(ret, false);
			case AppResponse::ONEHUNDRED_CONTINUE:
				SKC_TRACE(client, 2, "Application sent 100-Continue status");
				onAppResponse100Continue(client, req);
				return Channel::Result(ret, false);
			case AppResponse::ERROR:
				SKC_ERROR(client, "Error parsing application response header: " <<
					ServerKit::getErrorDesc(resp->aux.parseError));
				endRequestAsBadGateway(&client, &req);
				return Channel::Result(0, true);
			default:
				P_BUG("Invalid response HTTP state " << (int) resp->httpState);
				return Channel::Result(0, true);
			}
		} else if (errcode == 0 || errcode == ECONNRESET) {
			// EOF
			SKC_DEBUG(client, "Application sent EOF before finishing response headers");
			endRequestWithAppSocketIncompleteResponse(&client, &req);
			return Channel::Result(0, true);
		} else {
			// Error
			SKC_DEBUG(client, "Application socket read error occurred before finishing response headers");
			endRequestWithAppSocketReadError(&client, &req, errcode);
			return Channel::Result(0, true);
		}

	case AppResponse::PARSING_BODY_WITH_LENGTH:
		if (buffer.size() > 0) {
			// Data
			boost::uint64_t maxRemaining, remaining;

			maxRemaining = resp->aux.bodyInfo.contentLength - resp->bodyAlreadyRead;
			remaining = std::min<boost::uint64_t>(buffer.size(), maxRemaining);
			resp->bodyAlreadyRead += remaining;

			SKC_TRACE(client, 3, "Processing " << buffer.size() <<
				" bytes of application data: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			SKC_TRACE(client, 3, "Application response body: " <<
				resp->bodyAlreadyRead << " of " <<
				resp->aux.bodyInfo.contentLength << " bytes already read");

			if (remaining > 0) {
				writeResponse(client, MemoryKit::mbuf(buffer, 0, remaining));
				if (!req->ended() && resp->bodyFullyRead()) {
					SKC_TRACE(client, 2, "End of application response body reached");
					endRequest(&client, &req);
				}
			} else {
				SKC_TRACE(client, 2, "End of application response body reached");
				endRequest(&client, &req);
			}
			return Channel::Result(remaining, false);
		} else if (errcode == 0 || errcode == ECONNRESET) {
			// EOF
			if (resp->bodyFullyRead()) {
				SKC_TRACE(client, 2, "Application sent EOF");
				endRequest(&client, &req);
			} else {
				SKC_WARN(client, "Application sent EOF before finishing response body: " <<
					resp->bodyAlreadyRead << " bytes already read, " <<
					resp->aux.bodyInfo.contentLength << " bytes expected");
				endRequestWithAppSocketIncompleteResponse(&client, &req);
			}
			return Channel::Result(0, true);
		} else {
			// Error
			endRequestWithAppSocketReadError(&client, &req, errcode);
			return Channel::Result(0, true);
		}

	case AppResponse::PARSING_CHUNKED_BODY:
		if (!buffer.empty()) {
			// Data
			SKC_TRACE(client, 3, "Processing " << buffer.size() <<
				" bytes of application data: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			ServerKit::HttpChunkedEvent event(createAppResponseChunkedBodyParser(req)
				.feed(buffer));
			resp->bodyAlreadyRead += event.consumed;

			if (req->dechunkResponse) {
				switch (event.type) {
				case ServerKit::HttpChunkedEvent::NONE:
					assert(!event.end);
					return Channel::Result(event.consumed, false);
				case ServerKit::HttpChunkedEvent::DATA:
					assert(!event.end);
					writeResponse(client, event.data);
					return Channel::Result(event.consumed, false);
				case ServerKit::HttpChunkedEvent::END:
					assert(event.end);
					resp->aux.bodyInfo.endReached = true;
					endRequest(&client, &req);
					return Channel::Result(event.consumed, true);
				case ServerKit::HttpChunkedEvent::ERROR:
					assert(event.end);
					{
						string message = "error parsing app response chunked encoding: ";
						message.append(ServerKit::getErrorDesc(event.errcode));
						disconnectWithError(&client, message);
					}
					return Channel::Result(event.consumed, true);
				}
			} else {
				switch (event.type) {
				case ServerKit::HttpChunkedEvent::NONE:
				case ServerKit::HttpChunkedEvent::DATA:
					assert(!event.end);
					writeResponse(client, MemoryKit::mbuf(buffer, 0, event.consumed));
					return Channel::Result(event.consumed, false);
				case ServerKit::HttpChunkedEvent::END:
					assert(event.end);
					resp->aux.bodyInfo.endReached = true;
					writeResponse(client, MemoryKit::mbuf(buffer, 0, event.consumed));
					if (!req->ended()) {
						endRequest(&client, &req);
					}
					return Channel::Result(event.consumed, true);
				case ServerKit::HttpChunkedEvent::ERROR:
					assert(event.end);
					{
						string message = "error parsing app response chunked encoding: ";
						message.append(ServerKit::getErrorDesc(event.errcode));
						disconnectWithError(&client, message);
					}
					return Channel::Result(event.consumed, true);
				}
			}
		} else if (errcode == 0 || errcode == ECONNRESET) {
			// Premature EOF. This cannot be an expected EOF because
			// we end the request upon consuming the end of the chunked body.
			disconnectWithError(&client, "error parsing app response chunked encoding: "
				"unexpected end-of-stream");
			return Channel::Result(0, false);
		} else {
			// Error
			endRequestWithAppSocketReadError(&client, &req, errcode);
			return Channel::Result(0, true);
		}

	case AppResponse::PARSING_BODY_UNTIL_EOF:
	case AppResponse::UPGRADED:
		if (buffer.size() > 0) {
			// Data
			SKC_TRACE(client, 3, "Processing " << buffer.size() <<
				" bytes of application data: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			resp->bodyAlreadyRead += buffer.size();
			writeResponse(client, buffer);
			return Channel::Result(buffer.size(), false);
		} else if (errcode == 0 || errcode == ECONNRESET) {
			// EOF
			SKC_TRACE(client, 2, "Application sent EOF");
			endRequest(&client, &req);
			return Channel::Result(0, false);
		} else {
			// Error
			endRequestWithAppSocketReadError(&client, &req, errcode);
			return Channel::Result(0, false);
		}

	default:
		P_BUG("Invalid request HTTP state " << (int) resp->httpState);
		return Channel::Result(0, false);
	}
}

void
onAppResponseBegin(Client *client, Request *req) {
	AppResponse *resp = &req->appResponse;
	ssize_t bytesWritten;
	bool oobw;

	// Localize hash table operations for better CPU caching.
	oobw = resp->secureHeaders.lookup(PASSENGER_REQUEST_OOB_WORK) != NULL;
	resp->hasDateHeader = resp->headers.lookup(HTTP_DATE) != NULL;
	resp->headers.erase(HTTP_CONNECTION);
	resp->headers.erase(HTTP_STATUS);
	if (req->dechunkResponse && resp->bodyType == AppResponse::RBT_CHUNKED) {
		resp->headers.erase(HTTP_TRANSFER_ENCODING);
		req->wantKeepAlive = false;
	}

	if (OXT_UNLIKELY(oobw)) {
		SKC_TRACE(client, 2, "Response with OOBW detected");
		if (req->session != NULL) {
			req->session->requestOOBW();
		}
	}

	if (!sendResponseHeaderWithWritev(client, req, bytesWritten)) {
		if (bytesWritten >= 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
			sendResponseHeaderWithBuffering(client, req, bytesWritten);
		} else {
			int e = errno;
			P_ASSERT_EQ(bytesWritten, -1);
			disconnectWithClientSocketWriteError(&client, e);
		}
	}

	if (!req->ended() && !resp->hasBody()) {
		endRequest(&client, &req);
	}
}

void
onAppResponse100Continue(Client *client, Request *req) {
	if (!req->strip100ContinueHeader) {
		const unsigned int BUFSIZE = 32;
		char *buf = (char *) psg_pnalloc(req->pool, BUFSIZE);
		int size = snprintf(buf, BUFSIZE, "HTTP/%d.%d 100 Continue\r\n",
			(int) req->httpMajor, (int) req->httpMinor);
		writeResponse(client, buf, size);
	}
	if (!req->ended()) {
		deinitializeAppResponse(client, req);
		reinitializeAppResponse(client, req);
		req->appResponse.oneHundredContinueSent = !req->strip100ContinueHeader;
		// Allow sending more response headers.
		req->responseBegun = false;
	}
}

/**
 * Construct an array of buffers, which together contain the HTTP response
 * data that should be sent to the client. This method does not copy any data:
 * it just constructs buffers that point to the data stored inside `req->pool`,
 * `req->appResponse.headers`, etc.
 *
 * The buffers will be stored in the array pointed to by `buffer`. This array must
 * have space for at least `maxbuffers` items. The actual number of buffers constructed
 * is stored in `nbuffers`, and the total data size of the buffers is stored in `dataSize`.
 * Upon success, returns true. If the actual number of buffers necessary exceeds
 * `maxbuffers`, then false is returned.
 *
 * You can also set `buffers` to NULL, in which case this method will not construct any
 * buffers, but only count the number of buffers necessary, as well as the total data size.
 * In this case, this method always returns true.
 */
bool
constructHeaderBuffersForResponse(Request *req, struct iovec *buffers,
	unsigned int maxbuffers, unsigned int & restrict_ref nbuffers,
	unsigned int & restrict_ref dataSize)
{
	#define INC_BUFFER_ITER(i) \
		do { \
			if (buffers != NULL && i == maxbuffers) { \
				return false; \
			} \
			i++; \
		} while (false)
	#define PUSH_STATIC_BUFFER(str) \
		do { \
			if (buffers != NULL) { \
				buffers[i].iov_base = (void *) str; \
				buffers[i].iov_len  = sizeof(str) - 1; \
			} \
			INC_BUFFER_ITER(i); \
			dataSize += sizeof(str) - 1; \
		} while (false)

	AppResponse *resp = &req->appResponse;
	ServerKit::HeaderTable::Iterator it(resp->headers);
	const LString::Part *part;
	const char *statusAndReason;
	unsigned int i = 0;

	nbuffers = 0;
	dataSize = 0;

	PUSH_STATIC_BUFFER("HTTP/");

	if (buffers != NULL) {
		const unsigned int BUFSIZE = 16;
		char *buf = (char *) psg_pnalloc(req->pool, BUFSIZE);
		const char *end = buf + BUFSIZE;
		char *pos = buf;
		pos += uintToString(req->httpMajor, pos, end - pos);
		pos = appendData(pos, end, ".", 1);
		pos += uintToString(req->httpMinor, pos, end - pos);
		buffers[i].iov_base = (void *) buf;
		buffers[i].iov_len  = pos - buf;
		dataSize += pos - buf;
	} else {
		char buf[16];
		const char *end = buf + sizeof(buf);
		char *pos = buf;
		pos += uintToString(req->httpMajor, pos, end - pos);
		pos = appendData(pos, end, ".", 1);
		pos += uintToString(req->httpMinor, pos, end - pos);
		dataSize += pos - buf;
	}
	INC_BUFFER_ITER(i);

	PUSH_STATIC_BUFFER(" ");

	statusAndReason = getStatusCodeAndReasonPhrase(resp->statusCode);
	if (statusAndReason != NULL) {
		size_t len = strlen(statusAndReason);
		if (buffers != NULL) {
			buffers[i].iov_base = (void *) statusAndReason;
			buffers[i].iov_len  = len;
		}
		INC_BUFFER_ITER(i);
		dataSize += len;

		PUSH_STATIC_BUFFER("\r\nStatus: ");
		if (buffers != NULL) {
			buffers[i].iov_base = (void *) statusAndReason;
			buffers[i].iov_len  = len;
		}
		INC_BUFFER_ITER(i);
		dataSize += len;

		PUSH_STATIC_BUFFER("\r\n");
	} else {
		if (buffers != NULL) {
			const unsigned int BUFSIZE = 8;
			char *buf = (char *) psg_pnalloc(req->pool, BUFSIZE);
			const char *end = buf + BUFSIZE;
			char *pos = buf;
			unsigned int size = uintToString(resp->statusCode, pos, end - pos);
			buffers[i].iov_base = (void *) buf;
			buffers[i].iov_len  = size;
			INC_BUFFER_ITER(i);
			dataSize += size;

			PUSH_STATIC_BUFFER(" Unknown Reason-Phrase\r\nStatus: ");
			buffers[i].iov_base = (void *) buf;
			buffers[i].iov_len  = size;
			INC_BUFFER_ITER(i);
			dataSize += size;

			PUSH_STATIC_BUFFER("\r\n");
		} else {
			char buf[8];
			const char *end = buf + sizeof(buf);
			char *pos = buf;
			unsigned int size = uintToString(resp->statusCode, pos, end - pos);
			INC_BUFFER_ITER(i);
			dataSize += size;

			dataSize += sizeof(" Unknown Reason-Phrase\r\nStatus: ") - 1;
			INC_BUFFER_ITER(i);
			dataSize += size;
			INC_BUFFER_ITER(i);
			dataSize += sizeof("\r\n");
			INC_BUFFER_ITER(i);
		}
	}

	while (*it != NULL) {
		dataSize += it->header->key.size + sizeof(": ") - 1;
		dataSize += it->header->val.size + sizeof("\r\n") - 1;

		part = it->header->key.start;
		while (part != NULL) {
			if (buffers != NULL) {
				buffers[i].iov_base = (void *) part->data;
				buffers[i].iov_len  = part->size;
			}
			INC_BUFFER_ITER(i);
			part = part->next;
		}
		if (buffers != NULL) {
			buffers[i].iov_base = (void *) ": ";
			buffers[i].iov_len  = sizeof(": ") - 1;
		}
		INC_BUFFER_ITER(i);

		part = it->header->val.start;
		while (part != NULL) {
			if (buffers != NULL) {
				buffers[i].iov_base = (void *) part->data;
				buffers[i].iov_len  = part->size;
			}
			INC_BUFFER_ITER(i);
			part = part->next;
		}
		if (buffers != NULL) {
			buffers[i].iov_base = (void *) "\r\n";
			buffers[i].iov_len  = sizeof("\r\n") - 1;
		}
		INC_BUFFER_ITER(i);

		it.next();
	}

	// Add Date header. https://code.google.com/p/phusion-passenger/issues/detail?id=485
	if (!resp->hasDateHeader) {
		unsigned int size;

		if (buffers != NULL) {
			const unsigned int BUFSIZE = 60;
			char *dateStr = (char *) psg_pnalloc(req->pool, BUFSIZE);
			size = constructDateHeaderBuffersForResponse(dateStr, BUFSIZE);
			buffers[i].iov_base = dateStr;
			buffers[i].iov_len  = size;
		} else {
			char dateStr[60];
			size = constructDateHeaderBuffersForResponse(dateStr, sizeof(dateStr));
		}
		INC_BUFFER_ITER(i);
		dataSize += size;

		PUSH_STATIC_BUFFER("\r\n");
	}

	if (resp->bodyType == AppResponse::RBT_UPGRADE) {
		PUSH_STATIC_BUFFER("Connection: upgrade\r\n");
	} else if (canKeepAlive(req)) {
		unsigned int httpVersion = req->httpMajor * 1000 + req->httpMinor * 10;
		if (httpVersion < 1010) {
			// HTTP < 1.1 defaults to "Connection: close"
			PUSH_STATIC_BUFFER("Connection: keep-alive\r\n");
		}
	} else {
		unsigned int httpVersion = req->httpMajor * 1000 + req->httpMinor * 10;
		if (httpVersion >= 1010) {
			// HTTP 1.1 defaults to "Connection: keep-alive"
			PUSH_STATIC_BUFFER("Connection: close\r\n");
		}
	}

	if (showVersionInHeader) {
		PUSH_STATIC_BUFFER("X-Powered-By: " PROGRAM_NAME " " PASSENGER_VERSION "\r\n\r\n");
	} else {
		PUSH_STATIC_BUFFER("X-Powered-By: " PROGRAM_NAME "\r\n\r\n");
	}

	nbuffers = i;
	return true;

	#undef INC_BUFFER_ITER
	#undef PUSH_STATIC_BUFFER
}

unsigned int
constructDateHeaderBuffersForResponse(char *dateStr, unsigned int bufsize) {
	char *pos = dateStr;
	const char *end = dateStr + bufsize - 1;
	time_t the_time = (time_t) ev_now(getContext()->libev->getLoop());
	struct tm the_tm;

	pos = appendData(pos, end, "Date: ");
	gmtime_r(&the_time, &the_tm);
	pos += strftime(pos, end - pos, "%a, %d %b %Y %H:%M:%S %z", &the_tm);
	return pos - dateStr;
}

bool
sendResponseHeaderWithWritev(Client *client, Request *req, ssize_t &bytesWritten) {
	unsigned int maxbuffers = std::min<unsigned int>(
		8 + req->appResponse.headers.size() * 4 + 8, IOV_MAX);
	struct iovec *buffers = (struct iovec *) psg_palloc(req->pool,
		sizeof(struct iovec) * maxbuffers);
	unsigned int nbuffers, dataSize;

	if (constructHeaderBuffersForResponse(req, buffers,
		maxbuffers, nbuffers, dataSize))
	{
		ssize_t ret;
		do {
			ret = writev(client->getFd(), buffers, nbuffers);
		} while (ret == -1 && errno == EINTR);
		bytesWritten = ret;
		req->responseBegun |= ret > 0;
		return ret == (ssize_t) dataSize;
	} else {
		bytesWritten = 0;
		return false;
	}
}

void
sendResponseHeaderWithBuffering(Client *client, Request *req, unsigned int offset) {
	struct iovec *buffers;
	unsigned int nbuffers, dataSize;
	bool ok;

	ok = constructHeaderBuffersForResponse(req, NULL, 0, nbuffers, dataSize);
	assert(ok);

	buffers = (struct iovec *) psg_palloc(req->pool,
		sizeof(struct iovec) * nbuffers);
	ok = constructHeaderBuffersForResponse(req, buffers, nbuffers,
		nbuffers, dataSize);
	assert(ok);
	(void) ok; // Shut up compiler warning

	MemoryKit::mbuf_pool &mbuf_pool = getContext()->mbuf_pool;
	const unsigned int MBUF_MAX_SIZE = mbuf_pool.mbuf_block_chunk_size -
		mbuf_pool.mbuf_block_offset;
	if (dataSize <= MBUF_MAX_SIZE) {
		MemoryKit::mbuf buffer(MemoryKit::mbuf_get(&mbuf_pool));
		gatherBuffers(buffer.start, MBUF_MAX_SIZE, buffers, nbuffers);
		buffer = MemoryKit::mbuf(buffer, offset, dataSize - offset);
		writeResponse(client, buffer);
	} else {
		char *buffer = (char *) psg_pnalloc(req->pool, dataSize);
		gatherBuffers(buffer, dataSize, buffers, nbuffers);
		writeResponse(client, buffer + offset, dataSize - offset);
	}
}

static ServerKit::HttpHeaderParser<AppResponse, ServerKit::HttpParseResponse>
createAppResponseHeaderParser(ServerKit::Context *ctx, Request *req) {
	return ServerKit::HttpHeaderParser<AppResponse, ServerKit::HttpParseResponse>(
		ctx, req->appResponse.parserState.headerParser,
		&req->appResponse, req->pool, req->method);
}

static ServerKit::HttpChunkedBodyParser
createAppResponseChunkedBodyParser(Request *req) {
	return ServerKit::HttpChunkedBodyParser(
		&req->appResponse.parserState.chunkedBodyParser,
		formatAppResponseChunkedBodyParserLoggingPrefix,
		req);
}

static unsigned int formatAppResponseChunkedBodyParserLoggingPrefix(char *buf,
	unsigned int bufsize, void *userData)
{
	Request *req = static_cast<Request *>(userData);
	return snprintf(buf, bufsize,
		"[Client %u] ChunkedBodyParser: ",
		static_cast<Client *>(req->client)->number);
}

void prepareAppResponseChunkedBodyParsing(Client *client, Request *req) {
	P_ASSERT_EQ(req->appResponse.bodyType, AppResponse::RBT_CHUNKED);
	createAppResponseChunkedBodyParser(req).initialize();
}
