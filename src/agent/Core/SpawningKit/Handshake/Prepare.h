/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SPAWNING_KIT_HANDSHAKE_PREPARE_H_
#define _PASSENGER_SPAWNING_KIT_HANDSHAKE_PREPARE_H_

#include <oxt/backtrace.hpp>
#include <boost/thread.hpp>
#include <boost/scoped_array.hpp>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <utility>
#include <cerrno>
#include <cstddef>
#include <cassert>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <limits.h>

#include <jsoncpp/json.h>

#include <Constants.h>
#include <LoggingKit/LoggingKit.h>
#include <StaticString.h>
#include <Exceptions.h>
#include <FileTools/FileManip.h>
#include <FileTools/PathManip.h>
#include <SystemTools/UserDatabase.h>
#include <SystemTools/SystemTime.h>
#include <Utils/Timer.h>
#include <IOTools/IOUtils.h>
#include <StrIntTools/StrIntUtils.h>
#include <Core/SpawningKit/Context.h>
#include <Core/SpawningKit/Config.h>
#include <Core/SpawningKit/Journey.h>
#include <Core/SpawningKit/Exceptions.h>
#include <Core/SpawningKit/Handshake/Session.h>
#include <Core/SpawningKit/Handshake/WorkDir.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;
using namespace oxt;


/**
 * For an introduction see README.md, section
 * "The preparation and the HandshakePrepare class".
 */
class HandshakePrepare {
private:
	HandshakeSession &session;
	Context * const context;
	Config * const config;
	Json::Value args;
	Timer<SystemTime::GRAN_10MSEC> timer;


	void resolveUserAndGroup() {
		TRACE_POINT();
		string username = config->user.toString(); // null terminate string
		string groupname = config->group.toString(); // null terminate string
		OsUser osUser;
		OsGroup osGroup;

		if (lookupSystemUserByName(username, osUser)) {
			session.uid = osUser.pwd.pw_uid;
			session.shell = osUser.pwd.pw_shell;
			session.homedir = osUser.pwd.pw_dir;
		} else {
			if (looksLikePositiveNumber(username)) {
				P_WARN("OS user account '" << username << "' does not exist."
					" Will assume that this is a UID.");
				session.uid = (uid_t) atoi(username);
			} else {
				throw RuntimeException("OS user account '" + username + "' does not exist");
			}
		}

		if (lookupSystemGroupByName(groupname, osGroup)) {
			session.gid = osGroup.grp.gr_gid;
		} else {
			if (looksLikePositiveNumber(groupname)) {
				P_WARN("OS group account '" << groupname << "' does not exist."
					" Will assume that this is a GID.");
				session.gid = (gid_t) atoi(groupname);
			} else {
				throw RuntimeException("OS group account '" + groupname + "' does not exist");
			}
		}
	}

	void createWorkDir() {
		TRACE_POINT();
		session.workDir.reset(new HandshakeWorkDir());

		session.envDumpDir = session.workDir->getPath() + "/envdump";
		makeDirTree(session.envDumpDir,
			"u=rwx,g=,o=",
			session.uid,
			session.gid);
		makeDirTree(session.envDumpDir + "/annotations",
			"u=rwx,g=,o=",
			session.uid,
			session.gid);

		session.responseDir = session.workDir->getPath() + "/response";
		makeDirTree(session.responseDir,
			"u=rwx,g=,o=",
			session.uid,
			session.gid);
		createFifo(session.responseDir + "/finish");
		makeDirTree(session.responseDir + "/error",
			"u=rwx,g=,o=",
			session.uid,
			session.gid);
		makeDirTree(session.responseDir + "/steps",
			"u=rwx,g=,o=",
			session.uid,
			session.gid);


		createJourneyStepDirs(getFirstSubprocessJourneyStep(),
			getLastSubprocessJourneyStep());
		createJourneyStepDirs(getFirstPreloaderJourneyStep(),
			// Also create directory for PRELOADER_FINISH;
			// the preloader will want to write there.
			JourneyStep((int) getLastPreloaderJourneyStep() + 1));
	}

	void createJourneyStepDirs(JourneyStep firstStep, JourneyStep lastStep) {
		JourneyStep step;

		for (step = firstStep; step < lastStep; step = JourneyStep((int) step + 1)) {
			if (!session.journey.hasStep(step)) {
				continue;
			}

			string stepString = journeyStepToStringLowerCase(step);
			string stepDir = session.responseDir + "/steps/" + stepString;
			makeDirTree(stepDir, "u=rwx,g=,o=", session.uid, session.gid);
		}
	}

	void createFifo(const string &path) {
		int ret;

		do {
			ret = mkfifo(path.c_str(), 0600);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			int e = errno;
			throw FileSystemException("Cannot create FIFO file " + path,
				e, path);
		}

		ret = syscalls::chown(path.c_str(),
			session.uid,
			session.gid);
		if (ret == -1) {
			int e = errno;
			throw FileSystemException(
				"Cannot change ownership for FIFO file " + path,
				e, path);
		}
	}

	// Open various workdir subdirectories because we'll use these file descriptors later in
	// safeReadFile() calls.
	void openWorkDirSubdirFds() {
		session.workDirFd = openDirFd(session.workDir->getPath());
		session.responseDirFd = openDirFd(session.responseDir);
		session.responseErrorDirFd = openDirFd(session.responseDir + "/error");
		session.envDumpDirFd = openDirFd(session.envDumpDir);
		session.envDumpAnnotationsDirFd = openDirFd(session.envDumpDir + "/annotations");
		openJourneyStepDirFds(getFirstSubprocessJourneyStep(),
			getLastSubprocessJourneyStep());
		openJourneyStepDirFds(getFirstPreloaderJourneyStep(),
			JourneyStep((int) getLastPreloaderJourneyStep() + 1));
	}

	void openJourneyStepDirFds(JourneyStep firstStep, JourneyStep lastStep) {
		JourneyStep step;

		for (step = firstStep; step < lastStep; step = JourneyStep((int) step + 1)) {
			if (!session.journey.hasStep(step)) {
				continue;
			}

			string stepString = journeyStepToStringLowerCase(step);
			string stepDir = session.responseDir + "/steps/" + stepString;
			session.stepDirFds.insert(make_pair(step, openDirFd(stepDir)));
		}
	}

	int openDirFd(const string &path) {
		int fd = open(path.c_str(), O_RDONLY);
		if (fd == -1) {
			int e = errno;
			throw FileSystemException("Cannot open " + path, e, path);
		}
		return fd;
	}

	void initializeResult() {
		session.result.initialize(*context, config);
	}

	void preparePredefinedArgs() {
		TRACE_POINT();
		struct sockaddr_un addr;

		args["passenger_root"] = context->resourceLocator->getInstallSpec();
		args["passenger_version"] = PASSENGER_VERSION;
		args["passenger_agent_path"] = context->resourceLocator->findSupportBinary(AGENT_EXE);
		args["ruby_libdir"] = context->resourceLocator->getRubyLibDir();
		args["node_libdir"] = context->resourceLocator->getNodeLibDir();
		args["integration_mode"] = context->integrationMode;
		args["gupid"] = session.result.gupid;
		args["UNIX_PATH_MAX"] = (Json::UInt64) sizeof(addr.sun_path) - 1;
		if (config->genericApp || config->findFreePort) {
			args["expected_start_port"] = session.expectedStartPort;
		}
		if (!config->apiKey.empty()) {
			args["connect_password"] = config->apiKey.toString();
		}
		if (!context->instanceDir.empty()) {
			args["instance_dir"] = context->instanceDir;
			args["socket_dir"] = context->instanceDir + "/apps.s";
		}
	}

	void prepareArgsFromAppConfig() {
		TRACE_POINT();
		const Json::Value appConfigJson = config->getConfidentialFieldsToPassToApp();
		Json::Value::const_iterator it, end = appConfigJson.end();
		for (it = appConfigJson.begin(); it != end; it++) {
			args[it.name()] = *it;
		}
	}

	void absolutizeKeyArgPaths() {
		TRACE_POINT();
		args["app_root"] = absolutizePath(args["app_root"].asString());
		if (args.isMember("startup_file")) {
			args["startup_file"] = absolutizePath(args["startup_file"].asString(),
				args["app_root"].asString());
		}
	}

	void dumpArgsIntoWorkDir() {
		TRACE_POINT();
		P_DEBUG("[App spawn arg] " << args.toStyledString());

		// The workDir is a new random dir. the files that we create here
		// should not exist, so if any exist then have createFile()
		// throw an error because it could be a bug or an attack.

		createFile(session.workDir->getPath() + "/args.json",
			args.toStyledString(), 0600,
			session.uid, session.gid,
			false, __FILE__, __LINE__);

		const string dir = session.workDir->getPath() + "/args";
		makeDirTree(dir, "u=rwx,g=,o=",
			session.uid,
			session.gid);

		const Json::Value &constArgs = const_cast<const Json::Value &>(args);
		Json::Value::const_iterator it, end = constArgs.end();
		for (it = constArgs.begin(); it != end; it++) {
			const Json::Value &value = *it;
			switch (value.type()) {
			case Json::nullValue:
			case Json::intValue:
			case Json::uintValue:
			case Json::realValue:
			case Json::stringValue:
			case Json::booleanValue:
				createFile(dir + "/" + it.name(),
					jsonValueToString(*it),
					0600, session.uid, session.gid,
					false, __FILE__, __LINE__);
				break;
			default:
				createFile(dir + "/" + it.name() + ".json",
					jsonValueToString(*it),
					0600, session.uid, session.gid,
					false, __FILE__, __LINE__);
				break;
			}
		}
	}

	string jsonValueToString(const Json::Value &value) const {
		switch (value.type()) {
		case Json::nullValue:
			return string();
		case Json::intValue:
			return toString(value.asInt64());
		case Json::uintValue:
			return toString(value.asUInt64());
		case Json::realValue:
			return toString(value.asDouble());
		case Json::stringValue:
			return value.asString();
		case Json::booleanValue:
			if (value.asBool()) {
				return "true";
			} else {
				return "false";
			}
		default:
			return value.toStyledString();
		}
	}

	#if 0
	void inferApplicationInfo() const {
		TRACE_POINT();
		session.result.codeRevision = readFromRevisionFile();
		if (session.result.codeRevision.empty()) {
			session.result.codeRevision = inferCodeRevisionFromCapistranoSymlink();
		}
	}

	string readFromRevisionFile() const {
		TRACE_POINT();
		string filename = config->appRoot + "/REVISION";
		try {
			if (fileExists(filename)) {
				return strip(readAll(filename));
			}
		} catch (const SystemException &e) {
			P_WARN("Cannot access " << filename << ": " << e.what());
		}
		return string();
	}

	string inferCodeRevisionFromCapistranoSymlink() const {
		TRACE_POINT();
		if (extractBaseName(config->appRoot) == "current") {
			string appRoot = config->appRoot.toString(); // null terminate string
			char buf[PATH_MAX + 1];
			ssize_t ret;

			do {
				ret = readlink(appRoot.c_str(), buf, PATH_MAX);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				if (errno == EINVAL) {
					return string();
				} else {
					int e = errno;
					P_WARN("Cannot read symlink " << appRoot << ": " << strerror(e));
				}
			}

			buf[ret] = '\0';
			return extractBaseName(buf);
		} else {
			return string();
		}
	}
	#endif

	void findFreePortOrSocketFile() {
		TRACE_POINT();
		session.expectedStartPort = findFreePort();
		if (session.expectedStartPort == 0) {
			throwSpawnExceptionBecauseOfFailureToFindFreePort();
		}

		// TODO: support Unix domain sockets in the future
		// session.expectedStartSocketFile = findFreeSocketFile();
	}

	unsigned int findFreePort() {
		TRACE_POINT();
		unsigned int tryCount = 1;
		unsigned int maxTries;

		while (true) {
			unsigned int port;

			boost::this_thread::interruption_point();

			{
				boost::lock_guard<boost::mutex> l(context->syncher);
				port = context->nextPort;
				context->nextPort++;
				if (context->nextPort > context->maxPortRange) {
					context->nextPort = context->minPortRange;
				}
				maxTries = context->maxPortRange -
					context->minPortRange + 1;
			}

			unsigned long long timeout1 = 100000;
			unsigned long long timeout2 = 100000;

			if (!pingTcpServer("127.0.0.1", port, &timeout1)
			 && !pingTcpServer("0.0.0.0", port, &timeout2))
			{
				return port;
			} else if (tryCount >= maxTries) {
				return 0;
			} else if (timer.usecElapsed() >= session.timeoutUsec) {
				throwSpawnExceptionBecauseOfPortFindingTimeout();
			} // else: try again
		}
	}

	void adjustTimeout() {
		unsigned long long elapsed = timer.usecElapsed();

		if (elapsed >= session.timeoutUsec) {
			session.timeoutUsec = 0;
		} else {
			session.timeoutUsec -= elapsed;
		}
	}

	void throwSpawnExceptionBecauseOfPortFindingTimeout() {
		assert(config->genericApp || config->findFreePort);
		SpawnException e(TIMEOUT_ERROR, session.journey, config);
		e.setProblemDescriptionHTML(
			"<p>The " PROGRAM_NAME " application server tried"
			" to look for a free TCP port for the web application"
			" to start on. But this took too much time, so "
			SHORT_PROGRAM_NAME " put a stop to that.</p>");

		unsigned int minPortRange, maxPortRange;
		{
			boost::lock_guard<boost::mutex> l(context->syncher);
			minPortRange = context->minPortRange;
			maxPortRange = context->maxPortRange;
		}

		e.setSolutionDescriptionHTML(
			"<div class=\"multiple-solutions\">"

			"<h3>Check whether the server is low on resources</h3>"
			"<p>Maybe the server is currently so low on resources that"
			" all the work that needed to be done, could not finish within"
			" the given time limit."
			" Please inspect the server resource utilization statistics"
			" in the <em>diagnostics</em> section to verify"
			" whether server is indeed low on resources.</p>"
			"<p>If so, then either increase the spawn timeout (currently"
			" configured at " + toString(config->startTimeoutMsec / 1000)
			+ " sec), or find a way to lower the server's resource"
			" utilization.</p>"

			"<h3>Limit the port range that " SHORT_PROGRAM_NAME " searches in</h3>"
			"<p>Maybe the port range in which " SHORT_PROGRAM_NAME
			" tried to search for a free port for the application is"
			" large, and at the same time there were very few free ports"
			" available.</p>"
			"<p>If this is the case, then please configure the "
			SHORT_PROGRAM_NAME " application spawning port range"
			" to a range that is known to have many free ports. The port"
			" range is currently configured at " + toString(minPortRange)
			+ "-" + toString(maxPortRange) + ".</p>"

			"</div>"
		);

		throw e.finalize();
	}

	void throwSpawnExceptionBecauseOfFailureToFindFreePort() {
		assert(config->genericApp || config->findFreePort);
		unsigned int minPortRange, maxPortRange;
		{
			boost::lock_guard<boost::mutex> l(context->syncher);
			minPortRange = context->minPortRange;
			maxPortRange = context->maxPortRange;
		}

		SpawnException e(INTERNAL_ERROR, session.journey, config);
		e.setSummary("Could not find a free port to spawn the application on.");
		e.setProblemDescriptionHTML(
			"<p>The " PROGRAM_NAME " application server tried"
			" to look for a free TCP port for the web application"
			" to start on, but was unable to find one.</p>");
		e.setSolutionDescriptionHTML(
			"<div class=\"sole-solutions\">"

			"<p>Maybe the port range in which " SHORT_PROGRAM_NAME
			" tried to search for a free port, had very few or no"
			" free ports.</p>"
			"<p>If this is the case, then please configure the "
			SHORT_PROGRAM_NAME " application spawning port range"
			" to a range that is known to have many free ports. The port"
			" range is currently configured at " + toString(minPortRange)
			+ "-" + toString(maxPortRange) + ".</p>"

			"</div>");
		throw e.finalize();
	}

public:
	struct DebugSupport {
		virtual ~DebugSupport() { }
		virtual void beforeAdjustTimeout() { }
	};

	DebugSupport *debugSupport;


	HandshakePrepare(HandshakeSession &_session,
		const Json::Value &extraArgs = Json::Value())
		: session(_session),
		  context(_session.context),
		  config(_session.config),
		  args(extraArgs),
		  timer(false),
		  debugSupport(NULL)
	{
		assert(_session.context != NULL);
		assert(_session.context->isFinalized());
		assert(_session.config != NULL);
	}

	HandshakePrepare &execute() {
		TRACE_POINT();

		// We do not set SPAWNING_KIT_PREPARATION to the IN_PROGRESS or
		// PERFORMED state here. That will be done by the caller because
		// it may want to perform additional preparation.

		try {
			timer.start();

			resolveUserAndGroup();
			createWorkDir();
			openWorkDirSubdirFds();
			initializeResult();

			UPDATE_TRACE_POINT();
			// Disabled to fix CVE-2017-16355
			//inferApplicationInfo();
			if (config->genericApp || config->findFreePort) {
				findFreePortOrSocketFile();
			}

			UPDATE_TRACE_POINT();
			preparePredefinedArgs();
			prepareArgsFromAppConfig();
			absolutizeKeyArgPaths();
			dumpArgsIntoWorkDir();

			if (debugSupport != NULL) {
				debugSupport->beforeAdjustTimeout();
			}

			adjustTimeout();
		} catch (const SpawnException &) {
			session.journey.setStepErrored(SPAWNING_KIT_PREPARATION);
			throw;
		} catch (const std::exception &e) {
			session.journey.setStepErrored(SPAWNING_KIT_PREPARATION);
			throw SpawnException(e, session.journey, config).finalize();
		}

		return *this;
	}

	void finalize() {
		session.workDir->finalize(session.uid, session.gid);
	}
};


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_HANDSHAKE_PREPARE_H_ */
