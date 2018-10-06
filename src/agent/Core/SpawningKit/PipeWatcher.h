/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SPAwNING_KIT_PIPE_WATCHER_H_
#define _PASSENGER_SPAwNING_KIT_PIPE_WATCHER_H_

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <oxt/thread.hpp>
#include <oxt/backtrace.hpp>
#include <string>
#include <vector>

#include <sys/types.h>

#include <FileDescriptor.h>
#include <Constants.h>
#include <LoggingKit/LoggingKit.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {
namespace SpawningKit {

using namespace boost;


/** A PipeWatcher lives until the file descriptor is closed. */
class PipeWatcher: public boost::enable_shared_from_this<PipeWatcher> {
private:
	FileDescriptor fd;
	StaticString name;
	string appGroupName;
	string appLogFile;
	pid_t pid;
	bool started;
	string logFile;
	boost::mutex startSyncher;
	boost::condition_variable startCond;

	static void threadMain(boost::shared_ptr<PipeWatcher> self) {
		TRACE_POINT();
		self->threadMain();
	}

	void threadMain() {
		TRACE_POINT();
		{
			boost::unique_lock<boost::mutex> lock(startSyncher);
			while (!started) {
				startCond.wait(lock);
			}
		}

		UPDATE_TRACE_POINT();
		FILE *f = NULL;
		if (!logFile.empty()) {
			f = fopen(logFile.c_str(), "a");
			if (f == NULL) {
				P_ERROR("Cannot open log file " << logFile);
				return;
			}
		}

		UPDATE_TRACE_POINT();
		while (!boost::this_thread::interruption_requested()) {
			char buf[1024 * 8];
			ssize_t ret;

			UPDATE_TRACE_POINT();
			ret = syscalls::read(fd, buf, sizeof(buf));
			if (ret == 0) {
				break;
			} else if (ret == -1) {
				UPDATE_TRACE_POINT();
				if (errno == ECONNRESET) {
					break;
				} else if (errno != EAGAIN) {
					int e = errno;
					P_WARN("Cannot read from process " << pid << " " << name <<
						": " << strerror(e) << " (errno=" << e << ")");
					break;
				}
			} else if (ret == 1 && buf[0] == '\n') {
				UPDATE_TRACE_POINT();
				printOrLogAppOutput(f, StaticString());
			} else {
				UPDATE_TRACE_POINT();
				vector<StaticString> lines;
				ssize_t ret2 = ret;
				if (ret2 > 0 && buf[ret2 - 1] == '\n') {
					ret2--;
				}
				split(StaticString(buf, ret2), '\n', lines);
				foreach (const StaticString line, lines) {
					printOrLogAppOutput(f, line);
				}
			}
		}

		if (f != NULL) {
			fclose(f);
		}
	}

	void printOrLogAppOutput(FILE *f, const StaticString &line) {
		if (f == NULL) {
			LoggingKit::logAppOutput(appGroupName, pid, name, line.data(), line.size(), appLogFile);
		} else {
			size_t ret = fwrite(line.data(), 1, line.size(), f);
			(void) ret; // Avoid compiler warning
			ret = fwrite("\n", 1, 2, f);
			(void) ret; // Avoid compiler warning
			fflush(f);
		}
	}

public:
	PipeWatcher(const FileDescriptor &_fd, const StaticString &_name,
		const string &_appGroupName, const string &_appLogFile,
		pid_t _pid)
		: fd(_fd),
		  name(_name),
		  appGroupName(_appGroupName),
		  appLogFile(_appLogFile),
		  pid(_pid),
		  started(false)
		{ }

	void setLogFile(const string &path) {
		logFile = path;
	}

	void initialize() {
		oxt::thread(boost::bind(threadMain, shared_from_this()),
			"PipeWatcher: PID " + toString(pid) + " " + name + ", fd " + toString(fd),
			POOL_HELPER_THREAD_STACK_SIZE);
	}

	void start() {
		boost::lock_guard<boost::mutex> lock(startSyncher);
		started = true;
		startCond.notify_all();
	}
};

typedef boost::shared_ptr<PipeWatcher> PipeWatcherPtr;


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAwNING_KIT_PIPE_WATCHER_H_ */
