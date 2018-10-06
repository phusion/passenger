/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SPAWNING_KIT_BACKGROUND_IO_CAPTURER_H_
#define _PASSENGER_SPAWNING_KIT_BACKGROUND_IO_CAPTURER_H_

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/foreach.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/system_calls.hpp>
#include <string>
#include <vector>
#include <cstring>

#include <sys/types.h>

#include <LoggingKit/LoggingKit.h>
#include <FileDescriptor.h>
#include <StaticString.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;


/**
 * Given a file descriptor, captures its output in a background thread
 * and also forwards it immediately to a target file descriptor.
 * Call stop() to stop the background thread and to obtain the captured
 * output so far.
 */
class BackgroundIOCapturer {
private:
	const FileDescriptor fd;
	const pid_t pid;
	const string appGroupName;
	const string appLogFile;
	const StaticString channelName;
	mutable boost::mutex dataSyncher;
	string data;
	oxt::thread *thr;
	boost::function<void ()> endReachedCallback;
	bool stopped;

	void capture() {
		TRACE_POINT();
		while (!boost::this_thread::interruption_requested()) {
			char buf[1024 * 8];
			ssize_t ret;

			UPDATE_TRACE_POINT();
			ret = syscalls::read(fd, buf, sizeof(buf));
			int e = errno;
			boost::this_thread::disable_syscall_interruption dsi;
			if (ret == 0) {
				break;
			} else if (ret == -1) {
				if (e != EAGAIN && e != EWOULDBLOCK) {
					P_WARN("Background I/O capturer error: " <<
						strerror(e) << " (errno=" << e << ")");
					break;
				}
			} else {
				{
					boost::lock_guard<boost::mutex> l(dataSyncher);
					data.append(buf, ret);
				}
				UPDATE_TRACE_POINT();
				if (ret == 1 && buf[0] == '\n') {
					LoggingKit::logAppOutput(appGroupName, pid, channelName, "", 0, appLogFile);
				} else {
					vector<StaticString> lines;
					if (ret > 0 && buf[ret - 1] == '\n') {
						ret--;
					}
					split(StaticString(buf, ret), '\n', lines);
					foreach (const StaticString line, lines) {
						LoggingKit::logAppOutput(appGroupName, pid, channelName, line.data(), line.size(), appLogFile);
					}
				}
			}
		}

		{
			boost::lock_guard<boost::mutex> l(dataSyncher);
			stopped = true;
		}
		if (endReachedCallback) {
			endReachedCallback();
		}
	}

public:
	BackgroundIOCapturer(const FileDescriptor &_fd, pid_t _pid,
		const string &_appGroupName,
		const string &_appLogFile,
		const StaticString &_channelName = P_STATIC_STRING("output"),
		const StaticString &_data = StaticString())
		: fd(_fd),
		  pid(_pid),
		  appGroupName(_appGroupName),
		  appLogFile(_appLogFile),
		  channelName(_channelName),
		  data(_data.data(), _data.size()),
		  thr(NULL),
		  stopped(false)
		{ }

	~BackgroundIOCapturer() {
		TRACE_POINT();
		if (thr != NULL) {
			boost::this_thread::disable_interruption di;
			boost::this_thread::disable_syscall_interruption dsi;
			thr->interrupt_and_join();
			delete thr;
			thr = NULL;
		}
	}

	const FileDescriptor &getFd() const {
		return fd;
	}

	void start() {
		assert(thr == NULL);
		thr = new oxt::thread(boost::bind(&BackgroundIOCapturer::capture, this),
			"Background I/O capturer", 64 * 1024);
	}

	void stop() {
		TRACE_POINT();
		if (thr != NULL) {
			boost::this_thread::disable_interruption di;
			boost::this_thread::disable_syscall_interruption dsi;
			thr->interrupt_and_join();
			delete thr;
			thr = NULL;
		}
	}

	void setEndReachedCallback(const boost::function<void ()> &callback) {
		endReachedCallback = callback;
	}

	void appendToBuffer(const StaticString &dataToAdd) {
		TRACE_POINT();
		boost::lock_guard<boost::mutex> l(dataSyncher);
		data.append(dataToAdd.data(), dataToAdd.size());
	}

	string getData() const {
		boost::lock_guard<boost::mutex> l(dataSyncher);
		return data;
	}

	bool isStopped() const {
		boost::lock_guard<boost::mutex> l(dataSyncher);
		return stopped;
	}
};

typedef boost::shared_ptr<BackgroundIOCapturer> BackgroundIOCapturerPtr;


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_BACKGROUND_IO_CAPTURER_H_ */
