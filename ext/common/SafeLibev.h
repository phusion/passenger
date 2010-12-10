/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_SAFE_LIBEV_H_
#define _PASSENGER_SAFE_LIBEV_H_

#include <ev++.h>
#include <vector>
#include <boost/thread.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>

namespace Passenger {

using namespace std;
using namespace boost;


/**
 * Class for thread-safely using libev.
 */
class SafeLibev {
private:
	typedef function<void ()> Callback;
	
	struct ev_loop *loop;
	pthread_t loopThread;
	ev_async async;
	
	boost::mutex syncher;
	condition_variable cond;
	vector<Callback> commands;
	
	static void asyncHandler(EV_P_ ev_async *w, int revents) {
		SafeLibev *self = (SafeLibev *) w->data;
		unique_lock<boost::mutex> l(self->syncher);
		vector<Callback> commands = self->commands;
		self->commands.clear();
		l.unlock();
		
		vector<Callback>::const_iterator it, end = commands.end();
		for (it = commands.begin(); it != commands.end(); it++) {
			(*it)();
		}
	}
	
	template<typename Watcher>
	void startWatcherAndNotify(Watcher *watcher, bool *done) {
		watcher->set(loop);
		watcher->start();
		unique_lock<boost::mutex> l(syncher);
		*done = true;
		cond.notify_all();
	}
	
	template<typename Watcher>
	void stopWatcherAndNotify(Watcher *watcher, bool *done) {
		watcher->stop();
		unique_lock<boost::mutex> l(syncher);
		*done = true;
		cond.notify_all();
	}
	
public:
	SafeLibev(struct ev_loop *loop) {
		this->loop = loop;
		loopThread = pthread_self();
		ev_async_init(&async, asyncHandler);
		async.data = this;
		ev_async_start(loop, &async);
	}
	
	~SafeLibev() {
		ev_async_stop(loop, &async);
	}
	
	struct ev_loop *getLoop() const {
		return loop;
	}
	
	template<typename Watcher>
	void start(Watcher &watcher) {
		if (pthread_equal(pthread_self(), loopThread)) {
			watcher.set(loop);
			watcher.start();
		} else {
			unique_lock<boost::mutex> l(syncher);
			bool done = false;
			commands.push_back(boost::bind(&SafeLibev::startWatcherAndNotify<Watcher>,
				this, &watcher, &done));
			ev_async_send(loop, &async);
			while (!done) {
				cond.wait(l);
			}
		}
	}
	
	template<typename Watcher>
	void stop(Watcher &watcher) {
		if (pthread_equal(pthread_self(), loopThread)) {
			watcher.stop();
		} else {
			unique_lock<boost::mutex> l(syncher);
			bool done = false;
			commands.push_back(boost::bind(&SafeLibev::stopWatcherAndNotify<Watcher>,
				this, &watcher, &done));
			ev_async_send(loop, &async);
			while (!done) {
				cond.wait(l);
			}
		}
	}
	
	void run(const Callback &callback) {
		if (pthread_equal(pthread_self(), loopThread)) {
			callback();
		} else {
			unique_lock<boost::mutex> l(syncher);
			commands.push_back(callback);
			ev_async_send(loop, &async);
		}
	}
};


} // namespace Passenger

#endif /* _PASSENGER_SAFE_LIBEV_H_ */
