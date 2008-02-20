// Copyright (C) 2001-2003
// William E. Kempf
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>

#if defined(BOOST_HAS_FTIME)
#   define __STDC_CONSTANT_MACROS
#endif

#include <boost/thread/xtime.hpp>

#if defined(BOOST_HAS_FTIME)
#   include <windows.h>
#   include <boost/cstdint.hpp>
#elif defined(BOOST_HAS_GETTIMEOFDAY)
#   include <sys/time.h>
#elif defined(BOOST_HAS_MPTASKS)
#   include <DriverServices.h>
#   include <boost/thread/detail/force_cast.hpp>
#endif

#include <cassert>

namespace boost {

#ifdef BOOST_HAS_MPTASKS

namespace detail
{

using thread::force_cast;

struct startup_time_info
{
    startup_time_info()
    {
        // 1970 Jan 1 at 00:00:00
        static const DateTimeRec k_sUNIXBase = {1970, 1, 1, 0, 0, 0, 0};
        static unsigned long s_ulUNIXBaseSeconds = 0UL;

        if(s_ulUNIXBaseSeconds == 0UL)
        {
            // calculate the number of seconds between the Mac OS base and the
            // UNIX base the first time we enter this constructor.
            DateToSeconds(&k_sUNIXBase, &s_ulUNIXBaseSeconds);
        }

        unsigned long ulSeconds;

        // get the time in UpTime units twice, with the time in seconds in the
        // middle.
        uint64_t ullFirstUpTime = force_cast<uint64_t>(UpTime());
        GetDateTime(&ulSeconds);
        uint64_t ullSecondUpTime = force_cast<uint64_t>(UpTime());

        // calculate the midpoint of the two UpTimes, and save that.
        uint64_t ullAverageUpTime = (ullFirstUpTime + ullSecondUpTime) / 2ULL;
        m_sStartupAbsoluteTime = force_cast<AbsoluteTime>(ullAverageUpTime);

        // save the number of seconds, recentered at the UNIX base.
        m_ulStartupSeconds = ulSeconds - s_ulUNIXBaseSeconds;
    }

    AbsoluteTime m_sStartupAbsoluteTime;
    UInt32 m_ulStartupSeconds;
};

static startup_time_info g_sStartupTimeInfo;

} // namespace detail

#endif


int xtime_get(struct xtime* xtp, int clock_type)
{
    if (clock_type == TIME_UTC)
    {
#if defined(BOOST_HAS_FTIME)
        FILETIME ft;
#   if defined(BOOST_NO_GETSYSTEMTIMEASFILETIME)
        {
            SYSTEMTIME st;
            GetSystemTime(&st);
            SystemTimeToFileTime(&st,&ft);
        }
#   else
        GetSystemTimeAsFileTime(&ft);
#   endif
        static const boost::uint64_t TIMESPEC_TO_FILETIME_OFFSET =
            UINT64_C(116444736000000000);
        
        const boost::uint64_t ft64 =
            (static_cast<boost::uint64_t>(ft.dwHighDateTime) << 32)
            + ft.dwLowDateTime;

        xtp->sec = static_cast<xtime::xtime_sec_t>(
            (ft64 - TIMESPEC_TO_FILETIME_OFFSET) / 10000000
        );

        xtp->nsec = static_cast<xtime::xtime_nsec_t>(
            ((ft64 - TIMESPEC_TO_FILETIME_OFFSET) % 10000000) * 100
        );

        return clock_type;
#elif defined(BOOST_HAS_GETTIMEOFDAY)
        struct timeval tv;
#       ifndef NDEBUG
        int res =
#endif            
        gettimeofday(&tv, 0);
        assert(0 == res);
        assert(tv.tv_sec >= 0);
        assert(tv.tv_usec >= 0);
        xtp->sec = tv.tv_sec;
        xtp->nsec = tv.tv_usec * 1000;
        return clock_type;
#elif defined(BOOST_HAS_CLOCK_GETTIME)
        timespec ts;
#       ifndef NDEBUG
        int res =
#       endif            
        clock_gettime(CLOCK_REALTIME, &ts);
        assert(0 == res);
        xtp->sec = ts.tv_sec;
        xtp->nsec = ts.tv_nsec;
        return clock_type;
#elif defined(BOOST_HAS_MPTASKS)
        using detail::thread::force_cast;
        // the Mac OS does not have an MP-safe way of getting the date/time,
        // so we use a delta from the startup time.  We _could_ defer this
        // and use something that is interrupt-safe, but this would be _SLOW_,
        // and we need speed here.
        const uint64_t k_ullNanosecondsPerSecond(1000ULL * 1000ULL * 1000ULL);
        AbsoluteTime sUpTime(UpTime());
        uint64_t ullNanoseconds(
            force_cast<uint64_t>(
                AbsoluteDeltaToNanoseconds(sUpTime,
                    detail::g_sStartupTimeInfo.m_sStartupAbsoluteTime)));
        uint64_t ullSeconds = (ullNanoseconds / k_ullNanosecondsPerSecond);
        ullNanoseconds -= (ullSeconds * k_ullNanosecondsPerSecond);
        xtp->sec = detail::g_sStartupTimeInfo.m_ulStartupSeconds + ullSeconds;
        xtp->nsec = ullNanoseconds;
        return clock_type;
#else
#   error "xtime_get implementation undefined"
#endif
    }
    return 0;
}

} // namespace boost

// Change Log:
//   8 Feb 01  WEKEMPF Initial version.
