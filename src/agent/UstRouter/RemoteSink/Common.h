/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_COMMON_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_COMMON_H_

#include <ev.h>
#include <curl/curl.h>

#include <Integrations/CurlLibevIntegration.h>
#include <UstRouter/RemoteSink/Segment.h>
#include <UstRouter/RemoteSink/Server.h>

namespace Passenger {
namespace UstRouter {
namespace RemoteSink {


class Context {
public:
	struct ev_loop * const loop;
	CURLM *curlMulti;
	CurlLibevIntegration curlLibevIntegration;

	Context(struct ev_loop *_loop)
		: loop(_loop)
	{
		curlMulti = curl_multi_init();
		curl_multi_setopt(curlMulti, CURLMOPT_PIPELINING, (long) CURLPIPE_MULTIPLEX);
		curlLibevIntegration.initialize(_loop, curlMulti);
	}

	~Context() {
		curlLibevIntegration.destroy();
		curl_multi_cleanup(curlMulti);
	}
};

class SegmentProcessor {
public:
	virtual ~SegmentProcessor() { }
	virtual void schedule(SegmentList &segments) = 0;
};

class AbstractServerLivelinessChecker {
public:
	virtual void registerServers(const Segment::SmallServerList &servers) = 0;
	~AbstractServerLivelinessChecker() { }
};


} // namespace Passenger
} // namespace UstRouter
} // namespace RemoteSink

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_COMMON_H_ */
