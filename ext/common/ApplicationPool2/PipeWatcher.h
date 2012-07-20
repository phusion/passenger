/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2012 Phusion
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
#include <SafeLibev.h>
#include <FileDescriptor.h>
#include <ev++.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace boost;


struct PipeWatcher: public enable_shared_from_this<PipeWatcher> {
	SafeLibevPtr libev;
	FileDescriptor fd;
	ev::io watcher;
	shared_ptr<PipeWatcher> selfPointer;
	int fdToForwardTo;

	PipeWatcher(const SafeLibevPtr &_libev,
		const FileDescriptor &_fd,
		int _fdToForwardTo);
	~PipeWatcher();
	void start();
	void onReadable(ev::io &io, int revents);
};

typedef shared_ptr<PipeWatcher> PipeWatcherPtr;


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_PIPE_WATCHER_H_ */
