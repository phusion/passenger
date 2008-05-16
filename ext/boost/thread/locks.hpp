// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
// (C) Copyright 2007 Anthony Williams
#ifndef BOOST_THREAD_LOCKS_HPP
#define BOOST_THREAD_LOCKS_HPP
#include <boost/thread/detail/config.hpp>
#include <boost/thread/exceptions.hpp>
#include <boost/thread/detail/move.hpp>
#include <algorithm>
#include <boost/thread/thread_time.hpp>

namespace boost
{
    struct defer_lock_t
    {};
    struct try_to_lock_t
    {};
    struct adopt_lock_t
    {};
    
    const defer_lock_t defer_lock={};
    const try_to_lock_t try_to_lock={};
    const adopt_lock_t adopt_lock={};

    template<typename Mutex>
    class shared_lock;

    template<typename Mutex>
    class upgrade_lock;

    template<typename Mutex>
    class lock_guard
    {
    private:
        Mutex& m;

        explicit lock_guard(lock_guard&);
        lock_guard& operator=(lock_guard&);
    public:
        explicit lock_guard(Mutex& m_):
            m(m_)
        {
            m.lock();
        }
        lock_guard(Mutex& m_,adopt_lock_t):
            m(m_)
        {}
        ~lock_guard()
        {
            m.unlock();
        }
    };


    template<typename Mutex>
    class unique_lock
    {
    private:
        Mutex* m;
        bool is_locked;
        explicit unique_lock(unique_lock&);
        unique_lock& operator=(unique_lock&);
    public:
        explicit unique_lock(Mutex& m_):
            m(&m_),is_locked(false)
        {
            lock();
        }
        unique_lock(Mutex& m_,adopt_lock_t):
            m(&m_),is_locked(true)
        {}
        unique_lock(Mutex& m_,defer_lock_t):
            m(&m_),is_locked(false)
        {}
        unique_lock(Mutex& m_,try_to_lock_t):
            m(&m_),is_locked(false)
        {
            try_lock();
        }
        unique_lock(Mutex& m_,system_time const& target_time):
            m(&m_),is_locked(false)
        {
            timed_lock(target_time);
        }
        unique_lock(detail::thread_move_t<unique_lock<Mutex> > other):
            m(other->m),is_locked(other->is_locked)
        {
            other->is_locked=false;
            other->m=0;
        }
        unique_lock(detail::thread_move_t<upgrade_lock<Mutex> > other);

        operator detail::thread_move_t<unique_lock<Mutex> >()
        {
            return move();
        }

        detail::thread_move_t<unique_lock<Mutex> > move()
        {
            return detail::thread_move_t<unique_lock<Mutex> >(*this);
        }

        unique_lock& operator=(detail::thread_move_t<unique_lock<Mutex> > other)
        {
            unique_lock temp(other);
            swap(temp);
            return *this;
        }

        unique_lock& operator=(detail::thread_move_t<upgrade_lock<Mutex> > other)
        {
            unique_lock temp(other);
            swap(temp);
            return *this;
        }

        void swap(unique_lock& other)
        {
            std::swap(m,other.m);
            std::swap(is_locked,other.is_locked);
        }
        void swap(detail::thread_move_t<unique_lock<Mutex> > other)
        {
            std::swap(m,other->m);
            std::swap(is_locked,other->is_locked);
        }
        
        ~unique_lock()
        {
            if(owns_lock())
            {
                m->unlock();
            }
        }
        void lock()
        {
            if(owns_lock())
            {
                throw boost::lock_error();
            }
            m->lock();
            is_locked=true;
        }
        bool try_lock()
        {
            if(owns_lock())
            {
                throw boost::lock_error();
            }
            is_locked=m->try_lock();
            return is_locked;
        }
        template<typename TimeDuration>
        bool timed_lock(TimeDuration const& relative_time)
        {
            is_locked=m->timed_lock(relative_time);
            return is_locked;
        }
        
        bool timed_lock(::boost::system_time const& absolute_time)
        {
            is_locked=m->timed_lock(absolute_time);
            return is_locked;
        }
        void unlock()
        {
            if(!owns_lock())
            {
                throw boost::lock_error();
            }
            m->unlock();
            is_locked=false;
        }
            
        typedef void (unique_lock::*bool_type)();
        operator bool_type() const
        {
            return is_locked?&unique_lock::lock:0;
        }
        bool operator!() const
        {
            return !owns_lock();
        }
        bool owns_lock() const
        {
            return is_locked;
        }

        Mutex* mutex() const
        {
            return m;
        }

        Mutex* release()
        {
            Mutex* const res=m;
            m=0;
            is_locked=false;
            return res;
        }

        friend class shared_lock<Mutex>;
        friend class upgrade_lock<Mutex>;
    };

    template<typename Mutex>
    inline detail::thread_move_t<unique_lock<Mutex> > move(unique_lock<Mutex> & x)
    {
        return x.move();
    }
    
    template<typename Mutex>
    inline detail::thread_move_t<unique_lock<Mutex> > move(detail::thread_move_t<unique_lock<Mutex> > x)
    {
        return x;
    }

    template<typename Mutex>
    class shared_lock
    {
    protected:
        Mutex* m;
        bool is_locked;
    private:
        explicit shared_lock(shared_lock&);
        shared_lock& operator=(shared_lock&);
    public:
        explicit shared_lock(Mutex& m_):
            m(&m_),is_locked(false)
        {
            lock();
        }
        shared_lock(Mutex& m_,adopt_lock_t):
            m(&m_),is_locked(true)
        {}
        shared_lock(Mutex& m_,defer_lock_t):
            m(&m_),is_locked(false)
        {}
        shared_lock(Mutex& m_,try_to_lock_t):
            m(&m_),is_locked(false)
        {
            try_lock();
        }
        shared_lock(Mutex& m_,system_time const& target_time):
            m(&m_),is_locked(false)
        {
            timed_lock(target_time);
        }

        shared_lock(detail::thread_move_t<shared_lock<Mutex> > other):
            m(other->m),is_locked(other->is_locked)
        {
            other->is_locked=false;
        }

        shared_lock(detail::thread_move_t<unique_lock<Mutex> > other):
            m(other->m),is_locked(other->is_locked)
        {
            other->is_locked=false;
            if(is_locked)
            {
                m->unlock_and_lock_shared();
            }
        }

        shared_lock(detail::thread_move_t<upgrade_lock<Mutex> > other):
            m(other->m),is_locked(other->is_locked)
        {
            other->is_locked=false;
            if(is_locked)
            {
                m->unlock_upgrade_and_lock_shared();
            }
        }

        operator detail::thread_move_t<shared_lock<Mutex> >()
        {
            return move();
        }

        detail::thread_move_t<shared_lock<Mutex> > move()
        {
            return detail::thread_move_t<shared_lock<Mutex> >(*this);
        }


        shared_lock& operator=(detail::thread_move_t<shared_lock<Mutex> > other)
        {
            shared_lock temp(other);
            swap(temp);
            return *this;
        }

        shared_lock& operator=(detail::thread_move_t<unique_lock<Mutex> > other)
        {
            shared_lock temp(other);
            swap(temp);
            return *this;
        }

        shared_lock& operator=(detail::thread_move_t<upgrade_lock<Mutex> > other)
        {
            shared_lock temp(other);
            swap(temp);
            return *this;
        }

        void swap(shared_lock& other)
        {
            std::swap(m,other.m);
            std::swap(is_locked,other.is_locked);
        }
        
        ~shared_lock()
        {
            if(owns_lock())
            {
                m->unlock_shared();
            }
        }
        void lock()
        {
            if(owns_lock())
            {
                throw boost::lock_error();
            }
            m->lock_shared();
            is_locked=true;
        }
        bool try_lock()
        {
            if(owns_lock())
            {
                throw boost::lock_error();
            }
            is_locked=m->try_lock_shared();
            return is_locked;
        }
        bool timed_lock(boost::system_time const& target_time)
        {
            if(owns_lock())
            {
                throw boost::lock_error();
            }
            is_locked=m->timed_lock_shared(target_time);
            return is_locked;
        }
        void unlock()
        {
            if(!owns_lock())
            {
                throw boost::lock_error();
            }
            m->unlock_shared();
            is_locked=false;
        }
            
        typedef void (shared_lock::*bool_type)();
        operator bool_type() const
        {
            return is_locked?&shared_lock::lock:0;
        }
        bool operator!() const
        {
            return !owns_lock();
        }
        bool owns_lock() const
        {
            return is_locked;
        }

    };

    template<typename Mutex>
    inline detail::thread_move_t<shared_lock<Mutex> > move(shared_lock<Mutex> & x)
    {
        return x.move();
    }
    
    template<typename Mutex>
    inline detail::thread_move_t<shared_lock<Mutex> > move(detail::thread_move_t<shared_lock<Mutex> > x)
    {
        return x;
    }


    template<typename Mutex>
    class upgrade_lock
    {
    protected:
        Mutex* m;
        bool is_locked;
    private:
        explicit upgrade_lock(upgrade_lock&);
        upgrade_lock& operator=(upgrade_lock&);
    public:
        explicit upgrade_lock(Mutex& m_):
            m(&m_),is_locked(false)
        {
            lock();
        }
        upgrade_lock(Mutex& m_,bool do_lock):
            m(&m_),is_locked(false)
        {
            if(do_lock)
            {
                lock();
            }
        }
        upgrade_lock(detail::thread_move_t<upgrade_lock<Mutex> > other):
            m(other->m),is_locked(other->is_locked)
        {
            other->is_locked=false;
        }

        upgrade_lock(detail::thread_move_t<unique_lock<Mutex> > other):
            m(other->m),is_locked(other->is_locked)
        {
            other->is_locked=false;
            if(is_locked)
            {
                m->unlock_and_lock_upgrade();
            }
        }

        operator detail::thread_move_t<upgrade_lock<Mutex> >()
        {
            return move();
        }

        detail::thread_move_t<upgrade_lock<Mutex> > move()
        {
            return detail::thread_move_t<upgrade_lock<Mutex> >(*this);
        }


        upgrade_lock& operator=(detail::thread_move_t<upgrade_lock<Mutex> > other)
        {
            upgrade_lock temp(other);
            swap(temp);
            return *this;
        }

        upgrade_lock& operator=(detail::thread_move_t<unique_lock<Mutex> > other)
        {
            upgrade_lock temp(other);
            swap(temp);
            return *this;
        }

        void swap(upgrade_lock& other)
        {
            std::swap(m,other.m);
            std::swap(is_locked,other.is_locked);
        }
        
        ~upgrade_lock()
        {
            if(owns_lock())
            {
                m->unlock_upgrade();
            }
        }
        void lock()
        {
            if(owns_lock())
            {
                throw boost::lock_error();
            }
            m->lock_upgrade();
            is_locked=true;
        }
        bool try_lock()
        {
            if(owns_lock())
            {
                throw boost::lock_error();
            }
            is_locked=m->try_lock_upgrade();
            return is_locked;
        }
        void unlock()
        {
            if(!owns_lock())
            {
                throw boost::lock_error();
            }
            m->unlock_upgrade();
            is_locked=false;
        }
            
        typedef void (upgrade_lock::*bool_type)();
        operator bool_type() const
        {
            return is_locked?&upgrade_lock::lock:0;
        }
        bool operator!() const
        {
            return !owns_lock();
        }
        bool owns_lock() const
        {
            return is_locked;
        }
        friend class shared_lock<Mutex>;
        friend class unique_lock<Mutex>;
    };


    template<typename Mutex>
    inline detail::thread_move_t<upgrade_lock<Mutex> > move(upgrade_lock<Mutex> & x)
    {
        return x.move();
    }
    
    template<typename Mutex>
    inline detail::thread_move_t<upgrade_lock<Mutex> > move(detail::thread_move_t<upgrade_lock<Mutex> > x)
    {
        return x;
    }

    template<typename Mutex>
    unique_lock<Mutex>::unique_lock(detail::thread_move_t<upgrade_lock<Mutex> > other):
        m(other->m),is_locked(other->is_locked)
    {
        other->is_locked=false;
        if(is_locked)
        {
            m->unlock_upgrade_and_lock();
        }
    }

    template <class Mutex>
    class upgrade_to_unique_lock
    {
    private:
        upgrade_lock<Mutex>* source;
        unique_lock<Mutex> exclusive;

        explicit upgrade_to_unique_lock(upgrade_to_unique_lock&);
        upgrade_to_unique_lock& operator=(upgrade_to_unique_lock&);
    public:
        explicit upgrade_to_unique_lock(upgrade_lock<Mutex>& m_):
            source(&m_),exclusive(move(*source))
        {}
        ~upgrade_to_unique_lock()
        {
            if(source)
            {
                *source=move(exclusive);
            }
        }

        upgrade_to_unique_lock(detail::thread_move_t<upgrade_to_unique_lock<Mutex> > other):
            source(other->source),exclusive(move(other->exclusive))
        {
            other->source=0;
        }
        
        upgrade_to_unique_lock& operator=(detail::thread_move_t<upgrade_to_unique_lock<Mutex> > other)
        {
            upgrade_to_unique_lock temp(other);
            swap(temp);
            return *this;
        }
        void swap(upgrade_to_unique_lock& other)
        {
            std::swap(source,other.source);
            exclusive.swap(other.exclusive);
        }
        typedef void (upgrade_to_unique_lock::*bool_type)(upgrade_to_unique_lock&);
        operator bool_type() const
        {
            return exclusive.owns_lock()?&upgrade_to_unique_lock::swap:0;
        }
        bool operator!() const
        {
            return !owns_lock();
        }
        bool owns_lock() const
        {
            return exclusive.owns_lock();
        }
    };

}

#endif
