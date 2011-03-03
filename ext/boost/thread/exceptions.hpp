// Copyright (C) 2001-2003
// William E. Kempf
// Copyright (C) 2007-9 Anthony Williams
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_THREAD_EXCEPTIONS_PDM070801_H
#define BOOST_THREAD_EXCEPTIONS_PDM070801_H

#include <boost/thread/detail/config.hpp>
#include <oxt/tracable_exception.hpp>

//  pdm: Sorry, but this class is used all over the place & I end up
//       with recursive headers if I don't separate it
//  wek: Not sure why recursive headers would cause compilation problems
//       given the include guards, but regardless it makes sense to
//       seperate this out any way.

#include <string>
#include <stdexcept>
#include <sstream>
#include <cstring>

#include <boost/config/abi_prefix.hpp>

namespace boost
{

    class thread_interrupted:
        public oxt::tracable_exception
    {};

    class thread_exception:
        public oxt::tracable_exception
    {
    protected:
        std::string message;
        
        thread_exception():
            m_sys_err(0)
        {}
    
        thread_exception(const std::string &description, int sys_err_code):
            m_sys_err(sys_err_code)
        {
            std::ostringstream s;
            s << description << ": ";
            s << strerror(sys_err_code) << " (" << sys_err_code << ")";
            message.assign(s.str());
        }
    
        thread_exception(int sys_err_code):
            m_sys_err(sys_err_code)
        {
            std::ostringstream s;
            s << strerror(sys_err_code) << " (" << sys_err_code << ")";
            message.assign(s.str());
        }
    

    public:
        ~thread_exception() throw()
        {}
    

        int native_error() const
        {
            return m_sys_err;
        }
        
        virtual const char *what() const throw()
        {
            if(message.empty())
            {
                return std::exception::what();
            }
            else
            {
                return message.c_str();
            }
        }
    
    private:
        int m_sys_err;
    };

    class condition_error:
        public std::exception
    {
    public:
        const char* what() const throw()
        {
            return "Condition error";
        }
    };
    

    class lock_error:
        public thread_exception
    {
    public:
        lock_error()
        {}
    
        lock_error(int sys_err_code):
            thread_exception(sys_err_code)
        {}
    
        ~lock_error() throw()
        {}
    

        virtual const char* what() const throw()
        {
            if(message.empty())
            {
                return "boost::lock_error";
            }
            else
            {
                return message.c_str();
            }
        }
    };

    class thread_resource_error:
        public thread_exception
    {
    public:
        thread_resource_error()
        {}
    
        thread_resource_error(int sys_err_code):
            thread_exception(sys_err_code)
        {}
    
        thread_resource_error(const std::string &description, int sys_err_code):
            thread_exception(description, sys_err_code)
        {}
    
        ~thread_resource_error() throw()
        {}
    

        virtual const char* what() const throw()
        {
            if(message.empty())
            {
                return "boost::thread_resource_error";
            }
            else
            {
                return message.c_str();
            }
        }
    
    };

    class unsupported_thread_option:
        public thread_exception
    {
    public:
        unsupported_thread_option()
        {}
    
        unsupported_thread_option(int sys_err_code):
            thread_exception(sys_err_code)
        {}
    
        ~unsupported_thread_option() throw()
        {}
    

        virtual const char* what() const throw()
        {
            return "boost::unsupported_thread_option";
        }
    
    };

    class invalid_thread_argument:
        public thread_exception
    {
    public:
        invalid_thread_argument()
        {}
    
        invalid_thread_argument(int sys_err_code):
            thread_exception(sys_err_code)
        {}
    
        ~invalid_thread_argument() throw()
        {}
    

        virtual const char* what() const throw()
        {
            return "boost::invalid_thread_argument";
        }
    
    };

    class thread_permission_error:
        public thread_exception
    {
    public:
        thread_permission_error()
        {}
    
        thread_permission_error(int sys_err_code):
            thread_exception(sys_err_code)
        {}
    
        ~thread_permission_error() throw()
        {}
    

        virtual const char* what() const throw()
        {
            return "boost::thread_permission_error";
        }
    
    };

} // namespace boost

#include <boost/config/abi_suffix.hpp>

#endif
