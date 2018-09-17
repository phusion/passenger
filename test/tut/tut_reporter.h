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
#include <unistd.h>

/**
 * Template Unit Tests Framework for C++.
 * http://tut.dozen.ru
 *
 * @author Vladimir Dyuzhev, Vladimir.Dyuzhev@gmail.com
 */

namespace tut
{

inline const char* green(bool is_tty)
{
    if (is_tty) {
        return "\e[0;32m";
    } else {
        return "";
    }
}

inline const char* red(bool is_tty)
{
    if (is_tty) {
        return "\e[0;31m";
    } else {
        return "";
    }
}

inline const char* reset(bool is_tty)
{
    if (is_tty) {
        return "\e[0m";
    } else {
        return "";
    }
}

inline std::ostream& printTestResult(std::ostream& os, const tut::test_result& tr, bool is_tty)
{
    switch(tr.result)
    {
    case tut::test_result::ok:
        os << green(is_tty) << " ✔" << reset(is_tty);
        break;
    case tut::test_result::fail:
        os << red(is_tty) << " ✗" << reset(is_tty);
        break;
    case tut::test_result::ex_ctor:
        os << red(is_tty) << " ✗ (constructor failed)" << reset(is_tty);
        break;
    case tut::test_result::ex:
        os << red(is_tty) << " ✗ (exception)" << reset(is_tty);
        break;
    case tut::test_result::warn:
        os << red(is_tty) << " 😮" << reset(is_tty);
        break;
    case tut::test_result::term:
        os << red(is_tty) << " ✗ (abnormal)" << reset(is_tty);
        break;
    }

    return os;
}

/**
 * Default TUT callback handler.
 */
class reporter : public tut::callback
{
    typedef std::vector<tut::test_result> not_passed_list;
    not_passed_list not_passed;
    std::ostream& os;
    bool is_tty;

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

    virtual void group_started(const std::string& name)
    {
        os << std::endl << name << ":" << std::endl;
    }

    virtual void test_started(int n)
    {
        os << "  " << n << "..." << std::flush;
    }

    void test_completed(const tut::test_result& tr)
    {
        printTestResult(os, tr, is_tty) << std::endl;
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

    virtual void test_nonexistant(int n)
    {
        if (is_tty) {
            os << "\r          \r" << std::flush;
        } else {
            os << " skipped" << std::endl;
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
        is_tty = isatty(1);
    }
};

}

#endif
