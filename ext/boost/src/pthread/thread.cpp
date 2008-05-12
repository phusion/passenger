// Copyright (C) 2001-2003
// William E. Kempf
// Copyright (C) 2007 Anthony Williams
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>

#include <boost/thread/thread.hpp>
#include <boost/thread/xtime.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/once.hpp>
#include <boost/thread/tss.hpp>
#ifdef __linux__
#include <sys/sysinfo.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(__sun)
#include <unistd.h>
#endif

#include "timeconv.inl"

namespace boost
{
    namespace detail
    {
        struct thread_exit_callback_node
        {
            boost::detail::thread_exit_function_base* func;
            thread_exit_callback_node* next;

            thread_exit_callback_node(boost::detail::thread_exit_function_base* func_,
                                      thread_exit_callback_node* next_):
                func(func_),next(next_)
            {}
        };

        struct tss_data_node
        {
            void const* key;
            boost::shared_ptr<boost::detail::tss_cleanup_function> func;
            void* value;
            tss_data_node* next;

            tss_data_node(void const* key_,boost::shared_ptr<boost::detail::tss_cleanup_function> func_,void* value_,
                          tss_data_node* next_):
                key(key_),func(func_),value(value_),next(next_)
            {}
        };

        namespace
        {
            boost::once_flag current_thread_tls_init_flag=BOOST_ONCE_INIT;
            pthread_key_t current_thread_tls_key;

            extern "C"
            {
                void tls_destructor(void* data)
                {
                    boost::detail::thread_data_base* thread_info=static_cast<boost::detail::thread_data_base*>(data);
                    if(thread_info)
                    {
                        while(thread_info->tss_data || thread_info->thread_exit_callbacks)
                        {
                            while(thread_info->thread_exit_callbacks)
                            {
                                detail::thread_exit_callback_node* const current_node=thread_info->thread_exit_callbacks;
                                thread_info->thread_exit_callbacks=current_node->next;
                                if(current_node->func)
                                {
                                    (*current_node->func)();
                                    delete current_node->func;
                                }
                                delete current_node;
                            }
                            while(thread_info->tss_data)
                            {
                                detail::tss_data_node* const current_node=thread_info->tss_data;
                                thread_info->tss_data=current_node->next;
                                if(current_node->func)
                                {
                                    (*current_node->func)(current_node->value);
                                }
                                delete current_node;
                            }
                        }
                        thread_info->self.reset();
                    }
                }
            }
    

            void create_current_thread_tls_key()
            {
                BOOST_VERIFY(!pthread_key_create(&current_thread_tls_key,&tls_destructor));
            }
        }
        
        boost::detail::thread_data_base* get_current_thread_data()
        {
            boost::call_once(current_thread_tls_init_flag,create_current_thread_tls_key);
            return (boost::detail::thread_data_base*)pthread_getspecific(current_thread_tls_key);
        }

        void set_current_thread_data(detail::thread_data_base* new_data)
        {
            boost::call_once(current_thread_tls_init_flag,create_current_thread_tls_key);
            BOOST_VERIFY(!pthread_setspecific(current_thread_tls_key,new_data));
        }
    }
    
    namespace
    {
        extern "C"
        {
            void* thread_proxy(void* param)
            {
                boost::shared_ptr<boost::detail::thread_data_base> thread_info = static_cast<boost::detail::thread_data_base*>(param)->self;
                thread_info->self.reset();
                detail::set_current_thread_data(thread_info.get());
                try
                {
                    thread_info->run();
                }
                catch(thread_interrupted const&)
                {
                }
                catch(...)
                {
                    std::terminate();
                }

                detail::tls_destructor(thread_info.get());
                detail::set_current_thread_data(0);
                boost::lock_guard<boost::mutex> lock(thread_info->data_mutex);
                thread_info->done=true;
                thread_info->done_condition.notify_all();
                return 0;
            }
        }

        struct externally_launched_thread:
            detail::thread_data_base
        {
            externally_launched_thread()
            {
                interrupt_enabled=false;
            }
            
            void run()
            {}
        };

        detail::thread_data_base* make_external_thread_data()
        {
            detail::thread_data_base* const me(new externally_launched_thread());
            me->self.reset(me);
            set_current_thread_data(me);
            return me;
        }


        detail::thread_data_base* get_or_make_current_thread_data()
        {
            detail::thread_data_base* current_thread_data(detail::get_current_thread_data());
            if(!current_thread_data)
            {
                current_thread_data=make_external_thread_data();
            }
            return current_thread_data;
        }

    }


    thread::thread()
    {}

    void thread::start_thread()
    {
        thread_info->self=thread_info;
        int const res = pthread_create(&thread_info->thread_handle, 0, &thread_proxy, thread_info.get());
        if (res != 0)
        {
            thread_info->self.reset();
            throw thread_resource_error();
        }
    }

    thread::~thread()
    {
        detach();
    }

    thread::thread(detail::thread_move_t<thread> x)
    {
        lock_guard<mutex> lock(x->thread_info_mutex);
        thread_info=x->thread_info;
        x->thread_info.reset();
    }
    
    thread& thread::operator=(detail::thread_move_t<thread> x)
    {
        thread new_thread(x);
        swap(new_thread);
        return *this;
    }
        
    thread::operator detail::thread_move_t<thread>()
    {
        return move();
    }

    detail::thread_move_t<thread> thread::move()
    {
        detail::thread_move_t<thread> x(*this);
        return x;
    }

    void thread::swap(thread& x)
    {
        thread_info.swap(x.thread_info);
    }


    bool thread::operator==(const thread& other) const
    {
        return get_id()==other.get_id();
    }

    bool thread::operator!=(const thread& other) const
    {
        return !operator==(other);
    }

    detail::thread_data_ptr thread::get_thread_info() const
    {
        lock_guard<mutex> l(thread_info_mutex);
        return thread_info;
    }

    void thread::join()
    {
        detail::thread_data_ptr const local_thread_info=get_thread_info();
        if(local_thread_info)
        {
            bool do_join=false;
            
            {
                unique_lock<mutex> lock(local_thread_info->data_mutex);
                while(!local_thread_info->done)
                {
                    local_thread_info->done_condition.wait(lock);
                }
                do_join=!local_thread_info->join_started;
                
                if(do_join)
                {
                    local_thread_info->join_started=true;
                }
                else
                {
                    while(!local_thread_info->joined)
                    {
                        local_thread_info->done_condition.wait(lock);
                    }
                }
            }
            if(do_join)
            {
                void* result=0;
                BOOST_VERIFY(!pthread_join(local_thread_info->thread_handle,&result));
                lock_guard<mutex> lock(local_thread_info->data_mutex);
                local_thread_info->joined=true;
                local_thread_info->done_condition.notify_all();
            }
            
            lock_guard<mutex> l1(thread_info_mutex);
            if(thread_info==local_thread_info)
            {
                thread_info.reset();
            }
        }
    }

    bool thread::timed_join(system_time const& wait_until)
    {
        detail::thread_data_ptr const local_thread_info=get_thread_info();
        if(local_thread_info)
        {
            bool do_join=false;
            
            {
                unique_lock<mutex> lock(local_thread_info->data_mutex);
                while(!local_thread_info->done)
                {
                    if(!local_thread_info->done_condition.timed_wait(lock,wait_until))
                    {
                        return false;
                    }
                }
                do_join=!local_thread_info->join_started;
                
                if(do_join)
                {
                    local_thread_info->join_started=true;
                }
                else
                {
                    while(!local_thread_info->joined)
                    {
                        local_thread_info->done_condition.wait(lock);
                    }
                }
            }
            if(do_join)
            {
                void* result=0;
                BOOST_VERIFY(!pthread_join(local_thread_info->thread_handle,&result));
                lock_guard<mutex> lock(local_thread_info->data_mutex);
                local_thread_info->joined=true;
                local_thread_info->done_condition.notify_all();
            }
            
            lock_guard<mutex> l1(thread_info_mutex);
            if(thread_info==local_thread_info)
            {
                thread_info.reset();
            }
        }
        return true;
    }

    bool thread::joinable() const
    {
        return get_thread_info();
    }


    void thread::detach()
    {
        detail::thread_data_ptr local_thread_info;
        {
            lock_guard<mutex> l1(thread_info_mutex);
            thread_info.swap(local_thread_info);
        }
        
        if(local_thread_info)
        {
            lock_guard<mutex> lock(local_thread_info->data_mutex);
            if(!local_thread_info->join_started)
            {
                BOOST_VERIFY(!pthread_detach(local_thread_info->thread_handle));
                local_thread_info->join_started=true;
                local_thread_info->joined=true;
            }
        }
    }

    void thread::sleep(const system_time& st)
    {
        detail::thread_data_base* const thread_info=detail::get_current_thread_data();
        
        if(thread_info)
        {
            unique_lock<mutex> lk(thread_info->sleep_mutex);
            while(thread_info->sleep_condition.timed_wait(lk,st));
        }
        else
        {
            xtime const xt=get_xtime(st);
            
            for (int foo=0; foo < 5; ++foo)
            {
#   if defined(BOOST_HAS_PTHREAD_DELAY_NP)
                timespec ts;
                to_timespec_duration(xt, ts);
                BOOST_VERIFY(!pthread_delay_np(&ts));
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
                xtime cur;
                xtime_get(&cur, TIME_UTC);
                if (xtime_cmp(xt, cur) <= 0)
                    return;
            }
        }
    }

    void thread::yield()
    {
#   if defined(BOOST_HAS_SCHED_YIELD)
        BOOST_VERIFY(!sched_yield());
#   elif defined(BOOST_HAS_PTHREAD_YIELD)
        BOOST_VERIFY(!pthread_yield());
#   else
        xtime xt;
        xtime_get(&xt, TIME_UTC);
        sleep(xt);
#   endif
    }

    unsigned thread::hardware_concurrency()
    {
#if defined(PTW32_VERSION) || defined(__hpux)
        return pthread_num_processors_np();
#elif defined(__linux__)
        return get_nprocs();
#elif defined(__APPLE__) || defined(__FreeBSD__)
        int count;
        size_t size=sizeof(count);
        return sysctlbyname("hw.ncpu",&count,&size,NULL,0)?0:count;
#elif defined(__sun)
        int const count=sysconf(_SC_NPROCESSORS_ONLN);
        return (count>0)?count:0;
#else
        return 0;
#endif
    }

    thread::id thread::get_id() const
    {
        detail::thread_data_ptr const local_thread_info=get_thread_info();
        if(local_thread_info)
        {
            return id(local_thread_info);
        }
        else
        {
            return id();
        }
    }

    void thread::interrupt()
    {
        detail::thread_data_ptr const local_thread_info=get_thread_info();
        if(local_thread_info)
        {
            lock_guard<mutex> lk(local_thread_info->data_mutex);
            local_thread_info->interrupt_requested=true;
            if(local_thread_info->current_cond)
            {
                BOOST_VERIFY(!pthread_cond_broadcast(local_thread_info->current_cond));
            }
        }
    }

    bool thread::interruption_requested() const
    {
        detail::thread_data_ptr const local_thread_info=get_thread_info();
        if(local_thread_info)
        {
            lock_guard<mutex> lk(local_thread_info->data_mutex);
            return local_thread_info->interrupt_requested;
        }
        else
        {
            return false;
        }
    }
    

    namespace this_thread
    {
        thread::id get_id()
        {
            boost::detail::thread_data_base* const thread_info=get_or_make_current_thread_data();
            return thread::id(thread_info?thread_info->shared_from_this():detail::thread_data_ptr());
        }

        void interruption_point()
        {
            boost::detail::thread_data_base* const thread_info=detail::get_current_thread_data();
            if(thread_info && thread_info->interrupt_enabled)
            {
                lock_guard<mutex> lg(thread_info->data_mutex);
                if(thread_info->interrupt_requested)
                {
                    thread_info->interrupt_requested=false;
                    throw thread_interrupted();
                }
            }
        }
        
        bool interruption_enabled()
        {
            boost::detail::thread_data_base* const thread_info=detail::get_current_thread_data();
            return thread_info && thread_info->interrupt_enabled;
        }
        
        bool interruption_requested()
        {
            boost::detail::thread_data_base* const thread_info=detail::get_current_thread_data();
            if(!thread_info)
            {
                return false;
            }
            else
            {
                lock_guard<mutex> lg(thread_info->data_mutex);
                return thread_info->interrupt_requested;
            }
        }

        disable_interruption::disable_interruption():
            interruption_was_enabled(interruption_enabled())
        {
            if(interruption_was_enabled)
            {
                detail::get_current_thread_data()->interrupt_enabled=false;
            }
        }
        
        disable_interruption::~disable_interruption()
        {
            if(detail::get_current_thread_data())
            {
                detail::get_current_thread_data()->interrupt_enabled=interruption_was_enabled;
            }
        }

        restore_interruption::restore_interruption(disable_interruption& d)
        {
            if(d.interruption_was_enabled)
            {
                detail::get_current_thread_data()->interrupt_enabled=true;
            }
        }
        
        restore_interruption::~restore_interruption()
        {
            if(detail::get_current_thread_data())
            {
                detail::get_current_thread_data()->interrupt_enabled=false;
            }
        }
    }

    namespace detail
    {
        void add_thread_exit_function(thread_exit_function_base* func)
        {
            detail::thread_data_base* const current_thread_data(get_or_make_current_thread_data());
            thread_exit_callback_node* const new_node=
                new thread_exit_callback_node(func,current_thread_data->thread_exit_callbacks);
            current_thread_data->thread_exit_callbacks=new_node;
        }

        tss_data_node* find_tss_data(void const* key)
        {
            detail::thread_data_base* const current_thread_data(get_current_thread_data());
            if(current_thread_data)
            {
                detail::tss_data_node* current_node=current_thread_data->tss_data;
                while(current_node)
                {
                    if(current_node->key==key)
                    {
                        return current_node;
                    }
                    current_node=current_node->next;
                }
            }
            return NULL;
        }

        void* get_tss_data(void const* key)
        {
            if(tss_data_node* const current_node=find_tss_data(key))
            {
                return current_node->value;
            }
            return NULL;
        }
        
        void set_tss_data(void const* key,boost::shared_ptr<tss_cleanup_function> func,void* tss_data,bool cleanup_existing)
        {
            if(tss_data_node* const current_node=find_tss_data(key))
            {
                if(cleanup_existing && current_node->func)
                {
                    (*current_node->func)(current_node->value);
                }
                current_node->func=func;
                current_node->value=tss_data;
            }
            else
            {
                detail::thread_data_base* const current_thread_data(get_or_make_current_thread_data());
                tss_data_node* const new_node=new tss_data_node(key,func,tss_data,current_thread_data->tss_data);
                current_thread_data->tss_data=new_node;
            }
        }
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
        BOOST_ASSERT(it == m_threads.end());
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
        BOOST_ASSERT(it != m_threads.end());
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

    void thread_group::interrupt_all()
    {
        boost::lock_guard<mutex> guard(m_mutex);
            
        for(std::list<thread*>::iterator it=m_threads.begin(),end=m_threads.end();
            it!=end;
            ++it)
        {
            (*it)->interrupt();
        }
    }
        

    size_t thread_group::size() const
    {
        return m_threads.size();
    }

}
