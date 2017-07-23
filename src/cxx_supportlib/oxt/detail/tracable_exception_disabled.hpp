/*
 * OXT - OS eXtensions for boosT
 * Provides important functionality necessary for writing robust server software.
 *
 * Copyright (c) 2010-2017 Phusion Holding B.V.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <exception>
#include <string>

// Dummy implementation for tracable_exception.hpp.
// See detail/tracable_exception_enabled.hpp for API documentation.

namespace oxt {

class tracable_exception: public std::exception {
public:
	struct no_backtrace { };

	tracable_exception()
		: std::exception()
	{
		// Do nothing.
	}

	tracable_exception(const tracable_exception &other)
		: std::exception(other)
	{
		// Do nothing.
	}

	tracable_exception(const no_backtrace &tag)
		: std::exception()
	{
		// Do nothing.
	}

	virtual std::string backtrace() const throw() {
		return "     (backtrace support disabled during compile time)\n";
	}

	virtual const char *what() const throw() {
		return "oxt::tracable_exception";
	}
};

} // namespace oxt

