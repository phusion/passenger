// Copyright (C) 2001-2003
// William E. Kempf
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>

#include <boost/thread/mutex.hpp>
#include <boost/thread/xtime.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/exceptions.hpp>
#include <boost/limits.hpp>
#include <string>
#include <stdexcept>
#include <cassert>
#include "timeconv.inl"

#if defined(BOOST_HAS_WINTHREADS)
#   include <new>
#   include <boost/thread/once.hpp>
#   include <windows.h>
#   include <time.h>
#   include "mutex.inl"
#elif defined(BOOST_HAS_PTHREADS)
#   include <errno.h>
#elif defined(BOOST_HAS_MPTASKS)
#    include <MacErrors.h>
#    include "mac/init.hpp"
#    include "mac/safe.hpp"
#endif

namespace boost {

#if defined(BOOST_HAS_WINTHREADS)

mutex::mutex()
    : m_mutex(0)
    , m_critical_section(false)
{
    m_critical_section = true;
    if (m_critical_section)
        m_mutex = new_critical_section();
    else
        m_mutex = new_mutex(0);
}

mutex::~mutex()
{
    if (m_critical_section)
        delete_critical_section(m_mutex);
    else
        delete_mutex(m_mutex);
}

void mutex::do_lock()
{
    if (m_critical_section)
        wait_critical_section_infinite(m_mutex);
    else
        wait_mutex(m_mutex, INFINITE);
}

void mutex::do_unlock()
{
    if (m_critical_section)
        release_critical_section(m_mutex);
    else
        release_mutex(m_mutex);
}

void mutex::do_lock(cv_state&)
{
    do_lock();
}

void mutex::do_unlock(cv_state&)
{
    do_unlock();
}

try_mutex::try_mutex()
    : m_mutex(0)
    , m_critical_section(false)
{
    m_critical_section = has_TryEnterCriticalSection();
    if (m_critical_section)
        m_mutex = new_critical_section();
    else
        m_mutex = new_mutex(0);
}

try_mutex::~try_mutex()
{
    if (m_critical_section)
        delete_critical_section(m_mutex);
    else
        delete_mutex(m_mutex);
}

void try_mutex::do_lock()
{
    if (m_critical_section)
        wait_critical_section_infinite(m_mutex);
    else
        wait_mutex(m_mutex, INFINITE);
}

bool try_mutex::do_trylock()
{
    if (m_critical_section)
        return wait_critical_section_try(m_mutex);
    else
        return wait_mutex(m_mutex, 0) == WAIT_OBJECT_0;
}

void try_mutex::do_unlock()
{
    if (m_critical_section)
        release_critical_section(m_mutex);
    else
        release_mutex(m_mutex);
}

void try_mutex::do_lock(cv_state&)
{
    do_lock();
}

void try_mutex::do_unlock(cv_state&)
{
    do_unlock();
}

timed_mutex::timed_mutex()
    : m_mutex(0)
{
    m_mutex = new_mutex(0);
}

timed_mutex::~timed_mutex()
{
    delete_mutex(m_mutex);
}

void timed_mutex::do_lock()
{
    wait_mutex(m_mutex, INFINITE);
}

bool timed_mutex::do_trylock()
{
    return wait_mutex(m_mutex, 0) == WAIT_OBJECT_0;
}

bool timed_mutex::do_timedlock(const xtime& xt)
{
    for (;;)
    {
        int milliseconds;
        to_duration(xt, milliseconds);

        int res = wait_mutex(m_mutex, milliseconds);

        if (res == WAIT_TIMEOUT)
        {
            boost::xtime cur;
            boost::xtime_get(&cur, boost::TIME_UTC);
            if (boost::xtime_cmp(xt, cur) > 0)
                continue;
        }

        return res == WAIT_OBJECT_0;
    }
}

void timed_mutex::do_unlock()
{
    release_mutex(m_mutex);
}

void timed_mutex::do_lock(cv_state&)
{
    do_lock();
}

void timed_mutex::do_unlock(cv_state&)
{
    do_unlock();
}

#elif defined(BOOST_HAS_PTHREADS)

mutex::mutex()
{
    int res = 0;
    res = pthread_mutex_init(&m_mutex, 0);
    if (res != 0)
        throw thread_resource_error();
}

mutex::~mutex()
{
    int res = 0;
    res = pthread_mutex_destroy(&m_mutex);
    assert(res == 0);
}

void mutex::do_lock()
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    if (res == EDEADLK) throw lock_error();
    assert(res == 0);
}

void mutex::do_unlock()
{
    int res = 0;
    res = pthread_mutex_unlock(&m_mutex);
    if (res == EPERM) throw lock_error();
    assert(res == 0);
}

void mutex::do_lock(cv_state&)
{
}

void mutex::do_unlock(cv_state& state)
{
    state.pmutex = &m_mutex;
}

try_mutex::try_mutex()
{
    int res = 0;
    res = pthread_mutex_init(&m_mutex, 0);
    if (res != 0)
        throw thread_resource_error();
}

try_mutex::~try_mutex()
{
    int res = 0;
    res = pthread_mutex_destroy(&m_mutex);
    assert(res == 0);
}

void try_mutex::do_lock()
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    if (res == EDEADLK) throw lock_error();
    assert(res == 0);
}

bool try_mutex::do_trylock()
{
    int res = 0;
    res = pthread_mutex_trylock(&m_mutex);
    if (res == EDEADLK) throw lock_error();
    assert(res == 0 || res == EBUSY);
    return res == 0;
}

void try_mutex::do_unlock()
{
    int res = 0;
    res = pthread_mutex_unlock(&m_mutex);
    if (res == EPERM) throw lock_error();
    assert(res == 0);
}

void try_mutex::do_lock(cv_state&)
{
}

void try_mutex::do_unlock(cv_state& state)
{
    state.pmutex = &m_mutex;
}

timed_mutex::timed_mutex()
    : m_locked(false)
{
    int res = 0;
    res = pthread_mutex_init(&m_mutex, 0);
    if (res != 0)
        throw thread_resource_error();

    res = pthread_cond_init(&m_condition, 0);
    if (res != 0)
    {
        pthread_mutex_destroy(&m_mutex);
        throw thread_resource_error();
    }
}

timed_mutex::~timed_mutex()
{
    assert(!m_locked);
    int res = 0;
    res = pthread_mutex_destroy(&m_mutex);
    assert(res == 0);

    res = pthread_cond_destroy(&m_condition);
    assert(res == 0);
}

void timed_mutex::do_lock()
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    while (m_locked)
    {
        res = pthread_cond_wait(&m_condition, &m_mutex);
        assert(res == 0);
    }

    assert(!m_locked);
    m_locked = true;

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
}

bool timed_mutex::do_trylock()
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    bool ret = false;
    if (!m_locked)
    {
        m_locked = true;
        ret = true;
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
    return ret;
}

bool timed_mutex::do_timedlock(const xtime& xt)
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    timespec ts;
    to_timespec(xt, ts);

    while (m_locked)
    {
        res = pthread_cond_timedwait(&m_condition, &m_mutex, &ts);
        assert(res == 0 || res == ETIMEDOUT);

        if (res == ETIMEDOUT)
            break;
    }

    bool ret = false;
    if (!m_locked)
    {
        m_locked = true;
        ret = true;
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
    return ret;
}

void timed_mutex::do_unlock()
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    assert(m_locked);
    m_locked = false;

    res = pthread_cond_signal(&m_condition);
    assert(res == 0);

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
}

void timed_mutex::do_lock(cv_state&)
{
    int res = 0;
    while (m_locked)
    {
        res = pthread_cond_wait(&m_condition, &m_mutex);
        assert(res == 0);
    }

    assert(!m_locked);
    m_locked = true;

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
}

void timed_mutex::do_unlock(cv_state& state)
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    assert(m_locked);
    m_locked = false;

    res = pthread_cond_signal(&m_condition);
    assert(res == 0);

    state.pmutex = &m_mutex;
}

#elif defined(BOOST_HAS_MPTASKS)

using threads::mac::detail::safe_enter_critical_region;

mutex::mutex()
{
}

mutex::~mutex()
{
}

void mutex::do_lock()
{
    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);
}

void mutex::do_unlock()
{
    OSStatus lStatus = noErr;
    lStatus = MPExitCriticalRegion(m_mutex);
    assert(lStatus == noErr);
}

void mutex::do_lock(cv_state& /*state*/)
{
    do_lock();
}

void mutex::do_unlock(cv_state& /*state*/)
{
    do_unlock();
}

try_mutex::try_mutex()
{
}

try_mutex::~try_mutex()
{
}

void try_mutex::do_lock()
{
    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);
}

bool try_mutex::do_trylock()
{
    OSStatus lStatus = noErr;
    lStatus = MPEnterCriticalRegion(m_mutex, kDurationImmediate);
    assert(lStatus == noErr || lStatus == kMPTimeoutErr);
    return lStatus == noErr;
}

void try_mutex::do_unlock()
{
    OSStatus lStatus = noErr;
    lStatus = MPExitCriticalRegion(m_mutex);
    assert(lStatus == noErr);
}

void try_mutex::do_lock(cv_state& /*state*/)
{
    do_lock();
}

void try_mutex::do_unlock(cv_state& /*state*/)
{
    do_unlock();
}

timed_mutex::timed_mutex()
{
}

timed_mutex::~timed_mutex()
{
}

void timed_mutex::do_lock()
{
    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);
}

bool timed_mutex::do_trylock()
{
    OSStatus lStatus = noErr;
    lStatus = MPEnterCriticalRegion(m_mutex, kDurationImmediate);
    assert(lStatus == noErr || lStatus == kMPTimeoutErr);
    return(lStatus == noErr);
}

bool timed_mutex::do_timedlock(const xtime& xt)
{
    int microseconds;
    to_microduration(xt, microseconds);
    Duration lDuration = kDurationMicrosecond * microseconds;

    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, lDuration, m_mutex_mutex);
    assert(lStatus == noErr || lStatus == kMPTimeoutErr);

    return(lStatus == noErr);
}

void timed_mutex::do_unlock()
{
    OSStatus lStatus = noErr;
    lStatus = MPExitCriticalRegion(m_mutex);
    assert(lStatus == noErr);
}

void timed_mutex::do_lock(cv_state& /*state*/)
{
    do_lock();
}

void timed_mutex::do_unlock(cv_state& /*state*/)
{
    do_unlock();
}

#endif

} // namespace boost

// Change Log:
//   8 Feb 01  WEKEMPF Initial version.
