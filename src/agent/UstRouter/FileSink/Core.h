/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_FILE_SINK_CORE_H_
#define _PASSENGER_UST_ROUTER_FILE_SINK_CORE_H_

#include <oxt/system_calls.hpp>
#include <string>
#include <cstddef>
#include <cerrno>
#include <cstring>
#include <time.h>

#include <ev++.h>

#include <Logging.h>
#include <Utils/IOUtils.h>
#include <Utils/FastStringStream.h>
#include <Utils/ScopeGuard.h>
#include <UstRouter/Sink.h>

namespace Passenger {
namespace UstRouter {
namespace FileSink {

using namespace std;
using namespace oxt;


class Core: public Sink {
private:
	string directory;

	string determineFilename(const Transaction *transaction) const {
		string result(directory);
		result.append(1, '/');
		result.append(transaction->getCategory().data(),
			transaction->getCategory().size());
		return result;
	}

public:
	Core(struct ev_loop *loop, const string &_directory)
		: Sink(loop),
		  directory(_directory)
		{ }

	virtual void schedule(Transaction *transaction) {
		string filename = determineFilename(transaction);
		int fd = syscalls::open(filename.c_str(),
			O_CREAT | O_WRONLY | O_APPEND,
			0600);
		if (fd == -1) {
			int e = errno;
			P_WARN("Cannot open " << filename << " for appending: " <<
				strerror(e) << " (errno=" << e << ")");
		}

		FdGuard g(fd, __FILE__, __LINE__);
		FastStringStream<> preamble;
		time_t now = (time_t) ev_now(loop);
		char timeBuf[26];
		size_t timeStrLen;

		ctime_r(&now, timeBuf);
		timeStrLen = strlen(timeBuf);
		if (timeStrLen > 0) {
			// Remove trailing newline
			timeBuf[timeStrLen - 1] = '\0';
		}

		preamble << P_STATIC_STRING("-------- ") << timeBuf <<
			P_STATIC_STRING(" Transaction ") <<
			*transaction << P_STATIC_STRING(" --------\n");
		writeExact(fd, preamble.data(), preamble.size());
		writeExact(fd, transaction->getBody());
		writeExact(fd, P_STATIC_STRING("\n"));

		Sink::schedule(transaction);
		flush();
		delete transaction;
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc = Sink::inspectStateAsJson();
		doc["type"] = "file";
		doc["directory"] = directory;
		return doc;
	}
};


} // namespace FileSink
} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_FILE_SINK_CORE_H_ */
