/**
 * TUT unit testing framework.
 * http://tut-framework.sourceforge.net/
 *
 * Copyright 2002-2006 Vladimir Dyuzhev.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Slightly modified for use in this software package.

#ifndef TUT_REPORTER
#define TUT_REPORTER

#include "tut.h"

/**
 * Template Unit Tests Framework for C++.
 * http://tut.dozen.ru
 *
 * @author Vladimir Dyuzhev, Vladimir.Dyuzhev@gmail.com
 */
namespace
{
    
std::ostream& operator<<(std::ostream& os, const tut::test_result& tr)
{
    switch(tr.result)
    {
    case tut::test_result::ok:
        os << '.';
        break;
    case tut::test_result::fail:
        os << '[' << tr.test << "=F]";
        break;
    case tut::test_result::ex_ctor:
        os << '[' << tr.test << "=C]";
        break;
    case tut::test_result::ex:
        os << '[' << tr.test << "=X]";
        break;
    case tut::test_result::warn:
        os << '[' << tr.test << "=W]";
        break;
    case tut::test_result::term:
        os << '[' << tr.test << "=T]";
        break;
    }

    return os;
}

} // end of namespace

namespace tut
{

/**
 * Default TUT callback handler.
 */
class reporter : public tut::callback
{
    std::string current_group;
    typedef std::vector<tut::test_result> not_passed_list;
    not_passed_list not_passed;
    std::ostream& os;

public:

    int ok_count;
    int exceptions_count;
    int failures_count;
    int terminations_count;
    int warnings_count;

    reporter() 
        : os(std::cout)
    {
        init();
    }

    reporter(std::ostream& out) 
        : os(out)
    {
        init();
    }

    void run_started()
    {
        init();
    }

    void test_completed(const tut::test_result& tr)
    {
        if (tr.group != current_group)
        {
            os << std::endl << tr.group << ": " << std::flush;
            current_group = tr.group;
        }

        os << tr << std::flush;
        if (tr.result == tut::test_result::ok)
        {
            ok_count++;
        }
        else if (tr.result == tut::test_result::ex)
        {
            exceptions_count++;
        }
        else if (tr.result == tut::test_result::ex_ctor)
        {
            exceptions_count++;
        }
        else if (tr.result == tut::test_result::fail)
        {
            failures_count++;
        }
        else if (tr.result == tut::test_result::warn)
        {
            warnings_count++;
        }
        else
        {
            terminations_count++;
        }

        if (tr.result != tut::test_result::ok)
        {
            not_passed.push_back(tr);
        }
    }

    void run_completed()
    {
        os << std::endl;

        if (not_passed.size() > 0)
        {
            not_passed_list::const_iterator i = not_passed.begin();
            while (i != not_passed.end())
            {
                tut::test_result tr = *i;

                os << std::endl;

                os << "---> " << "group: " << tr.group
                << ", test: test<" << tr.test << ">"
                << (!tr.name.empty() ? (std::string(" : ") + tr.name) : std::string())
                << std::endl;

                os << "     problem: ";
                switch(tr.result)
                {
                case test_result::fail:
                    os << "assertion failed" << std::endl;
                    break;
                case test_result::ex:
                case test_result::ex_ctor:
                    os << "unexpected exception" << std::endl;
                    if( tr.exception_typeid != "" )
                    {
                        os << "     exception typeid: "
                        << tr.exception_typeid << std::endl;
                    }
                    break;
                case test_result::term:
                    os << "would be terminated" << std::endl;
                    break;
                case test_result::warn:
                    os << "test passed, but cleanup code (destructor) raised"
                        " an exception" << std::endl;
                    break;
                default:
                    break;
                }

                if (!tr.message.empty())
                {
                    if (tr.result == test_result::fail)
                    {
                        os << "     failed assertion: \"" << tr.message << "\"" 
                            << std::endl;
                    }
                    else
                    {
                        os << "     message: \"" << tr.message << "\"" 
                            << std::endl;
                    }
                }

                ++i;
            }
        }

        os << std::endl;

        os << "tests summary:";
        if (terminations_count > 0)
        {
            os << " terminations:" << terminations_count;
        }
        if (exceptions_count > 0)
        {
            os << " exceptions:" << exceptions_count;
        }
        if (failures_count > 0)
        {
            os << " failures:" << failures_count;
        }
        if (warnings_count > 0)
        {
            os << " warnings:" << warnings_count;
        }
        os << " ok:" << ok_count;
        os << std::endl;
    }

    bool all_ok() const
    {
        return not_passed.empty();
    }

private:

    void init()
    {
        ok_count = 0;
        exceptions_count = 0;
        failures_count = 0;
        terminations_count = 0;
        warnings_count = 0;
        not_passed.clear();
    }
};

}

#endif
