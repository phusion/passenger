// Copyright (C) 2001-2003
// William E. Kempf
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>

#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/xtime.hpp>
#include <boost/thread/thread.hpp>
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
#   include <MacErrors.h>
#   include "safe.hpp"
#endif

namespace boost {

#if defined(BOOST_HAS_WINTHREADS)

recursive_mutex::recursive_mutex()
    : m_mutex(0)
    , m_critical_section(false)
    , m_count(0)
{
    m_critical_section = true;
    if (m_critical_section)
        m_mutex = new_critical_section();
    else
        m_mutex = new_mutex(0);
}

recursive_mutex::~recursive_mutex()
{
    if (m_critical_section)
        delete_critical_section(m_mutex);
    else
        delete_mutex(m_mutex);
}

void recursive_mutex::do_lock()
{
    if (m_critical_section)
        wait_critical_section_infinite(m_mutex);
    else
        wait_mutex(m_mutex, INFINITE);

    if (++m_count > 1)
    {
        if (m_critical_section)
            release_critical_section(m_mutex);
        else
            release_mutex(m_mutex);
    }
}

void recursive_mutex::do_unlock()
{
    if (--m_count == 0)
    {
        if (m_critical_section)
            release_critical_section(m_mutex);
        else
            release_mutex(m_mutex);
    }
}

void recursive_mutex::do_lock(cv_state& state)
{
    if (m_critical_section)
        wait_critical_section_infinite(m_mutex);
    else
        wait_mutex(m_mutex, INFINITE);

    m_count = state;
}

void recursive_mutex::do_unlock(cv_state& state)
{
    state = m_count;
    m_count = 0;

    if (m_critical_section)
        release_critical_section(m_mutex);
    else
        release_mutex(m_mutex);
}

recursive_try_mutex::recursive_try_mutex()
    : m_mutex(0)
    , m_critical_section(false)
    , m_count(0)
{
    m_critical_section = has_TryEnterCriticalSection();
    if (m_critical_section)
        m_mutex = new_critical_section();
    else
        m_mutex = new_mutex(0);
}

recursive_try_mutex::~recursive_try_mutex()
{
    if (m_critical_section)
        delete_critical_section(m_mutex);
    else
        delete_mutex(m_mutex);
}

void recursive_try_mutex::do_lock()
{
    if (m_critical_section)
        wait_critical_section_infinite(m_mutex);
    else
        wait_mutex(m_mutex, INFINITE);

    if (++m_count > 1)
    {
        if (m_critical_section)
            release_critical_section(m_mutex);
        else
            release_mutex(m_mutex);
    }
}

bool recursive_try_mutex::do_trylock()
{
    bool res = false;
    if (m_critical_section)
        res = wait_critical_section_try(m_mutex);
    else
        res = wait_mutex(m_mutex, 0) == WAIT_OBJECT_0;

    if (res)
    {
        if (++m_count > 1)
        {
            if (m_critical_section)
                release_critical_section(m_mutex);
            else
                release_mutex(m_mutex);
        }
        return true;
    }
    return false;
}

void recursive_try_mutex::do_unlock()
{
    if (--m_count == 0)
    {
        if (m_critical_section)
            release_critical_section(m_mutex);
        else
            release_mutex(m_mutex);
    }
}

void recursive_try_mutex::do_lock(cv_state& state)
{
    if (m_critical_section)
        wait_critical_section_infinite(m_mutex);
    else
        wait_mutex(m_mutex, INFINITE);

    m_count = state;
}

void recursive_try_mutex::do_unlock(cv_state& state)
{
    state = m_count;
    m_count = 0;

    if (m_critical_section)
        release_critical_section(m_mutex);
    else
        release_mutex(m_mutex);
}

recursive_timed_mutex::recursive_timed_mutex()
    : m_mutex(0)
    , m_count(0)
{
    m_mutex = new_mutex(0);
}

recursive_timed_mutex::~recursive_timed_mutex()
{
    delete_mutex(m_mutex);
}

void recursive_timed_mutex::do_lock()
{
    wait_mutex(m_mutex, INFINITE);

    if (++m_count > 1)
        release_mutex(m_mutex);
}

bool recursive_timed_mutex::do_trylock()
{
    bool res = wait_mutex(m_mutex, 0) == WAIT_OBJECT_0;

    if (res)
    {
        if (++m_count > 1)
            release_mutex(m_mutex);
        return true;
    }
    return false;
}

bool recursive_timed_mutex::do_timedlock(const xtime& xt)
{
    for (;;)
    {
        int milliseconds;
        to_duration(xt, milliseconds);

        unsigned int res = wait_mutex(m_mutex, milliseconds);

        if (res == WAIT_TIMEOUT)
        {
            xtime cur;
            xtime_get(&cur, TIME_UTC);
            if (xtime_cmp(xt, cur) > 0)
                continue;
        }

        if (res == WAIT_OBJECT_0)
        {
            if (++m_count > 1)
                release_mutex(m_mutex);
            return true;
        }

        return false;
    }
}

void recursive_timed_mutex::do_unlock()
{
    if (--m_count == 0)
        release_mutex(m_mutex);
}

void recursive_timed_mutex::do_lock(cv_state& state)
{
    wait_mutex(m_mutex, INFINITE);

    m_count = state;
}

void recursive_timed_mutex::do_unlock(cv_state& state)
{
    state = m_count;
    m_count = 0;

    release_mutex(m_mutex);
}

#elif defined(BOOST_HAS_PTHREADS)

recursive_mutex::recursive_mutex()
    : m_count(0)
#   if !defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    , m_valid_id(false)
#   endif
{
    pthread_mutexattr_t attr;
    int res = pthread_mutexattr_init(&attr);
    assert(res == 0);

#   if defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    res = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    assert(res == 0);
#   endif

    res = pthread_mutex_init(&m_mutex, &attr);
    {
        int res = 0;
        res = pthread_mutexattr_destroy(&attr);
        assert(res == 0);
    }
    if (res != 0)
        throw thread_resource_error();

#   if !defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    res = pthread_cond_init(&m_unlocked, 0);
    if (res != 0)
    {
        pthread_mutex_destroy(&m_mutex);
        throw thread_resource_error();
    }
#   endif
}

recursive_mutex::~recursive_mutex()
{
    int res = 0;
    res = pthread_mutex_destroy(&m_mutex);
    assert(res == 0);

#   if !defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    res = pthread_cond_destroy(&m_unlocked);
    assert(res == 0);
#   endif
}

void recursive_mutex::do_lock()
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

#   if defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    if (++m_count > 1)
    {
        res = pthread_mutex_unlock(&m_mutex);
        assert(res == 0);
    }
#   else
    pthread_t tid = pthread_self();
    if (m_valid_id && pthread_equal(m_thread_id, tid))
        ++m_count;
    else
    {
        while (m_valid_id)
        {
            res = pthread_cond_wait(&m_unlocked, &m_mutex);
            assert(res == 0);
        }

        m_thread_id = tid;
        m_valid_id = true;
        m_count = 1;
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
#   endif
}

void recursive_mutex::do_unlock()
{
#   if defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    if (--m_count == 0)
    {
        int res = 0;
        res = pthread_mutex_unlock(&m_mutex);
        assert(res == 0);
    }
#   else
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    pthread_t tid = pthread_self();
    if (m_valid_id && !pthread_equal(m_thread_id, tid))
    {
        res = pthread_mutex_unlock(&m_mutex);
        assert(res == 0);
        throw lock_error();
    }

    if (--m_count == 0)
    {
        assert(m_valid_id);
        m_valid_id = false;

        res = pthread_cond_signal(&m_unlocked);
        assert(res == 0);
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
#   endif
}

void recursive_mutex::do_lock(cv_state& state)
{
#   if defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    m_count = state.count;
#   else
    int res = 0;

    while (m_valid_id)
    {
        res = pthread_cond_wait(&m_unlocked, &m_mutex);
        assert(res == 0);
    }

    m_thread_id = pthread_self();
    m_valid_id = true;
    m_count = state.count;

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
#   endif
}

void recursive_mutex::do_unlock(cv_state& state)
{
#   if !defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    assert(m_valid_id);
    m_valid_id = false;

    res = pthread_cond_signal(&m_unlocked);
    assert(res == 0);
#   endif

    state.pmutex = &m_mutex;
    state.count = m_count;
    m_count = 0;
}

recursive_try_mutex::recursive_try_mutex()
    : m_count(0)
#   if !defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    , m_valid_id(false)
#   endif
{
    pthread_mutexattr_t attr;
    int res = pthread_mutexattr_init(&attr);
    assert(res == 0);

#   if defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    res = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    assert(res == 0);
#   endif

    res = pthread_mutex_init(&m_mutex, &attr);
    {
        int res = 0;
        res = pthread_mutexattr_destroy(&attr);
        assert(res == 0);
    }
    if (res != 0)
        throw thread_resource_error();

#   if !defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    res = pthread_cond_init(&m_unlocked, 0);
    if (res != 0)
    {
        pthread_mutex_destroy(&m_mutex);
        throw thread_resource_error();
    }
#   endif
}

recursive_try_mutex::~recursive_try_mutex()
{
    int res = 0;
    res = pthread_mutex_destroy(&m_mutex);
    assert(res == 0);

#   if !defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    res = pthread_cond_destroy(&m_unlocked);
    assert(res == 0);
#   endif
}

void recursive_try_mutex::do_lock()
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

#   if defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    if (++m_count > 1)
    {
        res = pthread_mutex_unlock(&m_mutex);
        assert(res == 0);
    }
#   else
    pthread_t tid = pthread_self();
    if (m_valid_id && pthread_equal(m_thread_id, tid))
        ++m_count;
    else
    {
        while (m_valid_id)
        {
            res = pthread_cond_wait(&m_unlocked, &m_mutex);
            assert(res == 0);
        }

        m_thread_id = tid;
        m_valid_id = true;
        m_count = 1;
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
#   endif
}

bool recursive_try_mutex::do_trylock()
{
#   if defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    int res = 0;
    res = pthread_mutex_trylock(&m_mutex);
    assert(res == 0 || res == EBUSY);

    if (res == 0)
    {
        if (++m_count > 1)
        {
            res = pthread_mutex_unlock(&m_mutex);
            assert(res == 0);
        }
        return true;
    }

    return false;
#   else
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    bool ret = false;
    pthread_t tid = pthread_self();
    if (m_valid_id && pthread_equal(m_thread_id, tid))
    {
        ++m_count;
        ret = true;
    }
    else if (!m_valid_id)
    {
        m_thread_id = tid;
        m_valid_id = true;
        m_count = 1;
        ret = true;
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
    return ret;
#   endif
}

void recursive_try_mutex::do_unlock()
{
#   if defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    if (--m_count == 0)
    {
        int res = 0;
        res = pthread_mutex_unlock(&m_mutex);
        assert(res == 0);
    }
#   else
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    pthread_t tid = pthread_self();
    if (m_valid_id && !pthread_equal(m_thread_id, tid))
    {
        res = pthread_mutex_unlock(&m_mutex);
        assert(res == 0);
        throw lock_error();
    }

    if (--m_count == 0)
    {
        assert(m_valid_id);
        m_valid_id = false;

        res = pthread_cond_signal(&m_unlocked);
        assert(res == 0);
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
#   endif
}

void recursive_try_mutex::do_lock(cv_state& state)
{
#   if defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    m_count = state.count;
#   else
    int res = 0;

    while (m_valid_id)
    {
        res = pthread_cond_wait(&m_unlocked, &m_mutex);
        assert(res == 0);
    }

    m_thread_id = pthread_self();
    m_valid_id = true;
    m_count = state.count;

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
#   endif
}

void recursive_try_mutex::do_unlock(cv_state& state)
{
#   if !defined(BOOST_HAS_PTHREAD_MUTEXATTR_SETTYPE)
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    assert(m_valid_id);
    m_valid_id = false;

    res = pthread_cond_signal(&m_unlocked);
    assert(res == 0);
#   endif

    state.pmutex = &m_mutex;
    state.count = m_count;
    m_count = 0;
}

recursive_timed_mutex::recursive_timed_mutex()
    : m_valid_id(false), m_count(0)
{
    int res = 0;
    res = pthread_mutex_init(&m_mutex, 0);
    if (res != 0)
        throw thread_resource_error();

    res = pthread_cond_init(&m_unlocked, 0);
    if (res != 0)
    {
        pthread_mutex_destroy(&m_mutex);
        throw thread_resource_error();
    }
}

recursive_timed_mutex::~recursive_timed_mutex()
{
    int res = 0;
    res = pthread_mutex_destroy(&m_mutex);
    assert(res == 0);

    res = pthread_cond_destroy(&m_unlocked);
    assert(res == 0);
}

void recursive_timed_mutex::do_lock()
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    pthread_t tid = pthread_self();
    if (m_valid_id && pthread_equal(m_thread_id, tid))
        ++m_count;
    else
    {
        while (m_valid_id)
        {
            res = pthread_cond_wait(&m_unlocked, &m_mutex);
            assert(res == 0);
        }

        m_thread_id = tid;
        m_valid_id = true;
        m_count = 1;
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
}

bool recursive_timed_mutex::do_trylock()
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    bool ret = false;
    pthread_t tid = pthread_self();
    if (m_valid_id && pthread_equal(m_thread_id, tid))
    {
        ++m_count;
        ret = true;
    }
    else if (!m_valid_id)
    {
        m_thread_id = tid;
        m_valid_id = true;
        m_count = 1;
        ret = true;
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
    return ret;
}

bool recursive_timed_mutex::do_timedlock(const xtime& xt)
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    bool ret = false;
    pthread_t tid = pthread_self();
    if (m_valid_id && pthread_equal(m_thread_id, tid))
    {
        ++m_count;
        ret = true;
    }
    else
    {
        timespec ts;
        to_timespec(xt, ts);

        while (m_valid_id)
        {
            res = pthread_cond_timedwait(&m_unlocked, &m_mutex, &ts);
            if (res == ETIMEDOUT)
                break;
            assert(res == 0);
        }

        if (!m_valid_id)
        {
            m_thread_id = tid;
            m_valid_id = true;
            m_count = 1;
            ret = true;
        }
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
    return ret;
}

void recursive_timed_mutex::do_unlock()
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    pthread_t tid = pthread_self();
    if (m_valid_id && !pthread_equal(m_thread_id, tid))
    {
        res = pthread_mutex_unlock(&m_mutex);
        assert(res == 0);
        throw lock_error();
    }

    if (--m_count == 0)
    {
        assert(m_valid_id);
        m_valid_id = false;

        res = pthread_cond_signal(&m_unlocked);
        assert(res == 0);
    }

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
}

void recursive_timed_mutex::do_lock(cv_state& state)
{
    int res = 0;

    while (m_valid_id)
    {
        res = pthread_cond_wait(&m_unlocked, &m_mutex);
        assert(res == 0);
    }

    m_thread_id = pthread_self();
    m_valid_id = true;
    m_count = state.count;

    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0);
}

void recursive_timed_mutex::do_unlock(cv_state& state)
{
    int res = 0;
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0);

    assert(m_valid_id);
    m_valid_id = false;

    res = pthread_cond_signal(&m_unlocked);
    assert(res == 0);

    state.pmutex = &m_mutex;
    state.count = m_count;
    m_count = 0;
}
#elif defined(BOOST_HAS_MPTASKS)

using threads::mac::detail::safe_enter_critical_region;


recursive_mutex::recursive_mutex()
    : m_count(0)
{
}

recursive_mutex::~recursive_mutex()
{
}

void recursive_mutex::do_lock()
{
    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);

    if (++m_count > 1)
    {
        lStatus = MPExitCriticalRegion(m_mutex);
        assert(lStatus == noErr);
    }
}

void recursive_mutex::do_unlock()
{
    if (--m_count == 0)
    {
        OSStatus lStatus = noErr;
        lStatus = MPExitCriticalRegion(m_mutex);
        assert(lStatus == noErr);
    }
}

void recursive_mutex::do_lock(cv_state& state)
{
    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);

    m_count = state;
}

void recursive_mutex::do_unlock(cv_state& state)
{
    state = m_count;
    m_count = 0;

    OSStatus lStatus = noErr;
    lStatus = MPExitCriticalRegion(m_mutex);
    assert(lStatus == noErr);
}

recursive_try_mutex::recursive_try_mutex()
    : m_count(0)
{
}

recursive_try_mutex::~recursive_try_mutex()
{
}

void recursive_try_mutex::do_lock()
{
    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);

    if (++m_count > 1)
    {
        lStatus = MPExitCriticalRegion(m_mutex);
        assert(lStatus == noErr);
    }
}

bool recursive_try_mutex::do_trylock()
{
    OSStatus lStatus = noErr;
    lStatus = MPEnterCriticalRegion(m_mutex, kDurationImmediate);
    assert(lStatus == noErr || lStatus == kMPTimeoutErr);

    if (lStatus == noErr)
    {
        if (++m_count > 1)
        {
            lStatus = MPExitCriticalRegion(m_mutex);
            assert(lStatus == noErr);
        }
        return true;
    }
    return false;
}

void recursive_try_mutex::do_unlock()
{
    if (--m_count == 0)
    {
        OSStatus lStatus = noErr;
        lStatus = MPExitCriticalRegion(m_mutex);
        assert(lStatus == noErr);
    }
}

void recursive_try_mutex::do_lock(cv_state& state)
{
    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);

    m_count = state;
}

void recursive_try_mutex::do_unlock(cv_state& state)
{
    state = m_count;
    m_count = 0;

    OSStatus lStatus = noErr;
    lStatus = MPExitCriticalRegion(m_mutex);
    assert(lStatus == noErr);
}

recursive_timed_mutex::recursive_timed_mutex()
    : m_count(0)
{
}

recursive_timed_mutex::~recursive_timed_mutex()
{
}

void recursive_timed_mutex::do_lock()
{
    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);

    if (++m_count > 1)
    {
        lStatus = MPExitCriticalRegion(m_mutex);
        assert(lStatus == noErr);
    }
}

bool recursive_timed_mutex::do_trylock()
{
    OSStatus lStatus = noErr;
    lStatus = MPEnterCriticalRegion(m_mutex, kDurationImmediate);
    assert(lStatus == noErr || lStatus == kMPTimeoutErr);

    if (lStatus == noErr)
    {
        if (++m_count > 1)
        {
            lStatus = MPExitCriticalRegion(m_mutex);
            assert(lStatus == noErr);
        }
        return true;
    }
    return false;
}

bool recursive_timed_mutex::do_timedlock(const xtime& xt)
{
    int microseconds;
    to_microduration(xt, microseconds);
    Duration lDuration = kDurationMicrosecond * microseconds;

    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, lDuration, m_mutex_mutex);
    assert(lStatus == noErr || lStatus == kMPTimeoutErr);

    if (lStatus == noErr)
    {
        if (++m_count > 1)
        {
            lStatus = MPExitCriticalRegion(m_mutex);
            assert(lStatus == noErr);
        }
        return true;
    }
    return false;
}

void recursive_timed_mutex::do_unlock()
{
    if (--m_count == 0)
    {
        OSStatus lStatus = noErr;
        lStatus = MPExitCriticalRegion(m_mutex);
        assert(lStatus == noErr);
    }
}

void recursive_timed_mutex::do_lock(cv_state& state)
{
    OSStatus lStatus = noErr;
    lStatus = safe_enter_critical_region(m_mutex, kDurationForever,
        m_mutex_mutex);
    assert(lStatus == noErr);

    m_count = state;
}

void recursive_timed_mutex::do_unlock(cv_state& state)
{
    state = m_count;
    m_count = 0;

    OSStatus lStatus = noErr;
    lStatus = MPExitCriticalRegion(m_mutex);
    assert(lStatus == noErr);
}
#endif

} // namespace boost

// Change Log:
//   8 Feb 01  WEKEMPF Initial version.
