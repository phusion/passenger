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
#ifndef _PASSENGER_REQUEST_HANDLER_REQUEST_H_
#define _PASSENGER_REQUEST_HANDLER_REQUEST_H_

#include <ev++.h>
#include <string>

#include <ServerKit/HttpRequest.h>
#include <ServerKit/FileBufferedFdOutputChannel.h>
#include <ServerKit/FdInputChannel.h>
#include <ApplicationPool2/Pool.h>
#include <UnionStation/Core.h>
#include <UnionStation/Transaction.h>
#include <UnionStation/ScopeLog.h>
#include <agents/HelperAgent/RequestHandler/AppResponse.h>
#include <Logging.h>
#include <Utils/HttpHeaderBufferer.h>
#include <Utils/Dechunker.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace ApplicationPool2;


class Request: public ServerKit::BaseHttpRequest {
public:
	/***** RequestHandler <-> Application I/O channels, pipes and watchers *****/

	ServerKit::FileBufferedFdOutputChannel appInput;
	ServerKit::FdInputChannel appOutput;
	AppResponse appResponse;


	/***** State variables *****/

	enum {
		ANALYZING_REQUEST,
		CHECKING_OUT_SESSION,
		SENDING_HEADER_TO_APP,
		FORWARDING_BODY_TO_APP,
		WAITING_FOR_APP_OUTPUT
	} state;

	ev_tstamp startedAt;
	bool halfCloseAppConnection;

	Options options;
	SessionPtr session;
	struct {
		UnionStation::ScopeLog *requestProcessing;
		UnionStation::ScopeLog *bufferingRequestBody;
		UnionStation::ScopeLog *getFromPool;
		UnionStation::ScopeLog *requestProxying;
	} scopeLogs;
	unsigned int sessionCheckoutTry;

	bool sessionCheckedOut;
	bool stickySession;
	bool responseHeaderSeen;
	bool chunkedResponse;
	/** The size of the response body, set based on the values of
	 * the Content-Length and Transfer-Encoding response headers.
	 * Possible values:
	 *
	 * -1: infinite. Should keep forwarding response body until end of stream.
	 *     This is the case for WebSockets or for responses without Content-Length.
	 *     Responses with "Transfer-Encoding: chunked" also fall under this
	 *     category, though in this case encountering the zero-length chunk is
	 *     treated the same as end of stream.
	 * 0 : no client body. Should immediately close connection after forwarding
	 *     headers.
	 * >0: Should forward exactly this many bytes of the response body.
	 */
	long long responseContentLength;
	unsigned long long responseBodyAlreadyRead;
	HttpHeaderBufferer responseHeaderBufferer;
	Dechunker responseDechunker;


	const char *getStateString() const {
		switch (state) {
		case ANALYZING_REQUEST:
			return "ANALYZING_REQUEST";
		case CHECKING_OUT_SESSION:
			return "CHECKING_OUT_SESSION";
		case SENDING_HEADER_TO_APP:
			return "SENDING_HEADER_TO_APP";
		case FORWARDING_BODY_TO_APP:
			return "FORWARDING_BODY_TO_APP";
		case WAITING_FOR_APP_OUTPUT:
			return "WAITING_FOR_APP_OUTPUT";
		}
	}

	/**
	 * Checks whether we should half-close the application socket after forwarding
	 * the request. HTTP does not formally support half-closing, and Node.js treats a
	 * half-close as a full close, so we only half-close session sockets, not
	 * HTTP sockets.
	 */
	bool shouldHalfCloseWrite() const {
		return session->getProtocol() == "session";
	}

	bool useUnionStation() const {
		return options.transaction != NULL;
	}

	UnionStation::TransactionPtr getUnionStationTransaction() const {
		return options.transaction;
	}

	void beginScopeLog(UnionStation::ScopeLog **scopeLog, const char *name) {
		if (options.transaction != NULL) {
			*scopeLog = new UnionStation::ScopeLog(options.transaction, name);
		}
	}

	void endScopeLog(UnionStation::ScopeLog **scopeLog, bool success = true) {
		if (success && *scopeLog != NULL) {
			(*scopeLog)->success();
		}
		delete *scopeLog;
		*scopeLog = NULL;
	}

	void logMessage(const StaticString &message) {
		options.transaction->message(message);
	}

	void inspect(ostream &stream) const {
		//const char *indent = "    ";

		//stream << indent << "host                        = " << (scgiParser.getHeader("HTTP_HOST").empty() ? "(empty)" : scgiParser.getHeader("HTTP_HOST")) << "\n";
		//stream << indent << "uri                         = " << (scgiParser.getHeader("REQUEST_URI").empty() ? "(empty)" : scgiParser.getHeader("REQUEST_URI")) << "\n";
		//stream << indent << "connected at                = " << timestr << " (" << (unsigned long long) (ev_time() - connectedAt) << " sec ago)\n";
		//stream << indent << "state                       = " << getStateName() << "\n";
		/*if (session == NULL) {
			stream << indent << "session                     = NULL\n";
		} else {
			stream << indent << "session pid                 = " << session->getPid() << " (" <<
				session->getGroup()->name << ")\n";
			stream << indent << "session gupid               = " << session->getGupid() << "\n";
			stream << indent << "session initiated           = " << boolStr(session->initiated()) << "\n";
		}
		stream
			<< indent << "requestBodyIsBuffered       = " << boolStr(requestBodyIsBuffered) << "\n"
			<< indent << "requestIsChunked            = " << boolStr(requestIsChunked) << "\n"
			<< indent << "requestBodyLength           = " << requestBodyLength << "\n"
			<< indent << "requestBodyAlreadyRead      = " << requestBodyAlreadyRead << "\n"
			<< indent << "responseContentLength       = " << responseContentLength << "\n"
			<< indent << "responseBodyAlreadyRead     = " << responseBodyAlreadyRead << "\n"
			<< indent << "clientBodyBuffer started    = " << boolStr(clientBodyBuffer->isStarted()) << "\n"
			<< indent << "clientBodyBuffer reachedEnd = " << boolStr(clientBodyBuffer->reachedEnd()) << "\n"
			<< indent << "clientOutputPipe started    = " << boolStr(clientOutputPipe->isStarted()) << "\n"
			<< indent << "clientOutputPipe reachedEnd = " << boolStr(clientOutputPipe->reachedEnd()) << "\n"
			<< indent << "clientOutputWatcher active  = " << boolStr(clientOutputWatcher.is_active()) << "\n"
			<< indent << "appInput                    = " << appInput.get() << " " << appInput->inspect() << "\n"
			<< indent << "appInput started            = " << boolStr(appInput->isStarted()) << "\n"
			<< indent << "appInput reachedEnd         = " << boolStr(appInput->endReached()) << "\n"
			<< indent << "responseHeaderSeen          = " << boolStr(responseHeaderSeen) << "\n"
			<< indent << "useUnionStation             = " << boolStr(useUnionStation()) << "\n"
			;
		*/
	}

	DEFINE_SERVER_KIT_BASE_HTTP_REQUEST_FOOTER(Request);
};


} // namespace Passenger

#endif /* _PASSENGER_REQUEST_HANDLER_REQUEST_H_ */
