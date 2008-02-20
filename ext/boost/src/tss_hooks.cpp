// Copyright (C) 2004 Michael Glassford
// Copyright (C) 2006 Roland Schwarz
// Use, modification and distribution are subject to the
// Boost Software License, Version 1.0. (See accompanying file
// LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>

#if defined(BOOST_HAS_WINTHREADS)

    #include <boost/thread/detail/tss_hooks.hpp>

    #include <boost/assert.hpp>
//    #include <boost/thread/mutex.hpp>
    #include <boost/thread/once.hpp>

    #include <list>

    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>

    namespace
    {
        class CScopedCSLock
        {
        public:
            CScopedCSLock(LPCRITICAL_SECTION cs) : cs(cs), lk(true) {
                ::EnterCriticalSection(cs);
            }
            ~CScopedCSLock() {
                if (lk) ::LeaveCriticalSection(cs);
            }
            void Unlock() {
                lk = false;
                ::LeaveCriticalSection(cs);
            }
        private:
            LPCRITICAL_SECTION cs;
            bool lk;
        };

        typedef std::list<thread_exit_handler> thread_exit_handlers;

        boost::once_flag once_init_threadmon_mutex = BOOST_ONCE_INIT;
        //boost::mutex* threadmon_mutex;
        // We don't use boost::mutex here, to avoid a memory leak report,
        // because we cannot delete it again easily.
        CRITICAL_SECTION threadmon_mutex;
        void init_threadmon_mutex(void)
        {
            //threadmon_mutex = new boost::mutex;
            //if (!threadmon_mutex)
            //    throw boost::thread_resource_error();
            ::InitializeCriticalSection(&threadmon_mutex);
        }

        const DWORD invalid_tls_key = TLS_OUT_OF_INDEXES;
        DWORD tls_key = invalid_tls_key;

        unsigned long attached_thread_count = 0;
    }

    /*
    Calls to DllMain() and tls_callback() are serialized by the OS;
    however, calls to at_thread_exit are not, so it must be protected
    by a mutex. Since we already need a mutex for at_thread_exit(),
    and since there is no guarantee that on_process_enter(),
    on_process_exit(), on_thread_enter(), and on_thread_exit()
    will be called only from DllMain() or tls_callback(), it makes
    sense to protect those, too.
    */

    extern "C" BOOST_THREAD_DECL int at_thread_exit(
        thread_exit_handler exit_handler
        )
    {
        boost::call_once(init_threadmon_mutex, once_init_threadmon_mutex);
        //boost::mutex::scoped_lock lock(*threadmon_mutex);
        CScopedCSLock lock(&threadmon_mutex);

        //Allocate a tls slot if necessary.

        if (tls_key == invalid_tls_key)
            tls_key = TlsAlloc();

        if (tls_key == invalid_tls_key)
            return -1;

        //Get the exit handlers list for the current thread from tls.

        thread_exit_handlers* exit_handlers =
            static_cast<thread_exit_handlers*>(TlsGetValue(tls_key));

        if (!exit_handlers)
        {
            //No exit handlers list was created yet.

            try
            {
                //Attempt to create a new exit handlers list.

                exit_handlers = new thread_exit_handlers;
                if (!exit_handlers)
                    return -1;

                //Attempt to store the list pointer in tls.

                if (TlsSetValue(tls_key, exit_handlers))
                    ++attached_thread_count;
                else
                {
                    delete exit_handlers;
                    return -1;
                }
            }
            catch (...)
            {
                return -1;
            }
        }

        //Like the C runtime library atexit() function,
        //functions should be called in the reverse of
        //the order they are added, so push them on the
        //front of the list.

        try
        {
            exit_handlers->push_front(exit_handler);
        }
        catch (...)
        {
            return -1;
        }

        //Like the atexit() function, a result of zero
        //indicates success.

        return 0;
    }

    extern "C" BOOST_THREAD_DECL void on_process_enter(void)
    {
        boost::call_once(init_threadmon_mutex, once_init_threadmon_mutex);
//        boost::mutex::scoped_lock lock(*threadmon_mutex);
        CScopedCSLock lock(&threadmon_mutex);

        BOOST_ASSERT(attached_thread_count == 0);
    }

    extern "C" BOOST_THREAD_DECL void on_process_exit(void)
    {
        boost::call_once(init_threadmon_mutex, once_init_threadmon_mutex);
//        boost::mutex::scoped_lock lock(*threadmon_mutex);
        CScopedCSLock lock(&threadmon_mutex);

        BOOST_ASSERT(attached_thread_count == 0);

        //Free the tls slot if one was allocated.

        if (tls_key != invalid_tls_key)
        {
            TlsFree(tls_key);
            tls_key = invalid_tls_key;
        }
    }

    extern "C" BOOST_THREAD_DECL void on_thread_enter(void)
    {
        //boost::call_once(init_threadmon_mutex, once_init_threadmon_mutex);
        //boost::mutex::scoped_lock lock(*threadmon_mutex);
    }

    extern "C" BOOST_THREAD_DECL void on_thread_exit(void)
    {
        boost::call_once(init_threadmon_mutex, once_init_threadmon_mutex);
//        boost::mutex::scoped_lock lock(*threadmon_mutex);
        CScopedCSLock lock(&threadmon_mutex);

        //Get the exit handlers list for the current thread from tls.

        if (tls_key == invalid_tls_key)
            return;

        thread_exit_handlers* exit_handlers =
            static_cast<thread_exit_handlers*>(TlsGetValue(tls_key));

        //If a handlers list was found, use it.

        if (exit_handlers && TlsSetValue(tls_key, 0))
        {
            BOOST_ASSERT(attached_thread_count > 0);
            --attached_thread_count;

            //lock.unlock();
            lock.Unlock();

            //Call each handler and remove it from the list

            while (!exit_handlers->empty())
            {
                if (thread_exit_handler exit_handler = *exit_handlers->begin())
                    (*exit_handler)();
                exit_handlers->pop_front();
            }

            delete exit_handlers;
            exit_handlers = 0;
        }
    }

#endif //defined(BOOST_HAS_WINTHREADS)
