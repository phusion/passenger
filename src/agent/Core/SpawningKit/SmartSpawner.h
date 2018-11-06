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
#ifndef _PASSENGER_SPAWNING_KIT_SMART_SPAWNER_H_
#define _PASSENGER_SPAWNING_KIT_SMART_SPAWNER_H_

#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <cassert>

#include <adhoc_lve.h>

#include <LoggingKit/Logging.h>
#include <Constants.h>
#include <Exceptions.h>
#include <DataStructures/StringKeyTable.h>
#include <ProcessManagement/Utils.h>
#include <SystemTools/ProcessMetricsCollector.h>
#include <SystemTools/SystemTime.h>
#include <FileTools/FileManip.h>
#include <IOTools/BufferedIO.h>
#include <JsonTools/JsonUtils.h>
#include <Utils/ScopeGuard.h>
#include <Utils/AsyncSignalSafeUtils.h>
#include <LveLoggingDecorator.h>
#include <Core/SpawningKit/Spawner.h>
#include <Core/SpawningKit/Exceptions.h>
#include <Core/SpawningKit/PipeWatcher.h>
#include <Core/SpawningKit/Handshake/Session.h>
#include <Core/SpawningKit/Handshake/Prepare.h>
#include <Core/SpawningKit/Handshake/Perform.h>
#include <Core/SpawningKit/Handshake/BackgroundIOCapturer.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;
using namespace boost;
using namespace oxt;


class SmartSpawner: public Spawner {
private:
	const string preloaderCommandString;
	string preloaderEnvvars;
	string preloaderUserInfo;
	string preloaderUlimits;
	StringKeyTable<string> preloaderAnnotations;
	AppPoolOptions options;

	// Protects m_lastUsed and pid.
	mutable boost::mutex simpleFieldSyncher;
	// Protects everything else.
	mutable boost::mutex syncher;

	// Preloader information.
	pid_t pid;
	FileDescriptor preloaderStdin;
	string socketAddress;
	unsigned long long m_lastUsed;


	/**
	 * Behaves like <tt>waitpid(pid, status, WNOHANG)</tt>, but waits at most
	 * <em>timeout</em> miliseconds for the process to exit.
	 */
	static int timedWaitpid(pid_t pid, int *status, unsigned long long timeout) {
		Timer<SystemTime::GRAN_10MSEC> timer;
		int ret;

		do {
			ret = syscalls::waitpid(pid, status, WNOHANG);
			if (ret > 0 || ret == -1) {
				return ret;
			} else {
				syscalls::usleep(10000);
			}
		} while (timer.elapsed() < timeout);
		return 0; // timed out
	}

	static bool osProcessExists(pid_t pid) {
		if (syscalls::kill(pid, 0) == 0) {
			/* On some environments, e.g. Heroku, the init process does
			 * not properly reap adopted zombie processes, which can interfere
			 * with our process existance check. To work around this, we
			 * explicitly check whether or not the process has become a zombie.
			 */
			return !isZombie(pid);
		} else {
			return errno != ESRCH;
		}
	}

	static bool isZombie(pid_t pid) {
		string filename = "/proc/" + toString(pid) + "/status";
		FILE *f = fopen(filename.c_str(), "r");
		if (f == NULL) {
			// Don't know.
			return false;
		}

		bool result = false;
		while (!feof(f)) {
			char buf[512];
			const char *line;

			line = fgets(buf, sizeof(buf), f);
			if (line == NULL) {
				break;
			}
			if (strcmp(line, "State:	Z (zombie)\n") == 0) {
				// Is a zombie.
				result = true;
				break;
			}
		}
		fclose(f);
		return result;
	}

	static string createCommandString(const vector<string> &command) {
		string result;
		vector<string>::const_iterator it;
		vector<string>::const_iterator begin = command.begin();
		vector<string>::const_iterator end = command.end();

		for (it = begin; it != end; it++) {
			if (it != begin) {
				result.append(1, ' ');
			}
			result.append(escapeShell(*it));
		}

		return result;
	}

	void setConfigFromAppPoolOptions(Config *config, Json::Value &extraArgs,
		const AppPoolOptions &options)
	{
		Spawner::setConfigFromAppPoolOptions(config, extraArgs, options);
		config->spawnMethod = P_STATIC_STRING("smart");
	}

	struct StdChannelsAsyncOpenState {
		const int workDirFd;

		oxt::thread *stdinOpenThread;
		FileDescriptor stdinFd;
		int stdinOpenErrno;

		oxt::thread *stdoutAndErrOpenThread;
		FileDescriptor stdoutAndErrFd;
		int stdoutAndErrOpenErrno;

		BackgroundIOCapturerPtr stdoutAndErrCapturer;

		StdChannelsAsyncOpenState(int _workDirFd)
			: workDirFd(_workDirFd),
			  stdinOpenThread(NULL),
			  stdoutAndErrOpenThread(NULL)
			{ }

		~StdChannelsAsyncOpenState() {
			boost::this_thread::disable_interruption di;
			boost::this_thread::disable_syscall_interruption dsi;
			if (stdinOpenThread != NULL) {
				stdinOpenThread->interrupt_and_join();
				delete stdinOpenThread;
			}
			if (stdoutAndErrOpenThread != NULL) {
				stdoutAndErrOpenThread->interrupt_and_join();
				delete stdoutAndErrOpenThread;
			}
		}
	};

	typedef boost::shared_ptr<StdChannelsAsyncOpenState> StdChannelsAsyncOpenStatePtr;

	StdChannelsAsyncOpenStatePtr openStdChannelsFifosAsynchronously(
		HandshakeSession &session)
	{
		StdChannelsAsyncOpenStatePtr state = boost::make_shared<StdChannelsAsyncOpenState>(
			session.workDirFd);
		state->stdinOpenThread = new oxt::thread(boost::bind(
			openStdinChannel, state, session.workDir->getPath()),
			"FIFO opener: " + session.workDir->getPath() + "/stdin", 1024 * 128);
		state->stdoutAndErrOpenThread = new oxt::thread(boost::bind(
			openStdoutAndErrChannel, state, session.workDir->getPath()),
			"FIFO opener: " + session.workDir->getPath() + "/stdout_and_err", 1024 * 128);
		return state;
	}

	void waitForStdChannelFifosToBeOpenedByPeer(const StdChannelsAsyncOpenStatePtr &state,
		HandshakeSession &session, pid_t pid)
	{
		TRACE_POINT();
		MonotonicTimeUsec startTime = SystemTime::getMonotonicUsec();
		ScopeGuard guard(boost::bind(adjustTimeout, startTime, &session.timeoutUsec));

		try {
			if (state->stdinOpenThread->try_join_for(
				boost::chrono::microseconds(session.timeoutUsec)))
			{
				delete state->stdinOpenThread;
				state->stdinOpenThread = NULL;
				if (state->stdinFd == -1) {
					throw SystemException("Error opening FIFO "
						+ session.workDir->getPath() + "/stdin",
						state->stdinOpenErrno);
				} else {
					P_LOG_FILE_DESCRIPTOR_PURPOSE(state->stdinFd,
						"App " << pid << " (" << options.appRoot
						<< ") stdin");
					adjustTimeout(startTime, &session.timeoutUsec);
					startTime = SystemTime::getMonotonicUsec();
				}
			} else {
				throw TimeoutException("Timeout opening FIFO "
					+ session.workDir->getPath() + "/stdin");
			}

			UPDATE_TRACE_POINT();
			if (state->stdoutAndErrOpenThread->try_join_for(
				boost::chrono::microseconds(session.timeoutUsec)))
			{
				delete state->stdoutAndErrOpenThread;
				state->stdoutAndErrOpenThread = NULL;
				if (state->stdoutAndErrFd == -1) {
					throw SystemException("Error opening FIFO "
						+ session.workDir->getPath() + "/stdout_and_err",
						state->stdoutAndErrOpenErrno);
				} else {
					P_LOG_FILE_DESCRIPTOR_PURPOSE(state->stdoutAndErrFd,
						"App " << pid << " (" << options.appRoot
						<< ") stdoutAndErr");
				}
			} else {
				throw TimeoutException("Timeout opening FIFO "
					+ session.workDir->getPath() + "/stdout_and_err");
			}

			state->stdoutAndErrCapturer = boost::make_shared<BackgroundIOCapturer>(
				state->stdoutAndErrFd, pid, session.config->appGroupName,
				session.config->logFile);
			state->stdoutAndErrCapturer->start();
		} catch (const boost::system::system_error &e) {
			throw SystemException(e.what(), e.code().value());
		}
	}

	static void openStdinChannel(StdChannelsAsyncOpenStatePtr state,
		const string &workDir)
	{
		int fd = syscalls::openat(state->workDirFd, "stdin", O_WRONLY | O_APPEND | O_NOFOLLOW);
		int e = errno;
		state->stdinFd.assign(fd, __FILE__, __LINE__);
		state->stdinOpenErrno = e;
	}

	static void openStdoutAndErrChannel(StdChannelsAsyncOpenStatePtr state,
		const string &workDir)
	{
		int fd = syscalls::openat(state->workDirFd, "stdout_and_err", O_RDONLY | O_NOFOLLOW);
		int e = errno;
		state->stdoutAndErrFd.assign(fd, __FILE__, __LINE__);
		state->stdoutAndErrOpenErrno = e;
	}

	bool preloaderStarted() const {
		return pid != -1;
	}

	void startPreloader() {
		TRACE_POINT();
		boost::this_thread::disable_interruption di;
		boost::this_thread::disable_syscall_interruption dsi;
		assert(!preloaderStarted());
		P_DEBUG("Spawning new preloader: appRoot=" << options.appRoot);

		Config config;
		Json::Value extraArgs;
		try {
			setConfigFromAppPoolOptions(&config, extraArgs, options);
			config.startCommand = preloaderCommandString;
		} catch (const std::exception &originalException) {
			Journey journey(SPAWN_THROUGH_PRELOADER, true);
			journey.setStepErrored(SPAWNING_KIT_PREPARATION, true);
			throw SpawnException(originalException, journey,
				&config).finalize();
		}

		HandshakeSession session(*context, config, START_PRELOADER);
		session.journey.setStepInProgress(SPAWNING_KIT_PREPARATION);

		try {
			internalStartPreloader(config, session, extraArgs);
		} catch (const SpawnException &) {
			throw;
		} catch (const std::exception &originalException) {
			session.journey.setStepErrored(SPAWNING_KIT_PREPARATION);
			throw SpawnException(originalException, session.journey,
				&config).finalize();
		}
	}

	void internalStartPreloader(Config &config, HandshakeSession &session,
		const Json::Value &extraArgs)
	{
		TRACE_POINT();
		HandshakePrepare(session, extraArgs).execute();
		Pipe stdinChannel = createPipe(__FILE__, __LINE__);
		Pipe stdoutAndErrChannel = createPipe(__FILE__, __LINE__);
		adhoc_lve::LveEnter scopedLveEnter(LveLoggingDecorator::lveInitOnce(),
			session.uid,
			config.lveMinUid,
			LveLoggingDecorator::lveExitCallback);
		LveLoggingDecorator::logLveEnter(scopedLveEnter,
			session.uid,
			config.lveMinUid);
		string agentFilename = context->resourceLocator
			->findSupportBinary(AGENT_EXE);

		session.journey.setStepPerformed(SPAWNING_KIT_PREPARATION);
		session.journey.setStepInProgress(SPAWNING_KIT_FORK_SUBPROCESS);
		session.journey.setStepInProgress(SUBPROCESS_BEFORE_FIRST_EXEC);

		pid_t pid = syscalls::fork();
		if (pid == 0) {
			int e;
			char buf[1024];
			const char *end = buf + sizeof(buf);
			namespace ASSU = AsyncSignalSafeUtils;

			resetSignalHandlersAndMask();
			disableMallocDebugging();
			int stdinCopy = dup2(stdinChannel.first, 3);
			int stdoutAndErrCopy = dup2(stdoutAndErrChannel.second, 4);
			dup2(stdinCopy, 0);
			dup2(stdoutAndErrCopy, 1);
			dup2(stdoutAndErrCopy, 2);
			closeAllFileDescriptors(2);

			execlp(agentFilename.c_str(),
				agentFilename.c_str(),
				"spawn-env-setupper",
				session.workDir->getPath().c_str(),
				"--before",
				(char *) 0);

			char *pos = buf;
			e = errno;
			pos = ASSU::appendData(pos, end, "Cannot execute \"");
			pos = ASSU::appendData(pos, end, agentFilename.data(), agentFilename.size());
			pos = ASSU::appendData(pos, end, "\": ");
			pos = ASSU::appendData(pos, end, ASSU::limitedStrerror(e));
			pos = ASSU::appendData(pos, end, " (errno=");
			pos = ASSU::appendInteger<int, 10>(pos, end, e);
			pos = ASSU::appendData(pos, end, ")\n");
			ASSU::printError(buf, pos - buf);
			_exit(1);

		} else if (pid == -1) {
			int e = errno;
			UPDATE_TRACE_POINT();
			session.journey.setStepErrored(SPAWNING_KIT_FORK_SUBPROCESS);
			SpawnException ex(OPERATING_SYSTEM_ERROR, session.journey, &config);
			ex.setSummary(StaticString("Cannot fork a new process: ") + strerror(e)
				+ " (errno=" + toString(e) + ")");
			ex.setAdvancedProblemDetails(StaticString("Cannot fork a new process: ")
				+ strerror(e) + " (errno=" + toString(e) + ")");
			throw ex.finalize();

		} else {
			UPDATE_TRACE_POINT();
			session.journey.setStepPerformed(SPAWNING_KIT_FORK_SUBPROCESS);
			session.journey.setStepInProgress(SPAWNING_KIT_HANDSHAKE_PERFORM);

			scopedLveEnter.exit();

			P_LOG_FILE_DESCRIPTOR_PURPOSE(stdinChannel.second,
				"Preloader " << pid << " (" << options.appRoot << ") stdin");
			P_LOG_FILE_DESCRIPTOR_PURPOSE(stdoutAndErrChannel.first,
				"Preloader " << pid << " (" << options.appRoot << ") stdoutAndErr");

			UPDATE_TRACE_POINT();
			ScopeGuard guard(boost::bind(nonInterruptableKillAndWaitpid, pid));
			P_DEBUG("Preloader process forked for appRoot=" << options.appRoot
				<< ": PID " << pid);
			stdinChannel.first.close();
			stdoutAndErrChannel.second.close();

			HandshakePerform(session, pid, stdinChannel.second,
				stdoutAndErrChannel.first).execute();
			string envvars, userInfo, ulimits;
			// If a new output variable was added to this function,
			// then don't forget to also update these locations:
			// - the critical section below
			// - bottom of stopPreloader()
			// - addPreloaderEnvDumps()
			HandshakePerform::loadBasicInfoFromEnvDumpDir(session.envDumpDir,
				session.envDumpDirFd, envvars, userInfo, ulimits);
			string socketAddress = findPreloaderCommandSocketAddress(session);

			{
				boost::lock_guard<boost::mutex> l(simpleFieldSyncher);
				this->pid = pid;
				this->socketAddress = socketAddress;
				this->preloaderStdin = stdinChannel.second;
				this->preloaderEnvvars = envvars;
				this->preloaderUserInfo = userInfo;
				this->preloaderUlimits = ulimits;
				this->preloaderAnnotations = loadAnnotationsFromEnvDumpDir(
					session.envDumpDir, session.envDumpAnnotationsDirFd);
			}

			PipeWatcherPtr watcher = boost::make_shared<PipeWatcher>(
				stdoutAndErrChannel.first, "output", config.appGroupName,
				config.logFile, pid);
			watcher->initialize();
			watcher->start();

			UPDATE_TRACE_POINT();
			guard.clear();
			session.journey.setStepPerformed(SPAWNING_KIT_HANDSHAKE_PERFORM);
			P_INFO("Preloader for " << options.appRoot <<
				" started on PID " << pid <<
				", listening on " << socketAddress);
		}
	}

	void stopPreloader() {
		TRACE_POINT();
		boost::this_thread::disable_interruption di;
		boost::this_thread::disable_syscall_interruption dsi;

		if (!preloaderStarted()) {
			return;
		}

		preloaderStdin.close(false);

		if (timedWaitpid(pid, NULL, 5000) == 0) {
			P_DEBUG("Preloader did not exit in time, killing it...");
			syscalls::kill(pid, SIGKILL);
			syscalls::waitpid(pid, NULL, 0);
		}

		// Delete socket after the process has exited so that it
		// doesn't crash upon deleting a nonexistant file.
		if (getSocketAddressType(socketAddress) == SAT_UNIX) {
			string filename = parseUnixSocketAddress(socketAddress);
			syscalls::unlink(filename.c_str());
		}

		{
			boost::lock_guard<boost::mutex> l(simpleFieldSyncher);
			pid = -1;
			socketAddress.clear();
			preloaderEnvvars.clear();
			preloaderUserInfo.clear();
			preloaderUlimits.clear();
			preloaderAnnotations.clear();
		}
	}

	FileDescriptor connectToPreloader(HandshakeSession &session) {
		TRACE_POINT();
		FileDescriptor fd(connectToServer(socketAddress, __FILE__, __LINE__), NULL, 0);
		P_LOG_FILE_DESCRIPTOR_PURPOSE(fd, "Preloader " << pid
			<< " (" << session.config->appRoot << ") connection");
		return fd;
	}

	struct ForkResult {
		pid_t pid;
		FileDescriptor stdinFd;
		FileDescriptor stdoutAndErrFd;
		string alreadyReadStdoutAndErrData;

		ForkResult()
			: pid(-1)
			{ }

		ForkResult(pid_t _pid, const FileDescriptor &_stdinFd,
			const FileDescriptor &_stdoutAndErrFd,
			const string &_alreadyReadStdoutAndErrData)
			: pid(_pid),
			  stdinFd(_stdinFd),
			  stdoutAndErrFd(_stdoutAndErrFd),
			  alreadyReadStdoutAndErrData(_alreadyReadStdoutAndErrData)
			{ }
	};

	struct PreloaderCrashed {
		SystemException *systemException;
		IOException *ioException;

		PreloaderCrashed(const SystemException &e)
			: systemException(new SystemException(e)),
			  ioException(NULL)
			{ }

		PreloaderCrashed(const IOException &e)
			: systemException(NULL),
			  ioException(new IOException(e))
			{ }

		~PreloaderCrashed() {
			delete systemException;
			delete ioException;
		}

		const oxt::tracable_exception &getException() const {
			if (systemException != NULL) {
				return *systemException;
			} else {
				return *ioException;
			}
		}
	};

	ForkResult invokeForkCommand(HandshakeSession &session, JourneyStep &stepToMarkAsErrored) {
		TRACE_POINT();

		P_ASSERT_EQ(session.journey.getStepInfo(SPAWNING_KIT_PREPARATION).state,
			STEP_PERFORMED);

		try {
			StdChannelsAsyncOpenStatePtr stdChannelsAsyncOpenState =
				openStdChannelsFifosAsynchronously(session);
			return internalInvokeForkCommand(session, stdChannelsAsyncOpenState,
				stepToMarkAsErrored);
		} catch (const PreloaderCrashed &crashException1) {
			UPDATE_TRACE_POINT();
			P_WARN("An error occurred while spawning an application process: "
				<< crashException1.getException().what());
			P_WARN("The application preloader seems to have crashed,"
				" restarting it and trying again...");

			session.journey.reset();

			try {
				stopPreloader();
			} catch (const SpawnException &) {
				throw;
			} catch (const std::exception &originalException) {
				session.journey.setStepErrored(SPAWNING_KIT_PREPARATION, true);

				SpawnException e(originalException, session.journey, session.config);
				e.setSummary(StaticString("Error stopping a crashed preloader: ")
					+ originalException.what());
				e.setProblemDescriptionHTML(
					"<p>The " PROGRAM_NAME " application server tried"
					" to start the web application by communicating with a"
					" helper process that we call a \"preloader\". However,"
					" this helper process crashed unexpectedly. "
					SHORT_PROGRAM_NAME " then tried to restart it, but"
					" encountered the following error while trying to"
					" stop the preloader:</p>"
					"<pre>" + escapeHTML(originalException.what()) + "</pre>");
				throw e.finalize();
			}

			UPDATE_TRACE_POINT();
			startPreloader();
			session.journey.reset();
			session.journey.setStepPerformed(SPAWNING_KIT_PREPARATION, true);

			UPDATE_TRACE_POINT();
			try {
				StdChannelsAsyncOpenStatePtr stdChannelsAsyncOpenState =
					openStdChannelsFifosAsynchronously(session);
				return internalInvokeForkCommand(session, stdChannelsAsyncOpenState,
					stepToMarkAsErrored);
			} catch (const PreloaderCrashed &crashException2) {
				UPDATE_TRACE_POINT();

				session.journey.reset();
				session.journey.setStepErrored(SPAWNING_KIT_PREPARATION, true);

				try {
					stopPreloader();
				} catch (const SpawnException &) {
					throw;
				} catch (const std::exception &originalException) {
					SpawnException e(originalException, session.journey, session.config);
					e.setSummary(StaticString("Error stopping a crashed preloader: ")
						+ originalException.what());
					e.setProblemDescriptionHTML(
						"<p>The " PROGRAM_NAME " application server tried"
						" to start the web application by communicating with a"
						" helper process that we call a \"preloader\". However,"
						" this helper process crashed unexpectedly. "
						SHORT_PROGRAM_NAME " then tried to restart it, but"
						" encountered the following error while trying to"
						" stop the preloader:</p>"
						"<pre>" + escapeHTML(originalException.what()) + "</pre>");
					throw e.finalize();
				}

				SpawnException e(crashException2.getException(),
					session.journey, session.config);
				e.setSummary(StaticString("An application preloader crashed: ") +
					crashException2.getException().what());
				e.setProblemDescriptionHTML(
					"<p>The " PROGRAM_NAME " application server tried"
					" to start the web application by communicating with a"
					" helper process that we call a \"preloader\". However,"
					" this helper process crashed unexpectedly:</p>"
					"<pre>" + escapeHTML(crashException2.getException().what())
					+ "</pre>");
				throw e.finalize();
			}
		}
	}

	ForkResult internalInvokeForkCommand(HandshakeSession &session,
		const StdChannelsAsyncOpenStatePtr &stdChannelsAsyncOpenState,
		JourneyStep &stepToMarkAsErrored)
	{
		TRACE_POINT();

		P_ASSERT_EQ(session.journey.getStepInfo(SPAWNING_KIT_PREPARATION).state,
			STEP_PERFORMED);

		session.journey.setStepInProgress(SPAWNING_KIT_CONNECT_TO_PRELOADER);
		stepToMarkAsErrored = SPAWNING_KIT_CONNECT_TO_PRELOADER;
		FileDescriptor fd;
		string line;
		Json::Value doc;
		try {
			fd = connectToPreloader(session);
		} catch (const SystemException &e) {
			throw PreloaderCrashed(e);
		} catch (const IOException &e) {
			throw PreloaderCrashed(e);
		}

		session.journey.setStepPerformed(SPAWNING_KIT_CONNECT_TO_PRELOADER);
		session.journey.setStepInProgress(SPAWNING_KIT_SEND_COMMAND_TO_PRELOADER);
		stepToMarkAsErrored = SPAWNING_KIT_SEND_COMMAND_TO_PRELOADER;
		try {
			sendForkCommand(session, fd);
		} catch (const SystemException &e) {
			throw PreloaderCrashed(e);
		} catch (const IOException &e) {
			throw PreloaderCrashed(e);
		}

		session.journey.setStepPerformed(SPAWNING_KIT_SEND_COMMAND_TO_PRELOADER);
		session.journey.setStepInProgress(SPAWNING_KIT_READ_RESPONSE_FROM_PRELOADER);
		stepToMarkAsErrored = SPAWNING_KIT_READ_RESPONSE_FROM_PRELOADER;
		try {
			line = readForkCommandResponse(session, fd);
		} catch (const SystemException &e) {
			throw PreloaderCrashed(e);
		} catch (const IOException &e) {
			throw PreloaderCrashed(e);
		}

		session.journey.setStepPerformed(SPAWNING_KIT_READ_RESPONSE_FROM_PRELOADER);
		session.journey.setStepInProgress(SPAWNING_KIT_PARSE_RESPONSE_FROM_PRELOADER);
		stepToMarkAsErrored = SPAWNING_KIT_PARSE_RESPONSE_FROM_PRELOADER;
		doc = parseForkCommandResponse(session, line);

		session.journey.setStepPerformed(SPAWNING_KIT_PARSE_RESPONSE_FROM_PRELOADER);
		session.journey.setStepInProgress(SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER);
		stepToMarkAsErrored = SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER;
		return handleForkCommandResponse(session, stdChannelsAsyncOpenState, doc);
	}

	void sendForkCommand(HandshakeSession &session, const FileDescriptor &fd) {
		TRACE_POINT();
		Json::Value doc;

		doc["command"] = "spawn";
		doc["work_dir"] = session.workDir->getPath();

		writeExact(fd, Json::FastWriter().write(doc), &session.timeoutUsec);
	}

	string readForkCommandResponse(HandshakeSession &session, const FileDescriptor &fd) {
		TRACE_POINT();
		BufferedIO io(fd);

		try {
			return io.readLine(10240, &session.timeoutUsec);
		} catch (const SecurityException &) {
			session.journey.setStepErrored(SPAWNING_KIT_READ_RESPONSE_FROM_PRELOADER);

			SpawnException e(INTERNAL_ERROR, session.journey, session.config);
			addPreloaderEnvDumps(e);
			e.setSummary("The preloader process sent a response that exceeds the maximum size limit.");
			e.setProblemDescriptionHTML(
				"<p>The " PROGRAM_NAME " application server tried"
				" to start the web application by communicating with a"
				" helper process that we call a \"preloader\". However,"
				" this helper process sent a response that exceeded the"
				" internally-defined maximum size limit.</p>");
			e.setSolutionDescriptionHTML(
				"<p class=\"sole-solution\">"
				"This is probably a bug in the preloader process. Please "
				"<a href=\"" SUPPORT_URL "\">"
				"report this bug</a>."
				"</p>");
			throw e.finalize();
		}
	}

	Json::Value parseForkCommandResponse(HandshakeSession &session, const string &data) {
		TRACE_POINT();
		Json::Value doc;
		Json::Reader reader;

		if (!reader.parse(data, doc)) {
			session.journey.setStepErrored(SPAWNING_KIT_PARSE_RESPONSE_FROM_PRELOADER);

			SpawnException e(INTERNAL_ERROR, session.journey, session.config);
			addPreloaderEnvDumps(e);
			e.setSummary("The preloader process sent an unparseable response: " + data);
			e.setProblemDescriptionHTML(
				"<p>The " PROGRAM_NAME " application server tried"
				" to start the web application by communicating with a"
				" helper process that we call a \"preloader\". However,"
				" this helper process sent a response that looks like"
				" gibberish.</p>"
				"<p>The response is as follows:</p>"
				"<pre>" + escapeHTML(data) + "</pre>");
			e.setSolutionDescriptionHTML(
				"<p class=\"sole-solution\">"
				"This is probably a bug in the preloader process. Please "
				"<a href=\"" SUPPORT_URL "\">"
				"report this bug</a>."
				"</p>");
			throw e.finalize();
		}

		UPDATE_TRACE_POINT();
		if (!validateForkCommandResponse(doc)) {
			session.journey.setStepErrored(SPAWNING_KIT_PARSE_RESPONSE_FROM_PRELOADER);

			SpawnException e(INTERNAL_ERROR, session.journey, session.config);
			addPreloaderEnvDumps(e);
			e.setSummary("The preloader process sent a response that does not"
				" match the expected structure: " + stringifyJson(doc));
			e.setProblemDescriptionHTML(
				"<p>The " PROGRAM_NAME " application server tried"
				" to start the web application by communicating with a"
				" helper process that we call a \"preloader\". However,"
				" this helper process sent a response that does not match"
				" the structure that " SHORT_PROGRAM_NAME " expects.</p>"
				"<p>The response is as follows:</p>"
				"<pre>" + escapeHTML(doc.toStyledString()) + "</pre>");
			e.setSolutionDescriptionHTML(
				"<p class=\"sole-solution\">"
				"This is probably a bug in the preloader process. Please "
				"<a href=\"" SUPPORT_URL "\">"
				"report this bug</a>."
				"</p>");
			throw e.finalize();
		}

		return doc;
	}

	bool validateForkCommandResponse(const Json::Value &doc) const {
		if (!doc.isObject()) {
			return false;
		}
		if (!doc.isMember("result") || !doc["result"].isString()) {
			return false;
		}
		if (doc["result"].asString() == "ok") {
			if (!doc.isMember("pid") || !doc["pid"].isInt()) {
				return false;
			}
			return true;
		} else if (doc["result"].asString() == "error") {
			if (!doc.isMember("message") || !doc["message"].isString()) {
				return false;
			}
			return true;
		} else {
			return false;
		}
	}

	ForkResult handleForkCommandResponse(HandshakeSession &session,
		const StdChannelsAsyncOpenStatePtr &stdChannelsAsyncOpenState,
		const Json::Value &doc)
	{
		TRACE_POINT();
		if (doc["result"].asString() == "ok") {
			return handleForkCommandResponseSuccess(session, stdChannelsAsyncOpenState,
				doc);
		} else {
			P_ASSERT_EQ(doc["result"].asString(), "error");
			return handleForkCommandResponseError(session, doc);
		}
	}

	ForkResult handleForkCommandResponseSuccess(HandshakeSession &session,
		const StdChannelsAsyncOpenStatePtr &stdChannelsAsyncOpenState, const Json::Value &doc)
	{
		TRACE_POINT();
		pid_t spawnedPid = doc["pid"].asInt();

		// How do we know the preloader actually forked a process
		// instead of reporting the PID of a random other existing process?
		// For security reasons we perform a bunch of sanity checks,
		// including checking the PID's UID.

		if (spawnedPid < 1) {
			UPDATE_TRACE_POINT();
			session.journey.setStepErrored(SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER);

			SpawnException e(INTERNAL_ERROR, session.journey, session.config);
			addPreloaderEnvDumps(e);
			e.setSummary("The the preloader said it spawned a process with PID "
				+ toString(spawnedPid) + ", which is not allowed.");
			e.setSubprocessPid(spawnedPid);
			e.setStdoutAndErrData(getBackgroundIOCapturerData(
				stdChannelsAsyncOpenState->stdoutAndErrCapturer));
			e.setProblemDescriptionHTML(
				"<h2>Application process has unexpected PID</h2>"
				"<p>The " PROGRAM_NAME " application server tried"
				" to start the web application by communicating with a"
				" helper process that we call a \"preloader\". However,"
				" the preloader reported that it started a process with"
				" a PID of " + toString(spawnedPid) + ", which is not"
				" allowed.</p>");
			if (!session.config->genericApp && session.config->startsUsingWrapper
				&& session.config->wrapperSuppliedByThirdParty)
			{
				e.setSolutionDescriptionHTML(
					"<h2>Please report this bug</h2>"
					"<p class=\"sole-solution\">"
					"This is probably a bug in the preloader process. The preloader "
					"wrapper program is not written by the " PROGRAM_NAME " authors, "
					"but by a third party. Please report this bug to the author of "
					"the preloader wrapper program."
					"</p>");
			} else {
				e.setSolutionDescriptionHTML(
					"<h2>Please report this bug</h2>"
					"<p class=\"sole-solution\">"
					"This is probably a bug in the preloader process. The preloader "
					"is an internal tool part of " PROGRAM_NAME ". Please "
					"<a href=\"" SUPPORT_URL "\">"
					"report this bug</a>."
					"</p>");
			}
			throw e.finalize();
		}

		UPDATE_TRACE_POINT();
		uid_t spawnedUid = getProcessUid(session, spawnedPid,
			stdChannelsAsyncOpenState->stdoutAndErrCapturer);
		if (spawnedUid != session.uid) {
			UPDATE_TRACE_POINT();
			session.journey.setStepErrored(SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER);

			SpawnException e(INTERNAL_ERROR, session.journey, session.config);
			addPreloaderEnvDumps(e);
			e.setSummary("The process that the preloader said it spawned, PID "
				+ toString(spawnedPid) + ", has UID " + toString(spawnedUid)
				+ ", but the expected UID is " + toString(session.uid));
			e.setSubprocessPid(spawnedPid);
			e.setStdoutAndErrData(getBackgroundIOCapturerData(
				stdChannelsAsyncOpenState->stdoutAndErrCapturer));
			e.setProblemDescriptionHTML(
				"<h2>Application process has unexpected UID</h2>"
				"<p>The " PROGRAM_NAME " application server tried"
				" to start the web application by communicating with a"
				" helper process that we call a \"preloader\". However,"
				" the web application process that the preloader started"
				" belongs to the wrong user. The UID of the web"
				" application process should be " + toString(session.uid)
				+ ", but is actually " + toString(session.uid) + ".</p>");
			if (!session.config->genericApp && session.config->startsUsingWrapper
				&& session.config->wrapperSuppliedByThirdParty)
			{
				e.setSolutionDescriptionHTML(
					"<h2>Please report this bug</h2>"
					"<p class=\"sole-solution\">"
					"This is probably a bug in the preloader process. The preloader "
					"wrapper program is not written by the " PROGRAM_NAME " authors, "
					"but by a third party. Please report this bug to the author of "
					"the preloader wrapper program."
					"</p>");
			} else {
				e.setSolutionDescriptionHTML(
					"<h2>Please report this bug</h2>"
					"<p class=\"sole-solution\">"
					"This is probably a bug in the preloader process. The preloader "
					"is an internal tool part of " PROGRAM_NAME ". Please "
					"<a href=\"" SUPPORT_URL "\">"
					"report this bug</a>."
					"</p>");
			}
			throw e.finalize();
		}

		UPDATE_TRACE_POINT();
		ScopeGuard guard(boost::bind(nonInterruptableKillAndWaitpid, spawnedPid));
		waitForStdChannelFifosToBeOpenedByPeer(stdChannelsAsyncOpenState,
			session, spawnedPid);

		UPDATE_TRACE_POINT();
		string alreadyReadStdoutAndErrData;
		if (stdChannelsAsyncOpenState->stdoutAndErrCapturer != NULL) {
			stdChannelsAsyncOpenState->stdoutAndErrCapturer->stop();
			alreadyReadStdoutAndErrData = stdChannelsAsyncOpenState->stdoutAndErrCapturer->getData();
		}
		guard.clear();
		return ForkResult(spawnedPid, stdChannelsAsyncOpenState->stdinFd,
			stdChannelsAsyncOpenState->stdoutAndErrFd,
			alreadyReadStdoutAndErrData);
	}

	ForkResult handleForkCommandResponseError(HandshakeSession &session,
		const Json::Value &doc)
	{
		session.journey.setStepErrored(SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER);

		SpawnException e(INTERNAL_ERROR, session.journey, session.config);
		addPreloaderEnvDumps(e);
		e.setSummary("An error occured while starting the web application: "
			+ doc["message"].asString());
		e.setProblemDescriptionHTML(
			"<p>The " PROGRAM_NAME " application server tried to"
			" start the web application by communicating with a"
			" helper process that we call a \"preloader\". However, "
			" this helper process reported an error:</p>"
			"<pre>" + escapeHTML(doc["message"].asString()) + "</pre>");
		e.setSolutionDescriptionHTML(
			"<p class=\"sole-solution\">"
			"Please try troubleshooting the problem by studying the"
			" <strong>error message</strong> and the"
			" <strong>diagnostics</strong> reports. You can also"
			" consult <a href=\"" SUPPORT_URL "\">the " SHORT_PROGRAM_NAME
			" support resources</a> for help.</p>");
		throw e.finalize();
	}

	void createStdChannelFifos(const HandshakeSession &session) {
		const string &workDir = session.workDir->getPath();
		createFifo(session, workDir + "/stdin");
		createFifo(session, workDir + "/stdout_and_err");
	}

	void createFifo(const HandshakeSession &session, const string &path) {
		int ret;

		do {
			ret = mkfifo(path.c_str(), 0600);
		} while (ret == -1 && errno == EAGAIN);
		if (ret == -1) {
			int e = errno;
			throw FileSystemException("Cannot create FIFO file " + path,
				e, path);
		}

		ret = syscalls::chown(path.c_str(), session.uid, session.gid);
		if (ret == -1) {
			int e = errno;
			throw FileSystemException("Cannot change owner and group on FIFO file " + path,
				e, path);
		}
	}

	string getBackgroundIOCapturerData(const BackgroundIOCapturerPtr &capturer) const {
		if (capturer != NULL) {
			// Sleep shortly to allow the child process to finish writing logs.
			syscalls::usleep(50000);
			return capturer->getData();
		} else {
			return string();
		}
	}

	uid_t getProcessUid(HandshakeSession &session, pid_t pid,
		const BackgroundIOCapturerPtr &stdoutAndErrCapturer)
	{
		TRACE_POINT();
		uid_t uid = (uid_t) -1;

		try {
			vector<pid_t> pids;
			pids.push_back(pid);
			ProcessMetricMap result = ProcessMetricsCollector().collect(pids);
			uid = result[pid].uid;
		} catch (const ParseException &) {
			HandshakePerform::loadJourneyStateFromResponseDir(session, pid, stdoutAndErrCapturer);
			session.journey.setStepErrored(SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER);

			SpawnException e(INTERNAL_ERROR, session.journey, session.config);
			addPreloaderEnvDumps(e);
			e.setSummary("Unable to query the UID of spawned application process "
				+ toString(pid) + ": error parsing 'ps' output");
			e.setSubprocessPid(pid);
			e.setProblemDescriptionHTML(
				"<h2>Unable to use 'ps' to query PID " + toString(pid) + "</h2>"
				"<p>The " PROGRAM_NAME " application server tried"
				" to start the web application. As part of the starting"
				" procedure, " SHORT_PROGRAM_NAME " also tried to query"
				" the system user ID of the web application process"
				" using the operating system's \"ps\" tool. However,"
				" this tool returned output that " SHORT_PROGRAM_NAME
				" could not understand.</p>");
			e.setSolutionDescriptionHTML(
				createSolutionDescriptionForProcessMetricsCollectionError());
			throw e.finalize();
		} catch (const SystemException &originalException) {
			HandshakePerform::loadJourneyStateFromResponseDir(session, pid, stdoutAndErrCapturer);
			session.journey.setStepErrored(SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER);

			SpawnException e(OPERATING_SYSTEM_ERROR, session.journey, session.config);
			addPreloaderEnvDumps(e);
			e.setSummary("Unable to query the UID of spawned application process "
				+ toString(pid) + "; error capturing 'ps' output: "
				+ originalException.what());
			e.setSubprocessPid(pid);
			e.setProblemDescriptionHTML(
				"<h2>Error capturing 'ps' output for PID " + toString(pid) + "</h2>"
				"<p>The " PROGRAM_NAME " application server tried"
				" to start the web application. As part of the starting"
				" procedure, " SHORT_PROGRAM_NAME " also tried to query"
				" the system user ID of the web application process."
				" This is done by using the operating system's \"ps\""
				" tool and by querying operating system APIs and special"
				" files. However, an error was encountered while doing"
				" one of those things.</p>"
				"<p>The error returned by the operating system is as follows:</p>"
				"<pre>" + escapeHTML(originalException.what()) + "</pre>");
			e.setSolutionDescriptionHTML(
				createSolutionDescriptionForProcessMetricsCollectionError());
			throw e.finalize();
		}

		UPDATE_TRACE_POINT();
		if (uid == (uid_t) -1) {
			if (osProcessExists(pid)) {
				HandshakePerform::loadJourneyStateFromResponseDir(session, pid, stdoutAndErrCapturer);
				session.journey.setStepErrored(SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER);

				SpawnException e(INTERNAL_ERROR, session.journey, session.config);
				addPreloaderEnvDumps(e);
				e.setSummary("Unable to query the UID of spawned application process "
					+ toString(pid) + ": 'ps' did not report information"
					" about this process");
				e.setSubprocessPid(pid);
				e.setProblemDescriptionHTML(
					"<h2>'ps' did not return any information about PID " + toString(pid) + "</h2>"
					"<p>The " PROGRAM_NAME " application server tried"
					" to start the web application. As part of the starting"
					" procedure, " SHORT_PROGRAM_NAME " also tried to query"
					" the system user ID of the web application process"
					" using the operating system's \"ps\" tool. However,"
					" this tool did not return any information about"
					" the web application process.</p>");
				e.setSolutionDescriptionHTML(
					createSolutionDescriptionForProcessMetricsCollectionError());
				throw e.finalize();
			} else {
				HandshakePerform::loadJourneyStateFromResponseDir(session, pid, stdoutAndErrCapturer);
				session.journey.setStepErrored(SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER);

				SpawnException e(INTERNAL_ERROR, session.journey, session.config);
				addPreloaderEnvDumps(e);
				e.setSummary("The application process spawned from the preloader"
					" seems to have exited prematurely");
				e.setSubprocessPid(pid);
				e.setStdoutAndErrData(getBackgroundIOCapturerData(stdoutAndErrCapturer));
				e.setProblemDescriptionHTML(
					"<h2>Application process exited prematurely</h2>"
					"<p>The " PROGRAM_NAME " application server tried"
					" to start the web application. As part of the starting"
					" procedure, " SHORT_PROGRAM_NAME " also tried to query"
					" the system user ID of the web application process"
					" using the operating system's \"ps\" tool. However,"
					" this tool did not return any information about"
					" the web application process.</p>");
				e.setSolutionDescriptionHTML(
					createSolutionDescriptionForProcessMetricsCollectionError());
				throw e.finalize();
			}
		} else {
			return uid;
		}
	}

	static string createSolutionDescriptionForProcessMetricsCollectionError() {
		const char *path = getenv("PATH");
		if (path == NULL || path[0] == '\0') {
			path = "(empty)";
		}
		return "<div class=\"multiple-solutions\">"

			"<h3>Check whether the \"ps\" tool is installed and accessible by "
			SHORT_PROGRAM_NAME "</h3>"
			"<p>Maybe \"ps\" is not installed. Or maybe it is installed, but "
			SHORT_PROGRAM_NAME " cannot find it inside its PATH. Or"
			" maybe filesystem permissions disallow " SHORT_PROGRAM_NAME
			" from accessing \"ps\". Please check all these factors and"
			" fix them if necessary.</p>"
			"<p>" SHORT_PROGRAM_NAME "'s PATH is:</p>"
			"<pre>" + escapeHTML(path) + "</pre>"

			"<h3>Check whether the server is low on resources</h3>"
			"<p>Maybe the server is currently low on resources. This would"
			" cause the \"ps\" tool to encounter errors. Please study the"
			" <em>error message</em> and the <em>diagnostics reports</em> to"
			" verify whether this is the case. Key things to check for:</p>"
			"<ul>"
			"<li>Excessive CPU usage</li>"
			"<li>Memory and swap</li>"
			"<li>Ulimits</li>"
			"</ul>"
			"<p>If the server is indeed low on resources, find a way to"
			" free up some resources.</p>"

			"<h3>Check whether /proc is mounted</h3>"
			"<p>On many operating systems including Linux and FreeBSD, \"ps\""
			" only works if /proc is mounted. Please check this.</p>"

			"<h3>Still no luck?</h3>"
			"<p>Please try troubleshooting the problem by studying the"
			" <em>diagnostics</em> reports.</p>"

			"</div>";
	}

	static void adjustTimeout(MonotonicTimeUsec startTime, unsigned long long *timeout) {
		boost::this_thread::disable_interruption di;
		boost::this_thread::disable_syscall_interruption dsi;
		MonotonicTimeUsec now = SystemTime::getMonotonicUsec();
		assert(now >= startTime);
		MonotonicTimeUsec diff = now - startTime;
		if (*timeout >= diff) {
			*timeout -= diff;
		} else {
			*timeout = 0;
		}
	}

	static void doClosedir(DIR *dir) {
		closedir(dir);
	}

	static string findPreloaderCommandSocketAddress(const HandshakeSession &session) {
		const vector<Result::Socket> &sockets = session.result.sockets;
		vector<Result::Socket>::const_iterator it, end = sockets.end();
		for (it = sockets.begin(); it != end; it++) {
			if (it->protocol == "preloader") {
				return it->address;
			}
		}
		return string();
	}

	static StringKeyTable<string> loadAnnotationsFromEnvDumpDir(const string &envDumpDir,
		int envDumpAnnotationsDirFd)
	{
		string path = envDumpDir + "/annotations";
		DIR *dir = opendir(path.c_str());
		if (dir == NULL) {
			return StringKeyTable<string>();
		}

		ScopeGuard guard(boost::bind(doClosedir, dir));
		StringKeyTable<string> result;
		struct dirent *ent;
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] != '.') {
				result.insert(ent->d_name, strip(safeReadFile(envDumpAnnotationsDirFd,
					ent->d_name, SPAWNINGKIT_MAX_SUBPROCESS_ENVDUMP_SIZE).first), true);
			}
		}

		result.compact();

		return result;
	}

	void addPreloaderEnvDumps(SpawnException &e) const {
		e.setPreloaderPid(pid);
		e.setPreloaderEnvvars(preloaderEnvvars);
		e.setPreloaderUserInfo(preloaderUserInfo);
		e.setPreloaderUlimits(preloaderUlimits);

		if (e.getSubprocessEnvvars().empty()) {
			e.setSubprocessEnvvars(preloaderEnvvars);
		}
		if (e.getSubprocessUserInfo().empty()) {
			e.setSubprocessUserInfo(preloaderUserInfo);
		}
		if (e.getSubprocessUlimits().empty()) {
			e.setSubprocessUlimits(preloaderUlimits);
		}

		StringKeyTable<string>::ConstIterator it(preloaderAnnotations);
		while (*it != NULL) {
			e.setAnnotation(it.getKey(), it.getValue(), false);
			it.next();
		}
	}

public:
	SmartSpawner(Context *context,
		const vector<string> &preloaderCommand,
		const AppPoolOptions &_options)
		: Spawner(context),
		  preloaderCommandString(createCommandString(preloaderCommand))
	{
		if (preloaderCommand.size() < 2) {
			throw ArgumentException("preloaderCommand must have at least 2 elements");
		}

		options    = _options.copyAndPersist();
		pid        = -1;
		m_lastUsed = SystemTime::getUsec();
	}

	virtual ~SmartSpawner() {
		boost::lock_guard<boost::mutex> l(syncher);
		stopPreloader();
	}

	virtual Result spawn(const AppPoolOptions &options) {
		TRACE_POINT();
		P_ASSERT_EQ(options.appType, this->options.appType);
		P_ASSERT_EQ(options.appRoot, this->options.appRoot);

		P_DEBUG("Spawning new process: appRoot=" << options.appRoot);
		possiblyRaiseInternalError(options);

		{
			boost::lock_guard<boost::mutex> l(simpleFieldSyncher);
			m_lastUsed = SystemTime::getUsec();
		}
		UPDATE_TRACE_POINT();
		boost::lock_guard<boost::mutex> l(syncher);
		if (!preloaderStarted()) {
			UPDATE_TRACE_POINT();
			startPreloader();
		}

		UPDATE_TRACE_POINT();
		Config config;
		Json::Value extraArgs;
		try {
			setConfigFromAppPoolOptions(&config, extraArgs, options);
		} catch (const std::exception &originalException) {
			Journey journey(SPAWN_THROUGH_PRELOADER, true);
			journey.setStepErrored(SPAWNING_KIT_PREPARATION, true);
			SpawnException e(originalException, journey, &config);
			addPreloaderEnvDumps(e);
			throw e.finalize();
		}

		UPDATE_TRACE_POINT();
		HandshakeSession session(*context, config, SPAWN_THROUGH_PRELOADER);
		session.journey.setStepInProgress(SPAWNING_KIT_PREPARATION);
		JourneyStep stepToMarkAsErrored = SPAWNING_KIT_PREPARATION;

		try {
			UPDATE_TRACE_POINT();
			HandshakePrepare prepare(session, extraArgs);
			prepare.execute();
			createStdChannelFifos(session);
			prepare.finalize();
			session.journey.setStepPerformed(SPAWNING_KIT_PREPARATION, true);

			UPDATE_TRACE_POINT();
			ForkResult forkResult = invokeForkCommand(session, stepToMarkAsErrored);

			UPDATE_TRACE_POINT();
			ScopeGuard guard(boost::bind(nonInterruptableKillAndWaitpid, forkResult.pid));
			P_DEBUG("Process forked for appRoot=" << options.appRoot << ": PID " << forkResult.pid);

			UPDATE_TRACE_POINT();
			session.journey.setStepPerformed(SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER);
			session.journey.setStepInProgress(PRELOADER_PREPARATION);
			session.journey.setStepInProgress(SPAWNING_KIT_HANDSHAKE_PERFORM);
			stepToMarkAsErrored = SPAWNING_KIT_HANDSHAKE_PERFORM;
			HandshakePerform(session, forkResult.pid, forkResult.stdinFd,
				forkResult.stdoutAndErrFd, forkResult.alreadyReadStdoutAndErrData).
				execute();
			guard.clear();
			session.journey.setStepPerformed(SPAWNING_KIT_HANDSHAKE_PERFORM);
			P_DEBUG("Process spawning done: appRoot=" << options.appRoot <<
				", pid=" << forkResult.pid);
			return session.result;
		} catch (SpawnException &e) {
			addPreloaderEnvDumps(e);
			throw e;
		} catch (const std::exception &originalException) {
			session.journey.setStepErrored(stepToMarkAsErrored, true);
			SpawnException e(originalException, session.journey,
				&config);
			addPreloaderEnvDumps(e);
			throw e.finalize();
		}
	}

	virtual bool cleanable() const {
		return true;
	}

	virtual void cleanup() {
		TRACE_POINT();
		{
			boost::lock_guard<boost::mutex> l(simpleFieldSyncher);
			m_lastUsed = SystemTime::getUsec();
		}
		boost::lock_guard<boost::mutex> lock(syncher);
		stopPreloader();
	}

	virtual unsigned long long lastUsed() const {
		boost::lock_guard<boost::mutex> lock(simpleFieldSyncher);
		return m_lastUsed;
	}

	pid_t getPreloaderPid() const {
		boost::lock_guard<boost::mutex> lock(simpleFieldSyncher);
		return pid;
	}
};


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_SMART_SPAWNER_H_ */
