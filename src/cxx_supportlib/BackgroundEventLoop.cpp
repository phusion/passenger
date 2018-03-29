/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2017 Phusion Holding B.V.
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
#include <cstdlib>
#include <cerrno>
#include <cassert>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/scoped_ptr.hpp>
#include <oxt/thread.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/detail/context.hpp>
#include <ev.h>
#include <uv.h>
#include <BackgroundEventLoop.h>
#include <LoggingKit/LoggingKit.h>
#include <Exceptions.h>
#include <SafeLibev.h>

#ifndef HAVE_KQUEUE
	#if defined(__APPLE__) || \
		defined(__DragonFly__) || \
		defined(__FreeBSD__) || \
		defined(__FreeBSD_kernel__) || \
		defined(__OpenBSD__) || \
		defined(__NetBSD__)
		#define HAVE_KQUEUE 1
	#endif
#endif

#ifndef HAVE_EPOLL
	#ifdef __linux__
		#define HAVE_EPOLL 1
	#endif
#endif

#ifndef HAVE_POLLSET
	#ifdef __AIX
		#define HAVE_POLLSET 1
	#endif
#endif

#ifndef HAVE_EVENT_PORTS
	#if defined(sun) || defined(__sun)
		#define HAVE_EVENT_PORTS
	#endif
#endif

#if defined(HAVE_KQUEUE)
	#include <sys/types.h>
	#include <sys/event.h>
	#include <sys/time.h>
#elif defined(HAVE_EPOLL)
	#include <sys/epoll.h>
#elif defined(HAVE_POLLSET)
	#include <sys/poll.h>
	#include <sys/pollset.h>
	#include <sys/fcntl.h>
#elif defined(HAVE_EVENT_PORTS)
	#include <port.h>
#endif


namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


struct BackgroundEventLoopPrivate {
	struct ev_async exitSignaller;
	struct ev_async libuvActivitySignaller;
	uv_loop_t libuv_loop;
	/**
	 * Coordinates communication between the libuv poller thread and the
	 * libuv activity callback (the latter which runs on the libevent thread.
	 */
	uv_sem_t libuv_sem;
	/**
	 * This timer doesn't do anything. It only exists to prevent
	 * uv_backend_timeout() from returning 0, which would make the
	 * libuv poller thread use 100% CPU.
	 */
	uv_timer_t libuv_timer;

	oxt::thread *thr;
	oxt::thread *libuvPollerThr;
	uv_barrier_t startBarrier;

	bool usesLibuv;
	bool started;
};


static void
signalLibevExit(struct ev_loop *loop, ev_async *async, int revents) {
	BackgroundEventLoop *bg = (BackgroundEventLoop *) async->data;
	if (bg->priv->usesLibuv) {
		ev_async_stop(bg->libev_loop, &bg->priv->libuvActivitySignaller);
	}
	ev_async_stop(bg->libev_loop, &bg->priv->exitSignaller);
	ev_break(bg->libev_loop, EVBREAK_ALL);
	if (bg->priv->usesLibuv) {
		uv_timer_stop(&bg->priv->libuv_timer);
		uv_run(bg->libuv_loop, UV_RUN_NOWAIT);
	}
}

static void
onLibuvActivity(struct ev_loop *loop, ev_async *async, int revents) {
	BackgroundEventLoop *bg = (BackgroundEventLoop *) async->data;
	uv_run(bg->libuv_loop, UV_RUN_NOWAIT);
	uv_sem_post(&bg->priv->libuv_sem);
}

static void
doNothing(uv_timer_t *timer) {
	// Do nothing
}

static void
runBackgroundLoop(BackgroundEventLoop *bg) {
	bg->safe->setCurrentThread();
	if (bg->priv->usesLibuv) {
		uv_timer_start(&bg->priv->libuv_timer, doNothing, 99999000, 99999000);
		uv_run(bg->libuv_loop, UV_RUN_NOWAIT);
	}
	uv_barrier_wait(&bg->priv->startBarrier);
	ev_run(bg->libev_loop, 0);
}

static void
pollLibuv(BackgroundEventLoop *bg) {
	uv_barrier_wait(&bg->priv->startBarrier);

	int ret;
	int fd;
	int timeout;
	int lastErrno;
	bool intrRequested = false;
	oxt::thread_local_context *ctx = oxt::get_thread_local_context();
	assert(ctx != NULL);

	fd = uv_backend_fd(&bg->priv->libuv_loop);

	while (!boost::this_thread::interruption_requested()) {
		timeout = uv_backend_timeout(&bg->priv->libuv_loop);

		ctx->syscall_interruption_lock.unlock();

		do {
			#if defined(HAVE_KQUEUE)
				struct timespec ts;
				struct kevent event;

				ts.tv_sec = timeout / 1000;
				ts.tv_nsec = (timeout % 1000) * 1000000;

				ret = kevent(fd, NULL, 0, &event, 1, (timeout == -1) ? NULL : &ts);
			#elif defined(HAVE_EPOLL)
				struct epoll_event ev;
				ret = epoll_wait(fd, &ev, 1, timeout);
			#elif defined(HAVE_POLLSET)
				struct pollfd event;
				ret = pollset_poll(fd, &event, 1, timeout);
			#elif defined(HAVE_EVENT_PORTS)
				struct timespec ts;
				struct port_event event;

				ts.tv_sec = timeout / 1000;
				ts.tv_nsec = (timeout % 1000) * 1000000;

				ret = port_get(fd, &event, (timeout == -1) ? NULL : &ts);
			#else
				#error "This platform is not supported. Please add corresponding I/O polling code."
			#endif

			lastErrno = errno;
		} while (ret == -1
			&& lastErrno == EINTR
			&& (!boost::this_thread::syscalls_interruptable()
				|| !(intrRequested = boost::this_thread::interruption_requested())));

		ctx->syscall_interruption_lock.lock();

		if (ret == -1
			&& lastErrno == EINTR
			&& boost::this_thread::syscalls_interruptable()
			&& intrRequested)
		{
			throw boost::thread_interrupted();
		}

		ev_async_send(bg->libev_loop, &bg->priv->libuvActivitySignaller);
		uv_sem_wait(&bg->priv->libuv_sem);
	}
}

BackgroundEventLoop::BackgroundEventLoop(bool scalable, bool usesLibuv)
	: libev_loop(NULL),
	  libuv_loop(NULL),
	  priv(NULL)
{
	struct Guard {
		BackgroundEventLoop *self;

		Guard(BackgroundEventLoop *_self)
			: self(_self)
			{ }

		~Guard() {
			if (self != NULL) {
				if (self->libev_loop != NULL) {
					ev_loop_destroy(self->libev_loop);
				}
				if (self->libuv_loop != NULL) {
					uv_loop_close(self->libuv_loop);
				}
				delete self->priv;
			}
		}

		void clear() {
			self = NULL;
		}
	};

	TRACE_POINT();
	Guard guard(this);

	priv = new BackgroundEventLoopPrivate();

	if (scalable) {
		libev_loop = ev_loop_new(EVBACKEND_KQUEUE);
		if (libev_loop == NULL) {
			libev_loop = ev_loop_new(EVBACKEND_EPOLL);
		}
		if (libev_loop == NULL) {
			libev_loop = ev_loop_new(EVFLAG_AUTO);
		}
	} else {
		libev_loop = ev_loop_new(EVBACKEND_POLL);
	}
	if (libev_loop == NULL) {
		throw RuntimeException("Cannot create a libev event loop");
	}

	P_LOG_FILE_DESCRIPTOR_OPEN2(ev_backend_fd(libev_loop), "libev event loop: backend FD");

	ev_async_init(&priv->exitSignaller, signalLibevExit);
	P_LOG_FILE_DESCRIPTOR_OPEN2(ev_loop_get_pipe(libev_loop, 0), "libev event loop: async pipe 0");
	P_LOG_FILE_DESCRIPTOR_OPEN2(ev_loop_get_pipe(libev_loop, 1), "libev event loop: async pipe 1");
	priv->exitSignaller.data = this;
	safe = boost::make_shared<SafeLibev>(libev_loop);

	uv_barrier_init(&priv->startBarrier, usesLibuv ? 3 : 2);

	if (usesLibuv) {
		ev_async_init(&priv->libuvActivitySignaller, onLibuvActivity);
		priv->libuvActivitySignaller.data = this;

		libuv_loop = &priv->libuv_loop;
		uv_loop_init(&priv->libuv_loop);
		uv_timer_init(&priv->libuv_loop, &priv->libuv_timer);
		uv_sem_init(&priv->libuv_sem, 0);
		P_LOG_FILE_DESCRIPTOR_OPEN2(uv_backend_fd(libuv_loop), "libuv event loop: backend");
		P_LOG_FILE_DESCRIPTOR_OPEN2(libuv_loop->signal_pipefd[0], "libuv event loop: signal pipe 0");
		P_LOG_FILE_DESCRIPTOR_OPEN2(libuv_loop->signal_pipefd[1], "libuv event loop: signal pipe 1");
	}

	priv->thr = NULL;
	priv->libuvPollerThr = NULL;
	priv->usesLibuv = usesLibuv;
	priv->started = false;
	guard.clear();
}

BackgroundEventLoop::~BackgroundEventLoop() {
	stop();
	if (priv->usesLibuv) {
		uv_close((uv_handle_t *) &priv->libuv_timer, NULL);
		while (uv_loop_alive(libuv_loop)) {
			uv_run(libuv_loop, UV_RUN_NOWAIT);
			syscalls::usleep(10000);
		}
		uv_sem_destroy(&priv->libuv_sem);
		P_LOG_FILE_DESCRIPTOR_CLOSE(uv_backend_fd(libuv_loop));
		P_LOG_FILE_DESCRIPTOR_CLOSE(libuv_loop->signal_pipefd[0]);
		P_LOG_FILE_DESCRIPTOR_CLOSE(libuv_loop->signal_pipefd[1]);
		uv_loop_close(libuv_loop);
		if (ev_is_active(&priv->libuvActivitySignaller)) {
			ev_async_stop(libev_loop, &priv->libuvActivitySignaller);
		}
	}
	if (ev_is_active(&priv->exitSignaller)) {
		ev_async_stop(libev_loop, &priv->exitSignaller);
	}
	uv_barrier_destroy(&priv->startBarrier);
	delete priv;
}

void
BackgroundEventLoop::start(const string &threadName, unsigned int stackSize) {
	assert(priv->thr == NULL);
	ev_async_start(libev_loop, &priv->exitSignaller);
	if (priv->usesLibuv) {
		ev_async_start(libev_loop, &priv->libuvActivitySignaller);
	}
	priv->thr = new oxt::thread(
		boost::bind(runBackgroundLoop, this),
		threadName,
		stackSize
	);
	if (priv->usesLibuv) {
		priv->libuvPollerThr = new oxt::thread(
			boost::bind(pollLibuv, this),
			threadName + ": libuv poller",
			1024 * 512
		);
	}
	uv_barrier_wait(&priv->startBarrier);
}

void
BackgroundEventLoop::stop() {
	if (priv->thr != NULL) {
		if (priv->usesLibuv) {
			priv->libuvPollerThr->interrupt_and_join();
			delete priv->libuvPollerThr;
			priv->libuvPollerThr = NULL;
		}
		ev_async_send(libev_loop, &priv->exitSignaller);
		priv->thr->join();
		delete priv->thr;
		priv->thr = NULL;
	}
}

bool
BackgroundEventLoop::isStarted() const {
	return priv->thr != NULL;
}

pthread_t
BackgroundEventLoop::getNativeHandle() const {
	return priv->thr->native_handle();
}


} // namespace Passenger
