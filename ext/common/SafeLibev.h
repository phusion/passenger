/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010, 2011, 2012 Phusion
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
#include <list>
#include <memory>
#include <climits>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
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

	struct Command {
		int id;
		Callback callback;

		Command(unsigned int _id, const Callback &_callback)
			: id(_id),
			  callback(_callback)
			{ }
	};

	struct Timer {
		ev_timer realTimer;
		SafeLibev *self;
		Callback callback;
		list<Timer *>::iterator it;

		Timer(SafeLibev *_self, const Callback &_callback)
			: self(_self),
			  callback(_callback)
			{ }
	};
	
	struct ev_loop *loop;
	pthread_t loopThread;
	ev_async async;
	ev_idle idle;
	list<Timer *> timers;
	
	boost::mutex syncher;
	condition_variable cond;
	vector<Command> commands;
	unsigned int nextCommandId;
	
	static void asyncHandler(EV_P_ ev_async *w, int revents) {
		SafeLibev *self = (SafeLibev *) w->data;
		self->runCommands();
	}

	static void idleHandler(EV_P_ ev_idle *idle, int revents) {
		SafeLibev *self = (SafeLibev *) idle->data;
		self->runCommands();
	}

	static void timeoutHandler(EV_P_ ev_timer *t, int revents) {
		auto_ptr<Timer> timer((Timer *) ((const char *) t));
		SafeLibev *self = timer->self;
		self->timers.erase(timer->it);
		ev_timer_stop(self->loop, &timer->realTimer);
		timer->callback();
	}

	void runCommands() {
		unique_lock<boost::mutex> l(syncher);
		vector<Command> commands = this->commands;
		this->commands.clear();
		l.unlock();
		
		vector<Command>::const_iterator it, end = commands.end();
		for (it = commands.begin(); it != end; it++) {
			it->callback();
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
	
	void runAndNotify(const Callback *callback, bool *done) {
		(*callback)();
		unique_lock<boost::mutex> l(syncher);
		*done = true;
		cond.notify_all();
	}

	void incNextCommandId() {
		if (nextCommandId == INT_MAX) {
			nextCommandId = 0;
		} else {
			nextCommandId++;
		}
	}
	
public:
	/** SafeLibev takes over ownership of the loop object. */
	SafeLibev(struct ev_loop *loop) {
		this->loop = loop;
		loopThread = pthread_self();
		nextCommandId = 0;
		
		ev_async_init(&async, asyncHandler);
		async.data = this;
		ev_async_start(loop, &async);

		ev_idle_init(&idle, idleHandler);
		ev_set_priority(&idle, EV_MAXPRI);
		idle.data = this;
	}
	
	~SafeLibev() {
		destroy();
		ev_loop_destroy(loop);
	}

	void destroy() {
		ev_async_stop(loop, &async);
		ev_idle_stop(loop, &idle);

		list<Timer *>::iterator it, end = timers.end();
		for (it = timers.begin(); it != end; it++) {
			Timer *timer = *it;
			ev_timer_stop(loop, &timer->realTimer);
			delete timer;
		}
		timers.clear();
	}
	
	struct ev_loop *getLoop() const {
		return loop;
	}
	
	void setCurrentThread() {
		loopThread = pthread_self();
	}

	pthread_t getCurrentThread() const {
		return loopThread;
	}
	
	template<typename Watcher>
	void start(Watcher &watcher) {
		if (pthread_equal(pthread_self(), loopThread)) {
			watcher.set(loop);
			watcher.start();
		} else {
			unique_lock<boost::mutex> l(syncher);
			bool done = false;
			commands.push_back(Command(nextCommandId,
				boost::bind(&SafeLibev::startWatcherAndNotify<Watcher>,
					this, &watcher, &done)));
			incNextCommandId();
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
			commands.push_back(Command(nextCommandId,
				boost::bind(&SafeLibev::stopWatcherAndNotify<Watcher>,
					this, &watcher, &done)));
			incNextCommandId();
			ev_async_send(loop, &async);
			while (!done) {
				cond.wait(l);
			}
		}
	}
	
	void run(const Callback &callback) {
		assert(callback != NULL);
		if (pthread_equal(pthread_self(), loopThread)) {
			callback();
		} else {
			runSync(callback);
		}
	}

	void runSync(const Callback &callback) {
		assert(callback != NULL);
		unique_lock<boost::mutex> l(syncher);
		bool done = false;
		commands.push_back(Command(nextCommandId,
			boost::bind(&SafeLibev::runAndNotify, this,
				&callback, &done)));
		incNextCommandId();
		ev_async_send(loop, &async);
		while (!done) {
			cond.wait(l);
		}
	}

	void runAsync(const Callback &callback) {
		runLaterTS(callback);
	}

	// TODO: make it possible to call this from a thread
	void runAfter(unsigned int timeout, const Callback &callback) {
		assert(callback != NULL);
		Timer *timer = new Timer(this, callback);
		ev_timer_init(&timer->realTimer, timeoutHandler, timeout / 1000.0, 0);
		timers.push_front(timer);
		timer->it = timers.begin();
		ev_timer_start(loop, &timer->realTimer);
	}

	unsigned int runLater(const Callback &callback) {
		assert(callback != NULL);
		unsigned int result;
		{
			unique_lock<boost::mutex> l(syncher);
			commands.push_back(Command(nextCommandId, callback));
			result = nextCommandId;
			incNextCommandId();
		}
		if (!ev_is_active(&idle)) {
			ev_idle_start(loop, &idle);
		}
		return result;
	}
	
	unsigned int runLaterTS(const Callback &callback) {
		assert(callback != NULL);
		unsigned int result;
		{
			unique_lock<boost::mutex> l(syncher);
			commands.push_back(Command(nextCommandId, callback));
			result = nextCommandId;
			incNextCommandId();
		}
		ev_async_send(loop, &async);
		return result;
	}

	/**
	 * Cancels a callback that was scheduled to be run by runLater() and runLaterTS().
	 * Returns whether the command has been successfully cancelled or not.
	 * That is, a return value of true guarantees that the callback will not be called
	 * in the future, while a return value of false means that the callback has already
	 * been called or is currently being called.
	 */
	bool cancelCommand(int id) {
		unique_lock<boost::mutex> l(syncher);
		// TODO: we can do a binary search because the command ID
		// is monotically increasing except on overflow.
		vector<Command>::iterator it, end = commands.end();
		for (it = commands.begin(); it != end; it++) {
			if (it->id == id) {
				commands.erase(it);
				return true;
			}
		}
		return false;
	}
};

typedef shared_ptr<SafeLibev> SafeLibevPtr;


} // namespace Passenger

#endif /* _PASSENGER_SAFE_LIBEV_H_ */
