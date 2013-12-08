/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2013 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL_PIPE_WATCHER_H_
#define _PASSENGER_APPLICATION_POOL_PIPE_WATCHER_H_

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread.hpp>
#include <boost/function.hpp>
#include <sys/types.h>
#include <FileDescriptor.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace boost;


/** A PipeWatcher lives until the file descriptor is closed. */
struct PipeWatcher: public boost::enable_shared_from_this<PipeWatcher> {
	// For unit tests.
	typedef boost::function<void (const char *data, unsigned int size)> DataCallback;
	static DataCallback onData;

	FileDescriptor fd;
	const char *name;
	pid_t pid;
	bool started;
	boost::mutex startSyncher;
	boost::condition_variable startCond;

	PipeWatcher(const FileDescriptor &_fd, const char *name, pid_t pid);
	void initialize();
	void start();
	static void threadMain(boost::shared_ptr<PipeWatcher> self);
	void threadMain();
};

typedef boost::shared_ptr<PipeWatcher> PipeWatcherPtr;


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_PIPE_WATCHER_H_ */
