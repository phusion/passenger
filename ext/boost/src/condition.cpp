// Copyright (C) 2001-2003
// William E. Kempf
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>

#include <boost/thread/condition.hpp>
#include <boost/thread/xtime.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/exceptions.hpp>
#include <boost/limits.hpp>
#include <cassert>
#include "timeconv.inl"

#if defined(BOOST_HAS_WINTHREADS)
#   ifndef NOMINMAX
#      define NOMINMAX
#   endif
#   include <windows.h>
#elif defined(BOOST_HAS_PTHREADS)
#   include <errno.h>
#elif defined(BOOST_HAS_MPTASKS)
#   include <MacErrors.h>
#   include "mac/init.hpp"
#   include "mac/safe.hpp"
#endif

// The following include can be removed after the bug on QNX
// has been tracked down. I need this only for debugging
//#if !defined(NDEBUG) && defined(BOOST_HAS_PTHREADS)
#include <iostream>
//#endif

namespace boost {

namespace detail {

#if defined(BOOST_HAS_WINTHREADS)
condition_impl::condition_impl()
    : m_gone(0), m_blocked(0), m_waiting(0)
{
    m_gate = reinterpret_cast<void*>(CreateSemaphore(0, 1, 1, 0));
    m_queue = reinterpret_cast<void*>(
        CreateSemaphore(0, 0, (std::numeric_limits<long>::max)(), 0));
    m_mutex = reinterpret_cast<void*>(CreateMutex(0, 0, 0));

    if (!m_gate || !m_queue || !m_mutex)
    {
        int res = 0;
        if (m_gate)
        {
            res = CloseHandle(reinterpret_cast<HANDLE>(m_gate));
            assert(res);
        }
        if (m_queue)
        {
            res = CloseHandle(reinterpret_cast<HANDLE>(m_queue));
            assert(res);
        }
        if (m_mutex)
        {
            res = CloseHandle(reinterpret_cast<HANDLE>(m_mutex));
            assert(res);
        }

        throw thread_resource_error();
    }
}

condition_impl::~condition_impl()
{
    int res = 0;
    res = CloseHandle(reinterpret_cast<HANDLE>(m_gate));
    assert(res);
    res = CloseHandle(reinterpret_cast<HANDLE>(m_queue));
    assert(res);
    res = CloseHandle(reinterpret_cast<HANDLE>(m_mutex));
    assert(res);
}

void condition_impl::notify_one()
{
    unsigned signals = 0;

    int res = 0;
    res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_mutex), INFINITE);
    assert(res == WAIT_OBJECT_0);

    if (m_waiting != 0) // the m_gate is already closed
    {
        if (m_blocked == 0)
        {
            res = ReleaseMutex(reinterpret_cast<HANDLE>(m_mutex));
            assert(res);
            return;
        }

        ++m_waiting;
        --m_blocked;
        signals = 1;
    }
    else
    {
        res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_gate), INFINITE);
        assert(res == WAIT_OBJECT_0);
        if (m_blocked > m_gone)
        {
            if (m_gone != 0)
            {
                m_blocked -= m_gone;
                m_gone = 0;
            }
            signals = m_waiting = 1;
            --m_blocked;
        }
        else
        {
            res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_gate), 1, 0);
            assert(res);
        }
    }

    res = ReleaseMutex(reinterpret_cast<HANDLE>(m_mutex));
    assert(res);

    if (signals)
    {
        res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_queue), signals, 0);
        assert(res);
    }
}

void condition_impl::notify_all()
{
    unsigned signals = 0;

    int res = 0;
    res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_mutex), INFINITE);
    assert(res == WAIT_OBJECT_0);

    if (m_waiting != 0) // the m_gate is already closed
    {
        if (m_blocked == 0)
        {
            res = ReleaseMutex(reinterpret_cast<HANDLE>(m_mutex));
            assert(res);
            return;
        }

        m_waiting += (signals = m_blocked);
        m_blocked = 0;
    }
    else
    {
        res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_gate), INFINITE);
        assert(res == WAIT_OBJECT_0);
        if (m_blocked > m_gone)
        {
            if (m_gone != 0)
            {
                m_blocked -= m_gone;
                m_gone = 0;
            }
            signals = m_waiting = m_blocked;
            m_blocked = 0;
        }
        else
        {
            res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_gate), 1, 0);
            assert(res);
        }
    }

    res = ReleaseMutex(reinterpret_cast<HANDLE>(m_mutex));
    assert(res);

    if (signals)
    {
        res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_queue), signals, 0);
        assert(res);
    }
}

void condition_impl::enter_wait()
{
    int res = 0;
    res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_gate), INFINITE);
    assert(res == WAIT_OBJECT_0);
    ++m_blocked;
    res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_gate), 1, 0);
    assert(res);
}

void condition_impl::do_wait()
{
    int res = 0;
    res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_queue), INFINITE);
    assert(res == WAIT_OBJECT_0);

    unsigned was_waiting=0;
    unsigned was_gone=0;

    res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_mutex), INFINITE);
    assert(res == WAIT_OBJECT_0);
    was_waiting = m_waiting;
    was_gone = m_gone;
    if (was_waiting != 0)
    {
        if (--m_waiting == 0)
        {
            if (m_blocked != 0)
            {
                res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_gate), 1,
                    0); // open m_gate
                assert(res);
                was_waiting = 0;
            }
            else if (m_gone != 0)
                m_gone = 0;
        }
    }
    else if (++m_gone == ((std::numeric_limits<unsigned>::max)() / 2))
    {
        // timeout occured, normalize the m_gone count
        // this may occur if many calls to wait with a timeout are made and
        // no call to notify_* is made
        res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_gate), INFINITE);
        assert(res == WAIT_OBJECT_0);
        m_blocked -= m_gone;
        res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_gate), 1, 0);
        assert(res);
        m_gone = 0;
    }
    res = ReleaseMutex(reinterpret_cast<HANDLE>(m_mutex));
    assert(res);

    if (was_waiting == 1)
    {
        for (/**/ ; was_gone; --was_gone)
        {
            // better now than spurious later
            res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_queue),
                INFINITE);
            assert(res == WAIT_OBJECT_0);
        }
        res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_gate), 1, 0);
        assert(res);
    }
}

bool condition_impl::do_timed_wait(const xtime& xt)
{
    bool ret = false;
    unsigned int res = 0;

    for (;;)
    {
        int milliseconds;
        to_duration(xt, milliseconds);

        res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_queue),
            milliseconds);
        assert(res != WAIT_FAILED && res != WAIT_ABANDONED);
        ret = (res == WAIT_OBJECT_0);

        if (res == WAIT_TIMEOUT)
        {
            xtime cur;
            xtime_get(&cur, TIME_UTC);
            if (xtime_cmp(xt, cur) > 0)
                continue;
        }

        break;
    }

    unsigned was_waiting=0;
    unsigned was_gone=0;

    res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_mutex), INFINITE);
    assert(res == WAIT_OBJECT_0);
    was_waiting = m_waiting;
    was_gone = m_gone;
    if (was_waiting != 0)
    {
        if (!ret) // timeout
        {
            if (m_blocked != 0)
                --m_blocked;
            else
                ++m_gone; // count spurious wakeups
        }
        if (--m_waiting == 0)
        {
            if (m_blocked != 0)
            {
                res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_gate), 1,
                    0); // open m_gate
                assert(res);
                was_waiting = 0;
            }
            else if (m_gone != 0)
                m_gone = 0;
        }
    }
    else if (++m_gone == ((std::numeric_limits<unsigned>::max)() / 2))
    {
        // timeout occured, normalize the m_gone count
        // this may occur if many calls to wait with a timeout are made and
        // no call to notify_* is made
        res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_gate), INFINITE);
        assert(res == WAIT_OBJECT_0);
        m_blocked -= m_gone;
        res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_gate), 1, 0);
        assert(res);
        m_gone = 0;
    }
    res = ReleaseMutex(reinterpret_cast<HANDLE>(m_mutex));
    assert(res);

    if (was_waiting == 1)
    {
        for (/**/ ; was_gone; --was_gone)
        {
            // better now than spurious later
            res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_queue),
                INFINITE);
            assert(res ==  WAIT_OBJECT_0);
        }
        res = ReleaseSemaphore(reinterpret_cast<HANDLE>(m_gate), 1, 0);
        assert(res);
    }

    return ret;
}
#elif defined(BOOST_HAS_PTHREADS)
condition_impl::condition_impl()
{
    int res = 0;
    res = pthread_cond_init(&m_condition, 0);
    if (res != 0)
        throw thread_resource_error();
}

condition_impl::~condition_impl()
{
    int res = 0;
    res = pthread_cond_destroy(&m_condition);
    assert(res == 0);
}

void condition_impl::notify_one()
{
    int res = 0;
    res = pthread_cond_signal(&m_condition);
    assert(res == 0);
}

void condition_impl::notify_all()
{
    int res = 0;
    res = pthread_cond_broadcast(&m_condition);
    assert(res == 0);
}

void condition_impl::do_wait(pthread_mutex_t* pmutex)
{
    int res = 0;
    res = pthread_cond_wait(&m_condition, pmutex);
    assert(res == 0);
}

bool condition_impl::do_timed_wait(const xtime& xt, pthread_mutex_t* pmutex)
{
    timespec ts;
    to_timespec(xt, ts);

    int res = 0;
    res = pthread_cond_timedwait(&m_condition, pmutex, &ts);
// Test code for QNX debugging, to get information during regressions
#ifndef NDEBUG
    if (res == EINVAL) {
        boost::xtime now;
        boost::xtime_get(&now, boost::TIME_UTC);
        std::cerr << "now: " << now.sec << " " << now.nsec << std::endl;
        std::cerr << "time: " << time(0) << std::endl;
        std::cerr << "xtime: " << xt.sec << " " << xt.nsec << std::endl;
        std::cerr << "ts: " << ts.tv_sec << " " << ts.tv_nsec << std::endl;
        std::cerr << "pmutex: " << pmutex << std::endl;
        std::cerr << "condition: " << &m_condition << std::endl;
        assert(res != EINVAL);
    }
#endif    
    assert(res == 0 || res == ETIMEDOUT);

    return res != ETIMEDOUT;
}
#elif defined(BOOST_HAS_MPTASKS)

using threads::mac::detail::safe_enter_critical_region;
using threads::mac::detail::safe_wait_on_semaphore;

condition_impl::condition_impl()
    : m_gone(0), m_blocked(0), m_waiting(0)
{
    threads::mac::detail::thread_init();

    OSStatus lStatus = noErr;

    lStatus = MPCreateSemaphore(1, 1, &m_gate);
    if(lStatus == noErr)
        lStatus = MPCreateSemaphore(ULONG_MAX, 0, &m_queue);

    if(lStatus != noErr || !m_gate || !m_queue)
    {
        if (m_gate)
        {
            lStatus = MPDeleteSemaphore(m_gate);
            assert(lStatus == noErr);
        }
        if (m_queue)
        {
            lStatus = MPDeleteSemaphore(m_queue);
            assert(lStatus == noErr);
        }

        throw thread_resource_error();
    }
}

condition_impl::~condition_impl()
{
    OSStatus lStatus = noErr;
    lStatus = MPDeleteSemaphore(m_gate);
    assert(lStatus == noErr);
    lStatus = MPDeleteSemaphore(m_queue);
    assert(lStatus == noErr);
}

void condition_impl::notify_one()
{
    unsigned signals = 0;

    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);

    if (m_waiting != 0) // the m_gate is already closed
    {
        if (m_blocked == 0)
        {
            lStatus = MPExitCriticalRegion(m_mutex);
            assert(lStatus == noErr);
            return;
        }

        ++m_waiting;
        --m_blocked;
    }
    else
    {
        lStatus = safe_wait_on_semaphore(m_gate, kDurationForever);
        assert(lStatus == noErr);
        if (m_blocked > m_gone)
        {
            if (m_gone != 0)
            {
                m_blocked -= m_gone;
                m_gone = 0;
            }
            signals = m_waiting = 1;
            --m_blocked;
        }
        else
        {
            lStatus = MPSignalSemaphore(m_gate);
            assert(lStatus == noErr);
        }

        lStatus = MPExitCriticalRegion(m_mutex);
        assert(lStatus == noErr);

        while (signals)
        {
            lStatus = MPSignalSemaphore(m_queue);
            assert(lStatus == noErr);
            --signals;
        }
    }
}

void condition_impl::notify_all()
{
    unsigned signals = 0;

    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);

    if (m_waiting != 0) // the m_gate is already closed
    {
        if (m_blocked == 0)
        {
            lStatus = MPExitCriticalRegion(m_mutex);
            assert(lStatus == noErr);
            return;
        }

        m_waiting += (signals = m_blocked);
        m_blocked = 0;
    }
    else
    {
        lStatus = safe_wait_on_semaphore(m_gate, kDurationForever);
        assert(lStatus == noErr);
        if (m_blocked > m_gone)
        {
            if (m_gone != 0)
            {
                m_blocked -= m_gone;
                m_gone = 0;
            }
            signals = m_waiting = m_blocked;
            m_blocked = 0;
        }
        else
        {
            lStatus = MPSignalSemaphore(m_gate);
            assert(lStatus == noErr);
        }

        lStatus = MPExitCriticalRegion(m_mutex);
        assert(lStatus == noErr);

        while (signals)
        {
            lStatus = MPSignalSemaphore(m_queue);
            assert(lStatus == noErr);
            --signals;
        }
    }
}

void condition_impl::enter_wait()
{
    OSStatus lStatus = noErr;
    lStatus = safe_wait_on_semaphore(m_gate, kDurationForever);
    assert(lStatus == noErr);
    ++m_blocked;
    lStatus = MPSignalSemaphore(m_gate);
    assert(lStatus == noErr);
}

void condition_impl::do_wait()
{
    OSStatus lStatus = noErr;
    lStatus = safe_wait_on_semaphore(m_queue, kDurationForever);
    assert(lStatus == noErr);

    unsigned was_waiting=0;
    unsigned was_gone=0;

    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);
    was_waiting = m_waiting;
    was_gone = m_gone;
    if (was_waiting != 0)
    {
        if (--m_waiting == 0)
        {
            if (m_blocked != 0)
            {
                lStatus = MPSignalSemaphore(m_gate); // open m_gate
                assert(lStatus == noErr);
                was_waiting = 0;
            }
            else if (m_gone != 0)
                m_gone = 0;
        }
    }
    else if (++m_gone == ((std::numeric_limits<unsigned>::max)() / 2))
    {
        // timeout occured, normalize the m_gone count
        // this may occur if many calls to wait with a timeout are made and
        // no call to notify_* is made
        lStatus = safe_wait_on_semaphore(m_gate, kDurationForever);
        assert(lStatus == noErr);
        m_blocked -= m_gone;
        lStatus = MPSignalSemaphore(m_gate);
        assert(lStatus == noErr);
        m_gone = 0;
    }
    lStatus = MPExitCriticalRegion(m_mutex);
    assert(lStatus == noErr);

    if (was_waiting == 1)
    {
        for (/**/ ; was_gone; --was_gone)
        {
            // better now than spurious later
            lStatus = safe_wait_on_semaphore(m_queue, kDurationForever);
            assert(lStatus == noErr);
        }
        lStatus = MPSignalSemaphore(m_gate);
        assert(lStatus == noErr);
    }
}

bool condition_impl::do_timed_wait(const xtime& xt)
{
    int milliseconds;
    to_duration(xt, milliseconds);

    OSStatus lStatus = noErr;
    lStatus = safe_wait_on_semaphore(m_queue, milliseconds);
    assert(lStatus == noErr || lStatus == kMPTimeoutErr);

    bool ret = (lStatus == noErr);

    unsigned was_waiting=0;
    unsigned was_gone=0;

    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);
    was_waiting = m_waiting;
    was_gone = m_gone;
    if (was_waiting != 0)
    {
        if (!ret) // timeout
        {
            if (m_blocked != 0)
                --m_blocked;
            else
                ++m_gone; // count spurious wakeups
        }
        if (--m_waiting == 0)
        {
            if (m_blocked != 0)
            {
                lStatus = MPSignalSemaphore(m_gate); // open m_gate
                assert(lStatus == noErr);
                was_waiting = 0;
            }
            else if (m_gone != 0)
                m_gone = 0;
        }
    }
    else if (++m_gone == ((std::numeric_limits<unsigned>::max)() / 2))
    {
        // timeout occured, normalize the m_gone count
        // this may occur if many calls to wait with a timeout are made and
        // no call to notify_* is made
        lStatus = safe_wait_on_semaphore(m_gate, kDurationForever);
        assert(lStatus == noErr);
        m_blocked -= m_gone;
        lStatus = MPSignalSemaphore(m_gate);
        assert(lStatus == noErr);
        m_gone = 0;
    }
    lStatus = MPExitCriticalRegion(m_mutex);
    assert(lStatus == noErr);

    if (was_waiting == 1)
    {
        for (/**/ ; was_gone; --was_gone)
        {
            // better now than spurious later
            lStatus = safe_wait_on_semaphore(m_queue, kDurationForever);
            assert(lStatus == noErr);
        }
        lStatus = MPSignalSemaphore(m_gate);
        assert(lStatus == noErr);
    }

    return ret;
}
#endif

} // namespace detail

} // namespace boost

// Change Log:
//    8 Feb 01  WEKEMPF Initial version.
//   22 May 01  WEKEMPF Modified to use xtime for time outs.
//    3 Jan 03  WEKEMPF Modified for DLL implementation.
