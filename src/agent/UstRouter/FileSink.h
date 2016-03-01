/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_FILE_SINK_H_
#define _PASSENGER_UST_ROUTER_FILE_SINK_H_

#include <string>
#include <ctime>
#include <oxt/system_calls.hpp>
#include <Exceptions.h>
#include <FileDescriptor.h>
#include <UstRouter/LogSink.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {
namespace UstRouter {

using namespace std;
using namespace oxt;


class FileSink: public LogSink {
public:
	string filename;
	FileDescriptor fd;

	FileSink(Controller *controller, const string &_filename)
		: LogSink(controller),
		  filename(_filename)
	{
		fd.assign(syscalls::open(_filename.c_str(),
			O_CREAT | O_WRONLY | O_APPEND,
			0600), __FILE__, __LINE__);
		if (fd == -1) {
			int e = errno;
			throw FileSystemException("Cannnot open file '" +
				filename + "' for appending", e, filename);
		}
	}

	virtual void append(const TransactionPtr &transaction) {
		StaticString data = transaction->getBody();
		LogSink::append(transaction);
		syscalls::write(fd, data.data(), data.size());
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc = LogSink::inspectStateAsJson();
		doc["type"] = "file";
		doc["filename"] = filename;
		return doc;
	}

	string inspect() const {
		return "FileSink(" + filename + ")";
	}
};


} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_FILE_SINK_H_ */
