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
#ifndef _PASSENGER_CURL_LIBEV_INTEGRATION_H_
#define _PASSENGER_CURL_LIBEV_INTEGRATION_H_

#include <ev.h>
#include <curl/curl.h>
#include <cstddef>
#include <cstring>
#include <psg_sysqueue.h>
#include <Logging.h>

namespace Passenger {

using namespace std;


/**
 * Provides curl-multi + libev integration. Use this class as follows:
 *
 * 1. Create a CurlLibevIntegration object per curl-multi handle.
 * 2. Use curl-multi and curl-easy as normal, but make sure that you attach
 *    a CurlLibevIntegration::TransferInfo-derived object to each curl-easy
 *    handle through CURLINFO_PRIVATE.
 * 3. Whenever a transfer is completed, the finish() method on the attached
 *    TransferInfo object is called. So make sure you override the finish()
 *    method to perform your own logic.
 *    CurlLibevIntegration removes the easy handle from the multi handle (using
 *    curl_multi_remove_handle), but does not cleanup easy handles. You must do
 *    that yourself, e.g. inside finish().
 * 4. When you cleanup a curl-easy handle, make sure you destroy the attached
 *    TransferInfo object too.
 *
 * ## Example
 *
 *     class MyTransferInfo: public CurlLibevIntegration::TransferInfo {
 *     public:
 *         virtual void finish(CURL *curl, CURLcode code) {
 *             printf("Transfer complete!\n");
 *             curl_easy_cleanup(curl);
 *         }
 *     };
 *
 *     CURLM *multi = curl_multi_init();
 *     CurlLibevIntegration integration(loop, multi);
 *
 *     CURL *curl = curl_easy_init();
 *     curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, new MyTransferInfo());
 *     ...
 *     curl_multi_add_handle(multi, curl);
 *
 *     ev_loop(loop, 0);
 *     integration.destroy();
 *     curl_multi_cleanup(multi);
 */
class CurlLibevIntegration {
public:
	class TransferInfo {
	public:
		virtual ~TransferInfo() {}
		virtual void finish(CURL *curl, CURLcode code) = 0;
	};

private:
	struct SocketInfo {
		struct ev_io io;
		STAILQ_ENTRY(SocketInfo) next;

		SocketInfo() {
			memset(&io, 0, sizeof(io));
			STAILQ_NEXT(this, next) = NULL;
		}
	};

	STAILQ_HEAD(SocketInfoList, SocketInfo);

	struct ev_loop *loop;
	CURLM *multi;
	SocketInfoList socketInfos;
	struct ev_timer timer;

	static int onCurlSocketActivity(CURL *curl, curl_socket_t sock, int action,
		void *callbackData, void *socketData)
	{
		CurlLibevIntegration *self = static_cast<CurlLibevIntegration *>(callbackData);
		SocketInfo *socketInfo = (SocketInfo *) socketData;

		if (action == CURL_POLL_REMOVE) {
			// Not sure whether socketInfo can ever be NULL,
			// but check just to be sure.
			if (socketInfo != NULL) {
				self->removeSocket(socketInfo);
			}
		} else if (socketInfo == NULL) {
			self->addSocket(sock, curl, action);
		} else {
			assert(socketInfo != NULL);
			self->changeSocket(socketInfo, sock, curl, action);
		}
		return 0;
	}

	static void onEvSocketActivity(EV_P_ struct ev_io *w, int revents) {
		CurlLibevIntegration *self = static_cast<CurlLibevIntegration *>(w->data);
		CURLMcode ret;
		int stillRunning;

		int action = (revents & EV_READ ? CURL_POLL_IN : 0)
			| (revents & EV_WRITE ? CURL_POLL_OUT : 0);
		ret = curl_multi_socket_action(self->multi, w->fd, action,
			&stillRunning);
		if (ret != CURLM_OK) {
			P_ERROR("Error notifying libcurl of socket event: " <<
				curl_multi_strerror(ret) << " (errno=" << ret << ")");
		}
		self->processCompletedTransfers();
		if (stillRunning <= 0 && ev_is_active(&self->timer)) {
			// Last transfer has completed, so stop any active timeout.
			ev_timer_stop(self->loop, &self->timer);
		}
	}

	void addSocket(curl_socket_t sock, CURL *curl, int action) {
		SocketInfo *socketInfo = new SocketInfo();
		CURLMcode ret = curl_multi_assign(multi, sock, socketInfo);
		if (ret == CURLM_OK) {
			STAILQ_INSERT_TAIL(&socketInfos, socketInfo, next);
			changeSocket(socketInfo, sock, curl, action);
		} else {
			delete socketInfo;
			P_ERROR("Error assigning private pointer to a libcurl multi socket object: " <<
				curl_multi_strerror(ret) << " (errno=" << ret << ")");
		}
	}

	void changeSocket(SocketInfo *socketInfo, curl_socket_t sock, CURL *curl, int action) {
		int evAction = (action & CURL_POLL_IN ? EV_READ : 0)
			| (action & CURL_POLL_OUT ? EV_WRITE : 0);
		if (ev_is_active(&socketInfo->io)) {
			ev_io_stop(loop, &socketInfo->io);
		}
		ev_io_init(&socketInfo->io, onEvSocketActivity, sock, evAction);
		socketInfo->io.data = this;
		ev_io_start(loop, &socketInfo->io);
	}

	void removeSocket(SocketInfo *socketInfo) {
		if (ev_is_active(&socketInfo->io)) {
			ev_io_stop(loop, &socketInfo->io);
		}
		STAILQ_REMOVE(&socketInfos, socketInfo, SocketInfo, next);
		delete socketInfo;
	}

	static int onCurlTimerActivity(CURLM *multi, long timeout_ms, void *userData) {
		CurlLibevIntegration *self = static_cast<CurlLibevIntegration *>(userData);

		if (ev_is_active(&self->timer)) {
			ev_timer_stop(self->loop, &self->timer);
		}
		if (timeout_ms > 0) {
			ev_tstamp t = timeout_ms / 1000;
			ev_timer_init(&self->timer, onEvTimeout, t, 0);
			self->timer.data = self;
			ev_timer_start(self->loop, &self->timer);
		} else {
			onEvTimeout(self->loop, &self->timer, 0);
		}
		return 0;
	}

	static void onEvTimeout(EV_P_ struct ev_timer *w, int revents) {
		CurlLibevIntegration *self = static_cast<CurlLibevIntegration *>(w->data);
		CURLMcode ret;
		int stillRunning;

		ret = curl_multi_socket_action(self->multi, CURL_SOCKET_TIMEOUT, 0,
			&stillRunning);
		if (ret != CURLM_OK) {
			P_ERROR("Error notifying libcurl of timeout event: " <<
				curl_multi_strerror(ret) << " (errno=" << ret << ")");
		}
		self->processCompletedTransfers();
	}

	void processCompletedTransfers() {
		CURLMsg *msg;
		CURL *curl;
		void *priv;
		int msgsLeft;

		while ((msg = curl_multi_info_read(multi, &msgsLeft))) {
			if (msg->msg == CURLMSG_DONE) {
				curl = msg->easy_handle;
				curl_multi_remove_handle(multi, curl);
				curl_easy_getinfo(curl, CURLINFO_PRIVATE, &priv);
				if (priv == NULL) {
					curl_easy_cleanup(curl);
				} else {
					TransferInfo *info = static_cast<TransferInfo *>(priv);
					info->finish(curl, msg->data.result);
				}
			}
		}
	}

public:
	CurlLibevIntegration()
		: loop(NULL),
		  multi(NULL)
	{
		STAILQ_INIT(&socketInfos);
		memset(&timer, 0, sizeof(timer));
		timer.data = this;
	}

	CurlLibevIntegration(struct ev_loop *loop, CURLM *multi)
		: loop(NULL),
		  multi(NULL)
	{
		STAILQ_INIT(&socketInfos);
		memset(&timer, 0, sizeof(timer));
		timer.data = this;
		initialize(loop, multi);
	}

	~CurlLibevIntegration() {
		destroy();
	}

	void initialize(struct ev_loop *loop, CURLM *multi) {
		assert(this->loop == NULL);
		assert(this->multi == NULL);
		this->loop = loop;
		this->multi = multi;
		curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, onCurlSocketActivity);
		curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, this);
		curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, onCurlTimerActivity);
		curl_multi_setopt(multi, CURLMOPT_TIMERDATA, this);
	}

	void destroy() {
		SocketInfo *socketInfo, *next;

		curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, NULL);
		curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, NULL);

		STAILQ_FOREACH_SAFE(socketInfo, &socketInfos, next, next) {
			if (ev_is_active(&socketInfo->io)) {
				ev_io_stop(loop, &socketInfo->io);
			}
			delete socketInfo;
		}
		STAILQ_INIT(&socketInfos);

		if (ev_is_active(&timer)) {
			ev_timer_stop(loop, &timer);
		}

		loop = NULL;
		multi = NULL;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_CURL_LIBEV_INTEGRATION_H_ */
