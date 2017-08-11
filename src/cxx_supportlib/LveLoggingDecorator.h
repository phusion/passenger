/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_LVE_LOGGING_DECORATOR_H_
#define _PASSENGER_LVE_LOGGING_DECORATOR_H_

#include <LoggingKit/LoggingKit.h>
#include <adhoc_lve.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace Passenger {


class LveLoggingDecorator {
public:
	static adhoc_lve::LibLve &lveInitOnce() {
		std::string initOneTimeError;
		adhoc_lve::LibLve &lveLibHandle = adhoc_lve::LveInitSignleton::getInstance(&initOneTimeError);

		if (!lveLibHandle.is_lve_available()) {
			P_DEBUG("LVE lib is not available");
		} else if (lveLibHandle.is_error()) {
			if (!initOneTimeError.empty())
				P_ERROR("LVE init error: " << initOneTimeError);
		} else {
			P_DEBUG("LVE get instance (or init) success");
		}

		return lveLibHandle;
	}

	static void logLveEnter(const adhoc_lve::LveEnter &lveEnter, uid_t uid, uid_t min_uid) {
		if (lveEnter.lveInstance().is_lve_ready() && lveEnter.is_error()) {
			P_ERROR("LVE enter [pid " << ::getpid() << ", uid "     << uid
			                                        << ", min_uid " << min_uid
			                                        << "] error: "  << lveEnter.error());
		} else if (lveEnter.is_entered()) {
			P_DEBUG("LVE enter [pid " << ::getpid() << ", uid " << uid
			                                        << ", min_uid " << min_uid
			                                        << "] success");
		} else {
			P_DEBUG("LVE not in [pid " << ::getpid() << ", uid " << uid
			                                         << ", min_uid " << min_uid << "]");
		}
	}

	static void lveExitCallback(bool entered, const std::string &exit_error) {
		if (!entered) {
			return;
		}

		try {
			bool is_error = !exit_error.empty();
			if (is_error) {
				P_ERROR("LVE exit [pid " << ::getpid() << "] error: " << exit_error);
			} else {
				P_DEBUG("LVE exit [pid " << ::getpid() << "] success");
			}
		} catch(...) {
			// Can be called from adhoc_lve::LveEnter destructor while stack unwinding.
		}
	}
}; // class LveLoggingDecorator


} // namespace Passenger

#endif /* _PASSENGER_LVE_LOGGING_DECORATOR_H_ */
