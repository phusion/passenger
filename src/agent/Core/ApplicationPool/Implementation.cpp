/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#include <typeinfo>
#include <algorithm>
#include <utility>
#include <cstdio>
#include <sstream>
#include <limits.h>
#include <unistd.h>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>
#include <boost/cstdint.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <oxt/backtrace.hpp>
#include <Exceptions.h>
#include <Hooks.h>
#include <IOTools/MessageSerialization.h>
#include <Utils.h>
#include <IOTools/IOUtils.h>
#include <Utils/ScopeGuard.h>
#include <IOTools/MessageIO.h>
#include <JsonTools/JsonUtils.h>
#include <Core/ApplicationPool/Pool.h>
#include <Core/ApplicationPool/Group.h>
#include <Core/ApplicationPool/Pool/InitializationAndShutdown.cpp>
#include <Core/ApplicationPool/Pool/AnalyticsCollection.cpp>
#include <Core/ApplicationPool/Pool/GarbageCollection.cpp>
#include <Core/ApplicationPool/Pool/GeneralUtils.cpp>
#include <Core/ApplicationPool/Pool/GroupUtils.cpp>
#include <Core/ApplicationPool/Pool/ProcessUtils.cpp>
#include <Core/ApplicationPool/Pool/StateInspection.cpp>
#include <Core/ApplicationPool/Pool/Miscellaneous.cpp>
#include <Core/ApplicationPool/Group/InitializationAndShutdown.cpp>
#include <Core/ApplicationPool/Group/LifetimeAndBasics.cpp>
#include <Core/ApplicationPool/Group/SessionManagement.cpp>
#include <Core/ApplicationPool/Group/SpawningAndRestarting.cpp>
#include <Core/ApplicationPool/Group/ProcessListManagement.cpp>
#include <Core/ApplicationPool/Group/OutOfBandWork.cpp>
#include <Core/ApplicationPool/Group/Miscellaneous.cpp>
#include <Core/ApplicationPool/Group/InternalUtils.cpp>
#include <Core/ApplicationPool/Group/StateInspection.cpp>
#include <Core/ApplicationPool/Group/Verification.cpp>
#include <Core/ApplicationPool/Process.cpp>
#include <Core/SpawningKit/ErrorRenderer.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;

#define TRY_COPY_EXCEPTION(klass) \
	do { \
		const klass *ep = dynamic_cast<const klass *>(&e); \
		if (ep != NULL) { \
			return boost::make_shared<klass>(*ep); \
		} \
	} while (false)

ExceptionPtr
copyException(const tracable_exception &e) {
	TRY_COPY_EXCEPTION(FileSystemException);
	TRY_COPY_EXCEPTION(TimeRetrievalException);
	TRY_COPY_EXCEPTION(SystemException);

	TRY_COPY_EXCEPTION(FileNotFoundException);
	TRY_COPY_EXCEPTION(EOFException);
	TRY_COPY_EXCEPTION(IOException);

	TRY_COPY_EXCEPTION(ConfigurationException);

	TRY_COPY_EXCEPTION(RequestQueueFullException);
	TRY_COPY_EXCEPTION(GetAbortedException);
	TRY_COPY_EXCEPTION(SpawningKit::SpawnException);

	TRY_COPY_EXCEPTION(InvalidModeStringException);
	TRY_COPY_EXCEPTION(ArgumentException);

	TRY_COPY_EXCEPTION(RuntimeException);

	TRY_COPY_EXCEPTION(TimeoutException);

	TRY_COPY_EXCEPTION(NonExistentUserException);
	TRY_COPY_EXCEPTION(NonExistentGroupException);
	TRY_COPY_EXCEPTION(SecurityException);

	TRY_COPY_EXCEPTION(SyntaxError);

	TRY_COPY_EXCEPTION(boost::thread_interrupted);

	return boost::make_shared<tracable_exception>(e);
}

#define TRY_RETHROW_EXCEPTION(klass) \
	do { \
		const klass *ep = dynamic_cast<const klass *>(&*e); \
		if (ep != NULL) { \
			throw klass(*ep); \
		} \
	} while (false)

void
rethrowException(const ExceptionPtr &e) {
	TRY_RETHROW_EXCEPTION(FileSystemException);
	TRY_RETHROW_EXCEPTION(TimeRetrievalException);
	TRY_RETHROW_EXCEPTION(SystemException);

	TRY_RETHROW_EXCEPTION(FileNotFoundException);
	TRY_RETHROW_EXCEPTION(EOFException);
	TRY_RETHROW_EXCEPTION(IOException);

	TRY_RETHROW_EXCEPTION(ConfigurationException);

	TRY_RETHROW_EXCEPTION(SpawningKit::SpawnException);
	TRY_RETHROW_EXCEPTION(RequestQueueFullException);
	TRY_RETHROW_EXCEPTION(GetAbortedException);

	TRY_RETHROW_EXCEPTION(InvalidModeStringException);
	TRY_RETHROW_EXCEPTION(ArgumentException);

	TRY_RETHROW_EXCEPTION(RuntimeException);

	TRY_RETHROW_EXCEPTION(TimeoutException);

	TRY_RETHROW_EXCEPTION(NonExistentUserException);
	TRY_RETHROW_EXCEPTION(NonExistentGroupException);
	TRY_RETHROW_EXCEPTION(SecurityException);

	TRY_RETHROW_EXCEPTION(SyntaxError);

	TRY_RETHROW_EXCEPTION(boost::lock_error);
	TRY_RETHROW_EXCEPTION(boost::thread_resource_error);
	TRY_RETHROW_EXCEPTION(boost::unsupported_thread_option);
	TRY_RETHROW_EXCEPTION(boost::invalid_thread_argument);
	TRY_RETHROW_EXCEPTION(boost::thread_permission_error);

	TRY_RETHROW_EXCEPTION(boost::thread_interrupted);
	TRY_RETHROW_EXCEPTION(boost::thread_exception);
	TRY_RETHROW_EXCEPTION(boost::condition_error);

	throw tracable_exception(*e);
}

void processAndLogNewSpawnException(SpawningKit::SpawnException &e, const Options &options,
	const Context *context)
{
	TRACE_POINT();
	SpawningKit::ErrorRenderer renderer(*context->getSpawningKitContext());
	string errorId;
	char filename[PATH_MAX];
	stringstream stream;
	string errorPage;

	UPDATE_TRACE_POINT();
	if (errorId.empty()) {
		errorId = context->getRandomGenerator()->generateHexString(4);
	}
	e.setId(errorId);

	try {
		int fd = -1;

		UPDATE_TRACE_POINT();
		errorPage = renderer.renderWithDetails(e);

		#if (defined(__linux__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 11))) || defined(__APPLE__) || defined(__FreeBSD__)
			snprintf(filename, PATH_MAX, "%s/passenger-error-XXXXXX.html",
				getSystemTempDir());
			fd = mkstemps(filename, sizeof(".html") - 1);
		#else
			snprintf(filename, PATH_MAX, "%s/passenger-error.XXXXXX",
				getSystemTempDir());
			fd = mkstemp(filename);
		#endif
		FdGuard guard(fd, NULL, 0, true);
		if (fd == -1) {
			int e = errno;
			throw SystemException("Cannot generate a temporary filename",
				e);
		}

		UPDATE_TRACE_POINT();
		writeExact(fd, errorPage);
	} catch (const SystemException &e2) {
		filename[0] = '\0';
		P_ERROR("Cannot render an error page: " << e2.what() << "\n" <<
			e2.backtrace());
	}

	UPDATE_TRACE_POINT();
	stream << "Could not spawn process for application " << options.appRoot <<
		": " << e.what() << "\n" <<
		"  Error ID: " << errorId << "\n";
	if (filename[0] != '\0') {
		stream << "  Error details saved to: " << filename << "\n";
	}
	P_ERROR(stream.str());

	ScopedLock l(context->agentConfigSyncher);
	if (!context->agentConfig.isNull()) {
		HookScriptOptions hOptions;
		hOptions.name = "spawn_failed";
		hOptions.spec = context->agentConfig.get("hook_spawn_failed", Json::Value()).asString();
		hOptions.agentConfig = context->agentConfig;
		l.unlock();
		hOptions.environment.push_back(make_pair("PASSENGER_APP_ROOT", options.appRoot));
		hOptions.environment.push_back(make_pair("PASSENGER_APP_GROUP_NAME", options.getAppGroupName()));
		hOptions.environment.push_back(make_pair("PASSENGER_ERROR_MESSAGE", e.what()));
		hOptions.environment.push_back(make_pair("PASSENGER_ERROR_ID", errorId));
		hOptions.environment.push_back(make_pair("PASSENGER_ERROR_PAGE", errorPage));
		oxt::thread(boost::bind(runHookScripts, hOptions),
			"Hook: spawn_failed", 256 * 1024);
	}
}

void
recreateString(psg_pool_t *pool, StaticString &str) {
	str = psg_pstrdup(pool, str);
}


void
Session::requestOOBW() {
	ProcessPtr process = getProcess()->shared_from_this();
	assert(process->isAlive());
	process->getGroup()->requestOOBW(process);
}


} // namespace ApplicationPool2
} // namespace Passenger
