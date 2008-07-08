// Copyright (C) 2001-2003
// William E. Kempf
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>

#include <boost/thread/exceptions.hpp>
#include <cstring>
#include <string>
#include <sstream>

namespace boost {

thread_exception::thread_exception()
    : m_sys_err(0)
{
}

thread_exception::thread_exception(const std::string &description, int sys_err_code)
    : m_sys_err(sys_err_code)
{
    std::ostringstream s;
    s << description << ": ";
    s << strerror(sys_err_code) << " (" << sys_err_code << ")";
    message.assign(s.str());
}

thread_exception::thread_exception(int sys_err_code)
    : m_sys_err(sys_err_code)
{
    std::ostringstream s;
    s << strerror(sys_err_code) << " (" << sys_err_code << ")";
    message.assign(s.str());
}

thread_exception::~thread_exception() throw()
{
}

int thread_exception::native_error() const
{
    return m_sys_err; 
}

const char *thread_exception::what() const throw()
{
    if (message.empty()) {
        return std::exception::what();
    } else {
        return message.c_str();
    }
}

lock_error::lock_error()
{
}

lock_error::lock_error(int sys_err_code)
    : thread_exception(sys_err_code)
{
}

lock_error::~lock_error() throw()
{
}

const char* lock_error::what() const throw()
{
    return "boost::lock_error";
}

thread_resource_error::thread_resource_error()
{
}

thread_resource_error::thread_resource_error(const std::string &description, int sys_err_code)
    : thread_exception(description, sys_err_code)
{
}

thread_resource_error::thread_resource_error(int sys_err_code)
    : thread_exception(sys_err_code)
{
}

thread_resource_error::~thread_resource_error() throw()
{
}

unsupported_thread_option::unsupported_thread_option()
{
}

unsupported_thread_option::unsupported_thread_option(int sys_err_code)
    : thread_exception(sys_err_code)
{
}

unsupported_thread_option::~unsupported_thread_option() throw()
{
}

const char* unsupported_thread_option::what() const throw()
{
    return "boost::unsupported_thread_option";
}

invalid_thread_argument::invalid_thread_argument()
{
}

invalid_thread_argument::invalid_thread_argument(int sys_err_code)
    : thread_exception(sys_err_code)
{
}

invalid_thread_argument::~invalid_thread_argument() throw()
{
}

const char* invalid_thread_argument::what() const throw()
{
    return "boost::invalid_thread_argument";
}

thread_permission_error::thread_permission_error()
{
}

thread_permission_error::thread_permission_error(int sys_err_code)
    : thread_exception(sys_err_code)
{
}

thread_permission_error::~thread_permission_error() throw()
{
}

const char* thread_permission_error::what() const throw()
{
    return "boost::thread_permission_error";
}

} // namespace boost
