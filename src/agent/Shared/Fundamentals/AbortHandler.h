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
#ifndef _PASSENGER_AGENT_FUNDAMENTALS_ABORT_HANDLER_H_
#define _PASSENGER_AGENT_FUNDAMENTALS_ABORT_HANDLER_H_

#include <cstddef>

namespace Passenger {
	class ResourceLocator;
}

namespace Passenger {
namespace Agent {
namespace Fundamentals {


struct AbortHandlerConfig {
	static const unsigned int MAX_DIAGNOSTICS_DUMPERS = 5;
	typedef void (*DiagnosticsDumperFunc)(void *userData);

	struct DiagnosticsDumper {
		const char *name;
		const char *logFileName;
		DiagnosticsDumperFunc func;
		void *userData;

		DiagnosticsDumper()
			: name(0),
			  logFileName(0),
			  func(0),
			  userData(0)
			{ }
	};


	char *ruby;
	char **origArgv;
	unsigned int randomSeed;
	bool dumpWithCrashWatch;
	bool beep;
	bool stopProcess;
	ResourceLocator *resourceLocator;
	DiagnosticsDumper diagnosticsDumpers[MAX_DIAGNOSTICS_DUMPERS];

	AbortHandlerConfig()
		: ruby(NULL),
		  origArgv(NULL),
		  randomSeed(0),
		  dumpWithCrashWatch(false),
		  beep(false),
		  stopProcess(false),
		  resourceLocator(NULL)
		{ }
};

void installAbortHandler(const AbortHandlerConfig *config);
bool abortHandlerInstalled();
void abortHandlerLogFds();
void abortHandlerConfigChanged();
void shutdownAbortHandler();


} // namespace Fundamentals
} // namespace Agent
} // namespace Passenger

#endif /* _PASSENGER_AGENT_FUNDAMENTALS_ABORT_HANDLER_H_ */
