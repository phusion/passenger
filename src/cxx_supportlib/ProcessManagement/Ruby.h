/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_PROCESS_MANAGEMENT_RUBY_H_
#define _PASSENGER_PROCESS_MANAGEMENT_RUBY_H_

#include <string>
#include <vector>
#include <limits>
#include <cstddef>

namespace Passenger {

using namespace std;

class ResourceLocator;
struct SubprocessOutput;


/**
 * Run a Passenger-internal Ruby tool, e.g. passenger-config, and optionally capture
 * its stdout output. This function does not care whether the command fails.
 *
 * @param resourceLocator
 * @param ruby The Ruby interpreter to attempt to use for running the tool.
 * @param args The command as an array of strings, e.g. ["passenger-config", "system-properties"].
 * @param status The status of the child process will be stored here, if non-NULL.
 *               When unable to waitpid() the child process because of an ECHILD
 *               or ESRCH, this will be set to -1.
 * @param output The output of the child process will be stored here, if non-NULL.
 * @param maxOutputSize The maximum number of output bytes to read. Only applicable if
 *                      `output` is non-NULL.
 * @throws RuntimeException
 * @throws SystemException
 */
void runInternalRubyTool(const ResourceLocator &resourceLocator,
	const string &ruby, const vector<string> &args,
	int *status = NULL, SubprocessOutput *output = NULL,
	size_t maxOutputSize = std::numeric_limits<size_t>::max());


} // namespace Passenger

#endif /* _PASSENGER_PROCESS_MANAGEMENT_RUBY_H_ */
