// Copyright (C) 2002-2003
// David Moore, William E. Kempf
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/thread/detail/config.hpp>
#include <boost/thread/barrier.hpp>
#include <string> // see http://article.gmane.org/gmane.comp.lib.boost.devel/106981

namespace boost {

barrier::barrier(unsigned int count)
    : m_threshold(count), m_count(count), m_generation(0)
{
    if (count == 0)
        throw std::invalid_argument("count cannot be zero.");
}

barrier::~barrier()
{
}

bool barrier::wait()
{
    boost::mutex::scoped_lock lock(m_mutex);
    unsigned int gen = m_generation;

    if (--m_count == 0)
    {
        m_generation++;
        m_count = m_threshold;
        m_cond.notify_all();
        return true;
    }

    while (gen == m_generation)
        m_cond.wait(lock);
    return false;
}

} // namespace boost
