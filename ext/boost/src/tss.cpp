// Copyright (C) 2001-2003 William E. Kempf
// Copyright (C) 2006 Roland Schwarz
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>

#include <boost/thread/tss.hpp>
#ifndef BOOST_THREAD_NO_TSS_CLEANUP

#include <boost/thread/once.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/exceptions.hpp>
#include <vector>
#include <string>
#include <stdexcept>
#include <cassert>

#if defined(BOOST_HAS_WINTHREADS)
#   include <windows.h>
#   include <boost/thread/detail/tss_hooks.hpp>
#endif

namespace {

typedef std::vector<void*> tss_slots;
typedef std::vector<boost::function1<void, void*>*> tss_data_cleanup_handlers_type;

boost::once_flag tss_data_once = BOOST_ONCE_INIT;
boost::mutex* tss_data_mutex = 0;
tss_data_cleanup_handlers_type* tss_data_cleanup_handlers = 0;
#if defined(BOOST_HAS_WINTHREADS)
    DWORD tss_data_native_key=TLS_OUT_OF_INDEXES;
#elif defined(BOOST_HAS_PTHREADS)
    pthread_key_t tss_data_native_key;
#elif defined(BOOST_HAS_MPTASKS)
    TaskStorageIndex tss_data_native_key;
#endif
int tss_data_use = 0;

void tss_data_inc_use(boost::mutex::scoped_lock& lk)
{
    ++tss_data_use;
}

void tss_data_dec_use(boost::mutex::scoped_lock& lk)
{
    if (0 == --tss_data_use)
    {
        tss_data_cleanup_handlers_type::size_type i;
        for (i = 0; i < tss_data_cleanup_handlers->size(); ++i)
        {
            delete (*tss_data_cleanup_handlers)[i];
        }
        delete tss_data_cleanup_handlers;
        tss_data_cleanup_handlers = 0;
        lk.unlock();
        delete tss_data_mutex;
        tss_data_mutex = 0;
#if defined(BOOST_HAS_WINTHREADS)
        TlsFree(tss_data_native_key);
        tss_data_native_key=TLS_OUT_OF_INDEXES;
#elif defined(BOOST_HAS_PTHREADS)
        pthread_key_delete(tss_data_native_key);
#elif defined(BOOST_HAS_MPTASKS)
        // Don't know what to put here.
        // But MPTASKS isn't currently maintained anyways...
#endif
    }
}

extern "C" void cleanup_slots(void* p)
{
    tss_slots* slots = static_cast<tss_slots*>(p);
    boost::mutex::scoped_lock lock(*tss_data_mutex);
    for (tss_slots::size_type i = 0; i < slots->size(); ++i)
    {
        (*(*tss_data_cleanup_handlers)[i])((*slots)[i]);
        (*slots)[i] = 0;
    }
#if defined(BOOST_HAS_WINTHREADS)
    TlsSetValue(tss_data_native_key,0);
#endif
    tss_data_dec_use(lock);
    delete slots;
}

void init_tss_data()
{
    std::auto_ptr<tss_data_cleanup_handlers_type> 
        temp(new tss_data_cleanup_handlers_type);

    std::auto_ptr<boost::mutex> temp_mutex(new boost::mutex);
    if (temp_mutex.get() == 0)
        throw boost::thread_resource_error();

#if defined(BOOST_HAS_WINTHREADS)
    //Force the cleanup implementation library to be linked in
    //tss_cleanup_implemented();

    //Allocate tls slot
    tss_data_native_key = TlsAlloc();
    if (tss_data_native_key == TLS_OUT_OF_INDEXES)
        return;
#elif defined(BOOST_HAS_PTHREADS)
    int res = pthread_key_create(&tss_data_native_key, &cleanup_slots);
    if (res != 0)
        return;
#elif defined(BOOST_HAS_MPTASKS)
    OSStatus status = MPAllocateTaskStorageIndex(&tss_data_native_key);
    if (status != noErr)
        return;
#endif

    // The life time of cleanup handlers and mutex is beeing
    // managed by a reference counting technique.
    // This avoids a memory leak by releasing the global data
    // after last use only, since the execution order of cleanup
    // handlers is unspecified on any platform with regards to
    // C++ destructor ordering rules.
    tss_data_cleanup_handlers = temp.release();
    tss_data_mutex = temp_mutex.release();
}

#if defined(BOOST_HAS_WINTHREADS)
tss_slots* get_slots(bool alloc);

void __cdecl tss_thread_exit()
{
    tss_slots* slots = get_slots(false);
    if (slots)
        cleanup_slots(slots);
}
#endif

tss_slots* get_slots(bool alloc)
{
    tss_slots* slots = 0;

#if defined(BOOST_HAS_WINTHREADS)
    slots = static_cast<tss_slots*>(
        TlsGetValue(tss_data_native_key));
#elif defined(BOOST_HAS_PTHREADS)
    slots = static_cast<tss_slots*>(
        pthread_getspecific(tss_data_native_key));
#elif defined(BOOST_HAS_MPTASKS)
    slots = static_cast<tss_slots*>(
        MPGetTaskStorageValue(tss_data_native_key));
#endif

    if (slots == 0 && alloc)
    {
        std::auto_ptr<tss_slots> temp(new tss_slots);

#if defined(BOOST_HAS_WINTHREADS)
        if (at_thread_exit(&tss_thread_exit) == -1)
            return 0;
        if (!TlsSetValue(tss_data_native_key, temp.get()))
            return 0;
#elif defined(BOOST_HAS_PTHREADS)
        if (pthread_setspecific(tss_data_native_key, temp.get()) != 0)
            return 0;
#elif defined(BOOST_HAS_MPTASKS)
        if (MPSetTaskStorageValue(tss_data_native_key, temp.get()) != noErr)
            return 0;
#endif
        {
            boost::mutex::scoped_lock lock(*tss_data_mutex);
            tss_data_inc_use(lock);
        }
        slots = temp.release();
    }

    return slots;
}

} // namespace

namespace boost {

namespace detail {
void tss::init(boost::function1<void, void*>* pcleanup)
{
    boost::call_once(&init_tss_data, tss_data_once);
    if (tss_data_cleanup_handlers == 0)
        throw thread_resource_error();
    boost::mutex::scoped_lock lock(*tss_data_mutex);
    try
    {
        tss_data_cleanup_handlers->push_back(pcleanup);
        m_slot = tss_data_cleanup_handlers->size() - 1;
        tss_data_inc_use(lock);
    }
    catch (...)
    {
        throw thread_resource_error();
    }
}

tss::~tss()
{
    boost::mutex::scoped_lock lock(*tss_data_mutex);
    tss_data_dec_use(lock);
}

void* tss::get() const
{
    tss_slots* slots = get_slots(false);

    if (!slots)
        return 0;

    if (m_slot >= slots->size())
        return 0;

    return (*slots)[m_slot];
}

void tss::set(void* value)
{
    tss_slots* slots = get_slots(true);

    if (!slots)
        throw boost::thread_resource_error();

    if (m_slot >= slots->size())
    {
        try
        {
            slots->resize(m_slot + 1);
        }
        catch (...)
        {
            throw boost::thread_resource_error();
        }
    }

    (*slots)[m_slot] = value;
}

void tss::cleanup(void* value)
{
    boost::mutex::scoped_lock lock(*tss_data_mutex);
    (*(*tss_data_cleanup_handlers)[m_slot])(value);
}

} // namespace detail
} // namespace boost

#endif //BOOST_THREAD_NO_TSS_CLEANUP
