#ifndef BOOST_THREAD_THREAD_PTHREAD_HPP
#define BOOST_THREAD_THREAD_PTHREAD_HPP
// Copyright (C) 2001-2003
// William E. Kempf
// Copyright (C) 2007 Anthony Williams
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>

#include <boost/utility.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <list>
#include <memory>

#include <pthread.h>
#include <boost/optional.hpp>
#include <boost/thread/detail/move.hpp>
#include <boost/shared_ptr.hpp>
#include "thread_data.hpp"
#include <stdlib.h>

#ifdef BOOST_MSVC
#pragma warning(push)
#pragma warning(disable:4251)
#endif

namespace boost
{
    class thread;

    namespace detail
    {
        class thread_id;
    }
    
    namespace this_thread
    {
        BOOST_THREAD_DECL detail::thread_id get_id();
    }
    
    namespace detail
    {
        class thread_id
        {
        private:
            detail::thread_data_ptr thread_data;
            
            thread_id(detail::thread_data_ptr thread_data_):
                thread_data(thread_data_)
            {}
            friend class boost::thread;
            friend thread_id this_thread::get_id();
        public:
            thread_id():
                thread_data()
            {}
            
            bool operator==(const thread_id& y) const
            {
                return thread_data==y.thread_data;
            }
        
            bool operator!=(const thread_id& y) const
            {
                return thread_data!=y.thread_data;
            }
        
            bool operator<(const thread_id& y) const
            {
                return thread_data<y.thread_data;
            }
        
            bool operator>(const thread_id& y) const
            {
                return y.thread_data<thread_data;
            }
        
            bool operator<=(const thread_id& y) const
            {
                return !(y.thread_data<thread_data);
            }
        
            bool operator>=(const thread_id& y) const
            {
                return !(thread_data<y.thread_data);
            }

            template<class charT, class traits>
            friend std::basic_ostream<charT, traits>& 
            operator<<(std::basic_ostream<charT, traits>& os, const thread_id& x)
            {
                if(x.thread_data)
                {
                    return os<<x.thread_data;
                }
                else
                {
                    return os<<"{Not-any-thread}";
                }
            }
        };
    }

    struct xtime;
    class BOOST_THREAD_DECL thread
    {
    private:
        thread(thread&);
        thread& operator=(thread&);

        template<typename F>
        struct thread_data:
            detail::thread_data_base
        {
            F f;

            thread_data(F f_):
                f(f_)
            {}
            thread_data(detail::thread_move_t<F> f_):
                f(f_)
            {}
            
            void run()
            {
                f();
            }
        };
        
        mutable boost::mutex thread_info_mutex;
        detail::thread_data_ptr thread_info;

        void start_thread();
        
        explicit thread(detail::thread_data_ptr data);

        detail::thread_data_ptr get_thread_info() const;
        
    public:
        thread();
        ~thread();

        template <class F>
        explicit thread(F f):
            thread_info(new thread_data<F>(f))
        {
            start_thread();
        }
        template <class F>
        thread(detail::thread_move_t<F> f):
            thread_info(new thread_data<F>(f))
        {
            start_thread();
        }

        thread(detail::thread_move_t<thread> x);
        thread& operator=(detail::thread_move_t<thread> x);
        operator detail::thread_move_t<thread>();
        detail::thread_move_t<thread> move();

        void swap(thread& x);

        typedef detail::thread_id id;
        
        id get_id() const;

        bool joinable() const;
        void join();
        bool timed_join(const system_time& wait_until);

        template<typename TimeDuration>
        inline bool timed_join(TimeDuration const& rel_time)
        {
            return timed_join(get_system_time()+rel_time);
        }
        void detach();

        static unsigned hardware_concurrency();

        // backwards compatibility
        bool operator==(const thread& other) const;
        bool operator!=(const thread& other) const;

        static void sleep(const system_time& xt);
        static void yield();

        // extensions
        void interrupt();
        bool interruption_requested() const;
    };

    inline detail::thread_move_t<thread> move(thread& x)
    {
        return x.move();
    }
    
    inline detail::thread_move_t<thread> move(detail::thread_move_t<thread> x)
    {
        return x;
    }


    template<typename F>
    struct thread::thread_data<boost::reference_wrapper<F> >:
        detail::thread_data_base
    {
        F& f;
        
        thread_data(boost::reference_wrapper<F> f_):
            f(f_)
        {}
        
        void run()
        {
            f();
        }
    };

    namespace this_thread
    {
        class BOOST_THREAD_DECL disable_interruption
        {
            disable_interruption(const disable_interruption&);
            disable_interruption& operator=(const disable_interruption&);
            
            bool interruption_was_enabled;
            friend class restore_interruption;
        public:
            disable_interruption();
            ~disable_interruption();
        };

        class BOOST_THREAD_DECL restore_interruption
        {
            restore_interruption(const restore_interruption&);
            restore_interruption& operator=(const restore_interruption&);
        public:
            explicit restore_interruption(disable_interruption& d);
            ~restore_interruption();
        };

        BOOST_THREAD_DECL thread::id get_id();

        BOOST_THREAD_DECL void interruption_point();
        BOOST_THREAD_DECL bool interruption_enabled();
        BOOST_THREAD_DECL bool interruption_requested();

        inline void yield()
        {
            thread::yield();
        }
        
        template<typename TimeDuration>
        inline void sleep(TimeDuration const& rel_time)
        {
            thread::sleep(get_system_time()+rel_time);
        }
    }

    namespace detail
    {
        struct thread_exit_function_base
        {
            virtual ~thread_exit_function_base()
            {}
            virtual void operator()() const=0;
        };
        
        template<typename F>
        struct thread_exit_function:
            thread_exit_function_base
        {
            F f;
            
            thread_exit_function(F f_):
                f(f_)
            {}
            
            void operator()() const
            {
                f();
            }
        };
        
        BOOST_THREAD_DECL void add_thread_exit_function(thread_exit_function_base*);
    }
    
    namespace this_thread
    {
        template<typename F>
        inline void at_thread_exit(F f)
        {
            detail::thread_exit_function_base* const thread_exit_func=new detail::thread_exit_function<F>(f);
            detail::add_thread_exit_function(thread_exit_func);
        }
    }

    class BOOST_THREAD_DECL thread_group
    {
    public:
        thread_group();
        ~thread_group();

        thread* create_thread(const function0<void>& threadfunc);
        void add_thread(thread* thrd);
        void remove_thread(thread* thrd);
        void join_all();
        void interrupt_all();
        size_t size() const;

    private:
        thread_group(thread_group&);
        void operator=(thread_group&);
        
        std::list<thread*> m_threads;
        mutex m_mutex;
    };
} // namespace boost

#ifdef BOOST_MSVC
#pragma warning(pop)
#endif


#endif
