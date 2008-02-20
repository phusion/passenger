// Copyright (C) 2001-2003
// William E. Kempf
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>

#include <boost/thread/thread.hpp>
#include <boost/thread/xtime.hpp>
#include <boost/thread/condition.hpp>
#include <cassert>

#if defined(BOOST_HAS_WINTHREADS)
#   include <windows.h>
#   if !defined(BOOST_NO_THREADEX)
#      include <process.h>
#   endif
#elif defined(BOOST_HAS_MPTASKS)
#   include <DriverServices.h>

#   include "init.hpp"
#   include "safe.hpp"
#   include <boost/thread/tss.hpp>
#endif

#include "timeconv.inl"

#if defined(BOOST_HAS_WINTHREADS)
#   include "boost/thread/detail/tss_hooks.hpp"
#endif

namespace {

#if defined(BOOST_HAS_WINTHREADS) && defined(BOOST_NO_THREADEX)
// Windows CE doesn't define _beginthreadex

struct ThreadProxyData
{
    typedef unsigned (__stdcall* func)(void*);
    func start_address_;
    void* arglist_;
    ThreadProxyData(func start_address,void* arglist) : start_address_(start_address), arglist_(arglist) {}
};

DWORD WINAPI ThreadProxy(LPVOID args)
{
    ThreadProxyData* data=reinterpret_cast<ThreadProxyData*>(args);
    DWORD ret=data->start_address_(data->arglist_);
    delete data;
    return ret;
}

inline unsigned _beginthreadex(void* security, unsigned stack_size, unsigned (__stdcall* start_address)(void*),
void* arglist, unsigned initflag,unsigned* thrdaddr)
{
    DWORD threadID;
    HANDLE hthread=CreateThread(static_cast<LPSECURITY_ATTRIBUTES>(security),stack_size,ThreadProxy,
        new ThreadProxyData(start_address,arglist),initflag,&threadID);
    if (hthread!=0)
        *thrdaddr=threadID;
    return reinterpret_cast<unsigned>(hthread);
}
#endif

class thread_param
{
public:
    thread_param(const boost::function0<void>& threadfunc)
        : m_threadfunc(threadfunc), m_started(false)
    {
    }
    void wait()
    {
        boost::mutex::scoped_lock scoped_lock(m_mutex);
        while (!m_started)
            m_condition.wait(scoped_lock);
    }
    void started()
    {
        boost::mutex::scoped_lock scoped_lock(m_mutex);
        m_started = true;
        m_condition.notify_one();
    }

    boost::mutex m_mutex;
    boost::condition m_condition;
    const boost::function0<void>& m_threadfunc;
    bool m_started;
};

} // unnamed namespace

extern "C" {
#if defined(BOOST_HAS_WINTHREADS)
    unsigned __stdcall thread_proxy(void* param)
#elif defined(BOOST_HAS_PTHREADS)
        static void* thread_proxy(void* param)
#elif defined(BOOST_HAS_MPTASKS)
        static OSStatus thread_proxy(void* param)
#endif
    {
        //try
        //{
            thread_param* p = static_cast<thread_param*>(param);
            boost::function0<void> threadfunc = p->m_threadfunc;
            p->started();
            threadfunc();
#if defined(BOOST_HAS_WINTHREADS)
            on_thread_exit();
#endif
        //}
        //catch (...)
        //{
#if defined(BOOST_HAS_WINTHREADS)
        //    on_thread_exit();
#endif
        //}
#if defined(BOOST_HAS_MPTASKS)
        ::boost::detail::thread_cleanup();
#endif
        return 0;
    }

}

namespace boost {

thread::thread()
    : m_joinable(false)
{
#if defined(BOOST_HAS_WINTHREADS)
    m_thread = reinterpret_cast<void*>(GetCurrentThread());
    m_id = GetCurrentThreadId();
#elif defined(BOOST_HAS_PTHREADS)
    m_thread = pthread_self();
#elif defined(BOOST_HAS_MPTASKS)
    threads::mac::detail::thread_init();
    threads::mac::detail::create_singletons();
    m_pTaskID = MPCurrentTaskID();
    m_pJoinQueueID = kInvalidID;
#endif
}

thread::thread(const function0<void>& threadfunc)
    : m_joinable(true)
{
    thread_param param(threadfunc);
#if defined(BOOST_HAS_WINTHREADS)
    m_thread = reinterpret_cast<void*>(_beginthreadex(0, 0, &thread_proxy,
                                           &param, 0, &m_id));
    if (!m_thread)
        throw thread_resource_error();
#elif defined(BOOST_HAS_PTHREADS)
    int res = 0;
    res = pthread_create(&m_thread, 0, &thread_proxy, &param);
    if (res != 0)
        throw thread_resource_error();
#elif defined(BOOST_HAS_MPTASKS)
    threads::mac::detail::thread_init();
    threads::mac::detail::create_singletons();
    OSStatus lStatus = noErr;

    m_pJoinQueueID = kInvalidID;
    m_pTaskID = kInvalidID;

    lStatus = MPCreateQueue(&m_pJoinQueueID);
    if (lStatus != noErr) throw thread_resource_error();

    lStatus = MPCreateTask(&thread_proxy, &param, 0UL, m_pJoinQueueID, NULL,
        NULL, 0UL, &m_pTaskID);
    if (lStatus != noErr)
    {
        lStatus = MPDeleteQueue(m_pJoinQueueID);
        assert(lStatus == noErr);
        throw thread_resource_error();
    }
#endif
    param.wait();
}

thread::~thread()
{
    if (m_joinable)
    {
#if defined(BOOST_HAS_WINTHREADS)
        int res = 0;
        res = CloseHandle(reinterpret_cast<HANDLE>(m_thread));
        assert(res);
#elif defined(BOOST_HAS_PTHREADS)
        pthread_detach(m_thread);
#elif defined(BOOST_HAS_MPTASKS)
        assert(m_pJoinQueueID != kInvalidID);
        OSStatus lStatus = MPDeleteQueue(m_pJoinQueueID);
        assert(lStatus == noErr);
#endif
    }
}

bool thread::operator==(const thread& other) const
{
#if defined(BOOST_HAS_WINTHREADS)
    return other.m_id == m_id;
#elif defined(BOOST_HAS_PTHREADS)
    return pthread_equal(m_thread, other.m_thread) != 0;
#elif defined(BOOST_HAS_MPTASKS)
    return other.m_pTaskID == m_pTaskID;
#endif
}

bool thread::operator!=(const thread& other) const
{
    return !operator==(other);
}

void thread::join()
{
    assert(m_joinable); //See race condition comment below
    int res = 0;
#if defined(BOOST_HAS_WINTHREADS)
    res = WaitForSingleObject(reinterpret_cast<HANDLE>(m_thread), INFINITE);
    assert(res == WAIT_OBJECT_0);
    res = CloseHandle(reinterpret_cast<HANDLE>(m_thread));
    assert(res);
#elif defined(BOOST_HAS_PTHREADS)
    res = pthread_join(m_thread, 0);
    assert(res == 0);
#elif defined(BOOST_HAS_MPTASKS)
    OSStatus lStatus = threads::mac::detail::safe_wait_on_queue(
        m_pJoinQueueID, NULL, NULL, NULL, kDurationForever);
    assert(lStatus == noErr);
#endif
    // This isn't a race condition since any race that could occur would
    // have us in undefined behavior territory any way.
    m_joinable = false;
}

void thread::sleep(const xtime& xt)
{
    for (int foo=0; foo < 5; ++foo)
    {
#if defined(BOOST_HAS_WINTHREADS)
        int milliseconds;
        to_duration(xt, milliseconds);
        Sleep(milliseconds);
#elif defined(BOOST_HAS_PTHREADS)
#   if defined(BOOST_HAS_PTHREAD_DELAY_NP)
        timespec ts;
        to_timespec_duration(xt, ts);
        int res = 0;
        res = pthread_delay_np(&ts);
        assert(res == 0);
#   elif defined(BOOST_HAS_NANOSLEEP)
        timespec ts;
        to_timespec_duration(xt, ts);

        //  nanosleep takes a timespec that is an offset, not
        //  an absolute time.
        nanosleep(&ts, 0);
#   else
        mutex mx;
        mutex::scoped_lock lock(mx);
        condition cond;
        cond.timed_wait(lock, xt);
#   endif
#elif defined(BOOST_HAS_MPTASKS)
        int microseconds;
        to_microduration(xt, microseconds);
        Duration lMicroseconds(kDurationMicrosecond * microseconds);
        AbsoluteTime sWakeTime(DurationToAbsolute(lMicroseconds));
        threads::mac::detail::safe_delay_until(&sWakeTime);
#endif
        xtime cur;
        xtime_get(&cur, TIME_UTC);
        if (xtime_cmp(xt, cur) <= 0)
            return;
    }
}

void thread::yield()
{
#if defined(BOOST_HAS_WINTHREADS)
    Sleep(0);
#elif defined(BOOST_HAS_PTHREADS)
#   if defined(BOOST_HAS_SCHED_YIELD)
    int res = 0;
    res = sched_yield();
    assert(res == 0);
#   elif defined(BOOST_HAS_PTHREAD_YIELD)
    int res = 0;
    res = pthread_yield();
    assert(res == 0);
#   else
    xtime xt;
    xtime_get(&xt, TIME_UTC);
    sleep(xt);
#   endif
#elif defined(BOOST_HAS_MPTASKS)
    MPYield();
#endif
}

thread_group::thread_group()
{
}

thread_group::~thread_group()
{
    // We shouldn't have to scoped_lock here, since referencing this object
    // from another thread while we're deleting it in the current thread is
    // going to lead to undefined behavior any way.
    for (std::list<thread*>::iterator it = m_threads.begin();
         it != m_threads.end(); ++it)
    {
        delete (*it);
    }
}

thread* thread_group::create_thread(const function0<void>& threadfunc)
{
    // No scoped_lock required here since the only "shared data" that's
    // modified here occurs inside add_thread which does scoped_lock.
    std::auto_ptr<thread> thrd(new thread(threadfunc));
    add_thread(thrd.get());
    return thrd.release();
}

void thread_group::add_thread(thread* thrd)
{
    mutex::scoped_lock scoped_lock(m_mutex);

    // For now we'll simply ignore requests to add a thread object multiple
    // times. Should we consider this an error and either throw or return an
    // error value?
    std::list<thread*>::iterator it = std::find(m_threads.begin(),
        m_threads.end(), thrd);
    assert(it == m_threads.end());
    if (it == m_threads.end())
        m_threads.push_back(thrd);
}

void thread_group::remove_thread(thread* thrd)
{
    mutex::scoped_lock scoped_lock(m_mutex);

    // For now we'll simply ignore requests to remove a thread object that's
    // not in the group. Should we consider this an error and either throw or
    // return an error value?
    std::list<thread*>::iterator it = std::find(m_threads.begin(),
        m_threads.end(), thrd);
    assert(it != m_threads.end());
    if (it != m_threads.end())
        m_threads.erase(it);
}

void thread_group::join_all()
{
    mutex::scoped_lock scoped_lock(m_mutex);
    for (std::list<thread*>::iterator it = m_threads.begin();
         it != m_threads.end(); ++it)
    {
        (*it)->join();
    }
}

int thread_group::size()
{
        return m_threads.size();
}

} // namespace boost
