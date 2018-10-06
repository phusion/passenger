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

#include <SystemTools/SystemTime.h>

namespace Passenger {
	namespace SystemTimeData {
		bool initialized = false;
		bool hasForcedValue = false;
		time_t forcedValue = 0;
		bool hasForcedUsecValue = false;
		unsigned long long forcedUsecValue = 0;

		#if BOOST_OS_MACOS
			mach_timebase_info_data_t timeInfo;
		#elif defined(SYSTEM_TIME_HAVE_MONOTONIC_CLOCK)
			#ifdef SYSTEM_TIME_HAVE_CLOCK_MONOTONIC_COARSE
				unsigned long long monotonicCoarseResolutionNs = 0;
			#endif
			#ifdef SYSTEM_TIME_HAVE_CLOCK_MONOTONIC_FAST
				unsigned long long monotonicFastResolutionNs = 0;
			#endif
			unsigned long long monotonicResolutionNs;
		#endif
	}
}
