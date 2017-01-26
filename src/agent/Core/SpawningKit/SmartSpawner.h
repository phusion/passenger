/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2015 Phusion Holding B.V.
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

#include <Core/SpawningKit/Spawner.h>
#include <Core/SpawningKit/PipeWatcher.h>
#include <Constants.h>
#include <LveLoggingDecorator.h>

#include <adhoc_lve.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;
using namespace boost;
using namespace oxt;


class SmartSpawner: public Spawner, public boost::enable_shared_from_this<SmartSpawner> {
private:
	/**
	 * Structure containing arguments and working state for negotiating
	 * the preloader startup protocol.
	 */
	struct StartupDetails {
		/****** Arguments ******/
		pid_t pid;
		FileDescriptor adminSocket;
		BufferedIO io;
		BackgroundIOCapturerPtr stderrCapturer;
		DebugDirPtr debugDir;
		const Options *options;

		/****** Working state ******/
		unsigned long long timeout;

		StartupDetails() {
			options = NULL;
			timeout = 0;
		}
	};

	const vector<string> preloaderCommand;
	map<string, string> preloaderAnnotations;
	Options options;

	// Protects m_lastUsed and pid.
	mutable boost::mutex simpleFieldSyncher;
	// Protects everything else.
	mutable boost::mutex syncher;

	// Preloader information.
	pid_t pid;
	FileDescriptor adminSocket;
	string socketAddress;
	unsigned long long m_lastUsed;
	// Upon starting the preloader, its preparation info is stored here
	// for future reference.
	SpawnPreparationInfo preparation;

	string getPreloaderCommandString() const {
		string result;
		unsigned int i;

		for (i = 0; i < preloaderCommand.size(); i++) {
			if (i != 0) {
				result.append(1, '\0');
			}
			result.append(preloaderCommand[i]);
		}
		return result;
	}

	vector<string> createRealPreloaderCommand(const Options &options,
		shared_array<const char *> &args)
	{
		string agentFilename = config->resourceLocator->findSupportBinary(AGENT_EXE);
		vector<string> command;

		if (shouldLoadShellEnvvars(options, preparation)) {
			command.push_back(preparation.userSwitching.shell);
			command.push_back(preparation.userSwitching.shell);
			command.push_back("-lc");
			command.push_back("exec \"$@\"");
			command.push_back("SpawnPreparerShell");
		} else {
			command.push_back(agentFilename);
		}
		command.push_back(agentFilename);
		command.push_back("spawn-preparer");
		command.push_back(preparation.appRoot);
		command.push_back(serializeEnvvarsFromPoolOptions(options));
		command.push_back(preloaderCommand[0]);
		// Note: do not try to set a process title here.
		// https://code.google.com/p/phusion-passenger/issues/detail?id=855
		command.push_back(preloaderCommand[0]);
		for (unsigned int i = 1; i < preloaderCommand.size(); i++) {
			command.push_back(preloaderCommand[i]);
		}

		createCommandArgs(command, args);
		return command;
	}

	void throwPreloaderSpawnException(const string &msg,
		SpawnException::ErrorKind errorKind,
		StartupDetails &details)
	{
		throwPreloaderSpawnException(msg, errorKind, details.stderrCapturer,
			*details.options, details.debugDir);
	}

	void throwPreloaderSpawnException(const string &msg,
		SpawnException::ErrorKind errorKind,
		BackgroundIOCapturerPtr &stderrCapturer,
		const Options &options,
		const DebugDirPtr &debugDir)
	{
		TRACE_POINT();
		// Stop the stderr capturing thread and get the captured stderr
		// output so far.
		string stderrOutput;
		if (stderrCapturer != NULL) {
			stderrOutput = stderrCapturer->stop();
		}

		// If the exception wasn't due to a timeout, try to capture the
		// remaining stderr output for at most 2 seconds.
		if (errorKind != SpawnException::PRELOADER_STARTUP_TIMEOUT
		 && errorKind != SpawnException::APP_STARTUP_TIMEOUT
		 && stderrCapturer != NULL)
		{
			bool done = false;
			unsigned long long timeout = 2000;
			while (!done) {
				char buf[1024 * 32];
				unsigned int ret;

				try {
					ret = readExact(stderrCapturer->getFd(), buf,
						sizeof(buf), &timeout);
					if (ret == 0) {
						done = true;
					} else {
						stderrOutput.append(buf, ret);
					}
				} catch (const SystemException &e) {
					P_WARN("Stderr I/O capture error: " << e.what());
					done = true;
				} catch (const TimeoutException &) {
					done = true;
				}
			}
		}
		stderrCapturer.reset();

		// Now throw SpawnException with the captured stderr output
		// as error response.
		SpawnException e(msg,
			createErrorPageFromStderrOutput(msg, errorKind, stderrOutput),
			true,
			errorKind);
		e.setPreloaderCommand(getPreloaderCommandString());
		annotatePreloaderException(e, debugDir);
		throwSpawnException(e, options);
	}

	void annotatePreloaderException(SpawnException &e, const DebugDirPtr &debugDir) {
		if (debugDir != NULL) {
			e.addAnnotations(debugDir->readAll());
		}
	}

	bool preloaderStarted() const {
		return pid != -1;
	}

	void startPreloader() {
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		assert(!preloaderStarted());
		P_DEBUG("Spawning new preloader: appRoot=" << options.appRoot);
		checkChrootDirectories(options);

		shared_array<const char *> args;
		preparation = prepareSpawn(options);
		vector<string> command = createRealPreloaderCommand(options, args);
		SocketPair adminSocket = createUnixSocketPair(__FILE__, __LINE__);
		Pipe errorPipe = createPipe(__FILE__, __LINE__);
		DebugDirPtr debugDir = boost::make_shared<DebugDir>(preparation.userSwitching.uid,
			preparation.userSwitching.gid);

		adhoc_lve::LveEnter scopedLveEnter(LveLoggingDecorator::lveInitOnce(),
		                                   preparation.userSwitching.uid,
		                                   options.lveMinUid,
		                                   LveLoggingDecorator::lveExitCallback);
		LveLoggingDecorator::logLveEnter(scopedLveEnter,
		                                 preparation.userSwitching.uid,
		                                 options.lveMinUid);
		pid_t pid = syscalls::fork();
		if (pid == 0) {
			setenv("PASSENGER_DEBUG_DIR", debugDir->getPath().c_str(), 1);
			purgeStdio(stdout);
			purgeStdio(stderr);
			resetSignalHandlersAndMask();
			disableMallocDebugging();
			int adminSocketCopy = dup2(adminSocket.first, 3);
			int errorPipeCopy = dup2(errorPipe.second, 4);
			dup2(adminSocketCopy, 0);
			dup2(adminSocketCopy, 1);
			dup2(errorPipeCopy, 2);
			closeAllFileDescriptors(2);
			setChroot(preparation);
			setUlimits(options);
			switchUser(preparation);
			setWorkingDirectory(preparation);
			execvp(command[0].c_str(), (char * const *) args.get());

			int e = errno;
			printf("!> Error\n");
			printf("!> \n");
			printf("Cannot execute \"%s\": %s (errno=%d)\n", command[0].c_str(),
				strerror(e), e);
			fprintf(stderr, "Cannot execute \"%s\": %s (errno=%d)\n",
				command[0].c_str(), strerror(e), e);
			fflush(stdout);
			fflush(stderr);
			_exit(1);

		} else if (pid == -1) {
			int e = errno;
			throw SystemException("Cannot fork a new process", e);

		} else {
			scopedLveEnter.exit();

			UPDATE_TRACE_POINT();
			P_LOG_FILE_DESCRIPTOR_PURPOSE(adminSocket.first,
				"Preloader " << pid << " (" << options.appRoot << ") adminSocket[0]");
			P_LOG_FILE_DESCRIPTOR_PURPOSE(adminSocket.second,
				"Preloader " << pid << " (" << options.appRoot << ") adminSocket[1]");
			P_LOG_FILE_DESCRIPTOR_PURPOSE(errorPipe.first,
				"Preloader " << pid << " (" << options.appRoot << ") errorPipe[0]");
			P_LOG_FILE_DESCRIPTOR_PURPOSE(errorPipe.second,
				"Preloader " << pid << " (" << options.appRoot << ") errorPipe[1]");

			UPDATE_TRACE_POINT();
			ScopeGuard guard(boost::bind(nonInterruptableKillAndWaitpid, pid));
			P_DEBUG("Preloader process forked for appRoot=" << options.appRoot << ": PID " << pid);
			adminSocket.first.close();
			errorPipe.second.close();

			StartupDetails details;
			details.pid = pid;
			details.adminSocket = adminSocket.second;
			details.io = BufferedIO(adminSocket.second);
			details.stderrCapturer =
				boost::make_shared<BackgroundIOCapturer>(
					errorPipe.first,
					pid,
					// The cast works around a compilation problem in Clang.
					(const char *) "stderr");
			details.stderrCapturer->start();
			details.debugDir = debugDir;
			details.options = &options;
			details.timeout = options.startTimeout * 1000;

			{
				this_thread::restore_interruption ri(di);
				this_thread::restore_syscall_interruption rsi(dsi);
				socketAddress = negotiatePreloaderStartup(details);
			}
			this->adminSocket = adminSocket.second;
			{
				boost::lock_guard<boost::mutex> l(simpleFieldSyncher);
				this->pid = pid;
			}

			PipeWatcherPtr watcher;

			watcher = boost::make_shared<PipeWatcher>(config,
				adminSocket.second, "stdout", pid);
			watcher->initialize();
			watcher->start();

			watcher = boost::make_shared<PipeWatcher>(config,
				errorPipe.first, "stderr", pid);
			watcher->initialize();
			watcher->start();

			preloaderAnnotations = debugDir->readAll();
			P_INFO("Preloader for " << options.appRoot <<
				" started on PID " << pid <<
				", listening on " << socketAddress);
			guard.clear();
		}
	}

	void stopPreloader() {
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;

		if (!preloaderStarted()) {
			return;
		}
		syscalls::shutdown(adminSocket, SHUT_WR);
		if (timedWaitpid(pid, NULL, 5000) == 0) {
			P_TRACE(2, "Spawn server did not exit in time, killing it...");
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
		}
		socketAddress.clear();
		preparation = SpawnPreparationInfo();
	}

	void sendStartupRequest(StartupDetails &details) {
		TRACE_POINT();
		try {
			const size_t UNIX_PATH_MAX = sizeof(((struct sockaddr_un *) 0)->sun_path);
			string data = "You have control 1.0\n"
				"passenger_root: " + config->resourceLocator->getInstallSpec() + "\n"
				"ruby_libdir: " + config->resourceLocator->getRubyLibDir() + "\n"
				"passenger_version: " PASSENGER_VERSION "\n"
				"UNIX_PATH_MAX: " + toString(UNIX_PATH_MAX) + "\n";
			if (!details.options->apiKey.empty()) {
				data.append("connect_password: " + details.options->apiKey + "\n");
			}
			if (!config->instanceDir.empty()) {
				data.append("instance_dir: " + config->instanceDir + "\n");
				data.append("socket_dir: " + config->instanceDir + "/apps.s\n");
			}

			vector<string> args;
			vector<string>::const_iterator it, end;
			details.options->toVector(args, *config->resourceLocator, Options::SPAWN_OPTIONS);
			for (it = args.begin(); it != args.end(); it++) {
				const string &key = *it;
				it++;
				const string &value = *it;
				data.append(key + ": " + value + "\n");
			}

			vector<StaticString> lines;
			split(data, '\n', lines);
			foreach (const StaticString line, lines) {
				P_DEBUG("[App " << details.pid << " stdin >>] " << line);
			}
			writeExact(details.adminSocket, data, &details.timeout);
			writeExact(details.adminSocket, "\n", &details.timeout);
		} catch (const SystemException &e) {
			if (e.code() == EPIPE) {
				/* Ignore this. Process might have written an
				 * error response before reading the arguments,
				 * in which case we'll want to show that instead.
				 */
			} else {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. There was an I/O error while "
					"sending the startup request message to it: " +
					e.sys(),
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			}
		} catch (const TimeoutException &) {
			throwPreloaderSpawnException("An error occurred while starting up the "
				"preloader: it did not read the startup request message in time.",
				SpawnException::PRELOADER_STARTUP_TIMEOUT,
				details);
		}
	}

	string handleStartupResponse(StartupDetails &details) {
		TRACE_POINT();
		string socketAddress;

		while (true) {
			string line;

			try {
				line = readMessageLine(details);
			} catch (const SystemException &e) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			} catch (const TimeoutException &) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader: it did not write a startup response in time.",
					SpawnException::PRELOADER_STARTUP_TIMEOUT,
					details);
			}

			if (line.empty()) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It unexpected closed the connection while "
					"sending its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			} else if (line[line.size() - 1] != '\n') {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a line without a newline character "
					"in its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			} else if (line == "\n") {
				break;
			}

			string::size_type pos = line.find(": ");
			if (pos == string::npos) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a startup response line without "
					"separator.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			}

			string key = line.substr(0, pos);
			string value = line.substr(pos + 2, line.size() - pos - 3);
			if (key == "socket") {
				// TODO: validate socket address here
				socketAddress = fixupSocketAddress(options, value);
			} else {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent an unknown startup response line "
					"called '" + key + "'.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			}
		}

		if (socketAddress.empty()) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It did not report a socket address in its "
				"startup response.",
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
				details);
		}

		return socketAddress;
	}

	void handleErrorResponse(StartupDetails &details) {
		TRACE_POINT();
		map<string, string> attributes;

		while (true) {
			string line;

			try {
				line = readMessageLine(details);
			} catch (const SystemException &e) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			} catch (const TimeoutException &) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader: it did not write a startup response in time.",
					SpawnException::PRELOADER_STARTUP_TIMEOUT,
					details);
			}

			if (line.empty()) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It unexpected closed the connection while "
					"sending its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			} else if (line[line.size() - 1] != '\n') {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a line without a newline character "
					"in its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			} else if (line == "\n") {
				break;
			}

			string::size_type pos = line.find(": ");
			if (pos == string::npos) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a startup response line without "
					"separator.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			}

			string key = line.substr(0, pos);
			string value = line.substr(pos + 2, line.size() - pos - 3);
			attributes[key] = value;
		}

		try {
			string message = details.io.readAll(&details.timeout);
			SpawnException e("An error occurred while starting up the preloader.",
				message,
				attributes["html"] == "true",
				SpawnException::PRELOADER_STARTUP_EXPLAINABLE_ERROR);
			e.setPreloaderCommand(getPreloaderCommandString());
			annotatePreloaderException(e, details.debugDir);
			throwSpawnException(e, *details.options);
		} catch (const SystemException &e) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It tried to report an error message, but "
				"an I/O error occurred while reading this error message: " +
				e.sys(),
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
				details);
		} catch (const TimeoutException &) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It tried to report an error message, but "
				"it took too much time doing that.",
				SpawnException::PRELOADER_STARTUP_TIMEOUT,
				details);
		}
	}

	void handleInvalidResponseType(StartupDetails &details, const string &line) {
		if (line.empty()) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It exited before signalling successful "
				"startup back to " PROGRAM_NAME ".",
				SpawnException::PRELOADER_STARTUP_ERROR,
				details);
		} else {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It sent an unknown response type \"" +
				cEscapeString(line) + "\".",
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
				details);
		}
	}

	string negotiatePreloaderStartup(StartupDetails &details) {
		TRACE_POINT();
		string result;
		try {
			result = readMessageLine(details);
		} catch (const SystemException &e) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. There was an I/O error while reading its "
				"handshake message: " + e.sys(),
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
				details);
		} catch (const TimeoutException &) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader: it did not write a handshake message in time.",
				SpawnException::PRELOADER_STARTUP_TIMEOUT,
				details);
		}

		if (result == "I have control 1.0\n") {
			UPDATE_TRACE_POINT();
			sendStartupRequest(details);
			try {
				result = readMessageLine(details);
			} catch (const SystemException &e) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					details);
			} catch (const TimeoutException &) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader: it did not write a startup response in time.",
					SpawnException::PRELOADER_STARTUP_TIMEOUT,
					details);
			}
			if (result == "Ready\n") {
				return handleStartupResponse(details);
			} else if (result == "Error\n") {
				handleErrorResponse(details);
			} else {
				handleInvalidResponseType(details, result);
			}
		} else {
			UPDATE_TRACE_POINT();
			if (result == "Error\n") {
				handleErrorResponse(details);
			} else {
				handleInvalidResponseType(details, result);
			}
		}

		// Never reached, shut up compiler warning.
		abort();
		return "";
	}

	NegotiationDetails sendSpawnCommandAndGetNegotiationDetails(const Options &options) {
		TRACE_POINT();
		NegotiationDetails details;

		details.preparation = &preparation;
		details.options = &options;

		try {
			sendSpawnCommand(details);
		} catch (const SystemException &e) {
			sendSpawnCommandAgain(e, details);
		} catch (const IOException &e) {
			sendSpawnCommandAgain(e, details);
		} catch (const SpawnException &e) {
			sendSpawnCommandAgain(e, details);
		}

		return details;
	}

	void sendSpawnCommand(NegotiationDetails &details) {
		TRACE_POINT();
		const Options &options = *details.options;
		FileDescriptor fd;

		try {
			fd.assign(connectToServer(socketAddress, __FILE__, __LINE__), NULL, 0);
		} catch (const SystemException &e) {
			BackgroundIOCapturerPtr stderrCapturer;
			throwPreloaderSpawnException("An error occurred while starting "
				"the application. Unable to connect to the preloader's "
				"socket: " + string(e.what()),
				SpawnException::APP_STARTUP_PROTOCOL_ERROR,
				stderrCapturer,
				options,
				DebugDirPtr());
		}
		P_LOG_FILE_DESCRIPTOR_PURPOSE(fd, "Preloader " << pid
			<< " (" << options.appRoot << ") connection");

		UPDATE_TRACE_POINT();
		BufferedIO io(fd);
		unsigned long long timeout = options.startTimeout * 1000;
		string result;
		vector<string> args;
		vector<string>::const_iterator it;

		writeExact(fd, "spawn\n", &timeout);
		options.toVector(args, *config->resourceLocator, Options::SPAWN_OPTIONS);
		for (it = args.begin(); it != args.end(); it++) {
			const string &key = *it;
			it++;
			const string &value = *it;
			writeExact(fd, key + ": " + value + "\n", &timeout);
		}
		writeExact(fd, "\n", &timeout);

		result = io.readLine(1024 * 8, &timeout);
		if (result == "OK\n") {
			UPDATE_TRACE_POINT();
			pid_t spawnedPid;

			spawnedPid = atoi(io.readLine(1024 * 8, &timeout).c_str());
			if (spawnedPid <= 0) {
				BackgroundIOCapturerPtr stderrCapturer;
				throwPreloaderSpawnException("An error occurred while starting "
					"the web application. Its preloader responded to the "
					"'spawn' command with an invalid PID: '" +
					toString(spawnedPid) + "'",
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					options,
					DebugDirPtr());
			}
			// TODO: we really should be checking UID.
			// FIXME: for Passenger 4 we *must* check the UID otherwise this is a gaping security hole.
			if (getsid(spawnedPid) != getsid(pid)) {
				BackgroundIOCapturerPtr stderrCapturer;
				throwPreloaderSpawnException("An error occurred while starting "
					"the web application. Its preloader responded to the "
					"'spawn' command with a PID that doesn't belong to "
					"the same session: '" + toString(spawnedPid) + "'",
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					options,
					DebugDirPtr());
			}

			P_LOG_FILE_DESCRIPTOR_PURPOSE(fd, "App " << spawnedPid
				<< " (" << options.appRoot << ") adminSocket[1]");
			details.pid = spawnedPid;
			details.adminSocket = fd;
			details.io = io;

		} else if (result == "Error\n") {
			UPDATE_TRACE_POINT();
			details.io = io;
			details.timeout = timeout;
			handleSpawnErrorResponse(details);

		} else {
			UPDATE_TRACE_POINT();
			handleInvalidSpawnResponseType(result, details);
		}
	}

	template<typename Exception>
	void sendSpawnCommandAgain(const Exception &e, NegotiationDetails &details) {
		TRACE_POINT();
		P_WARN("An error occurred while spawning a process: " << e.what());
		P_WARN("The application preloader seems to have crashed, restarting it and trying again...");
		stopPreloader();
		startPreloader();
		ScopeGuard guard(boost::bind(&SmartSpawner::stopPreloader, this));
		sendSpawnCommand(details);
		guard.clear();
	}

protected:
	virtual void annotateAppSpawnException(SpawnException &e, NegotiationDetails &details) {
		Spawner::annotateAppSpawnException(e, details);
		e.addAnnotations(preloaderAnnotations);
	}

public:
	SmartSpawner(const vector<string> &_preloaderCommand,
		const Options &_options,
		const ConfigPtr &_config)
		: Spawner(_config),
		  preloaderCommand(_preloaderCommand)
	{
		if (preloaderCommand.size() < 2) {
			throw ArgumentException("preloaderCommand must have at least 2 elements");
		}

		options    = _options.copyAndPersist().detachFromUnionStationTransaction();
		pid        = -1;
		m_lastUsed = SystemTime::getUsec();
	}

	virtual ~SmartSpawner() {
		boost::lock_guard<boost::mutex> l(syncher);
		stopPreloader();
	}

	virtual Result spawn(const Options &options) {
		TRACE_POINT();
		assert(options.appType == this->options.appType);
		assert(options.appRoot == this->options.appRoot);

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
		NegotiationDetails details = sendSpawnCommandAndGetNegotiationDetails(options);
		Result result = negotiateSpawn(details);
		P_DEBUG("Process spawning done: appRoot=" << options.appRoot <<
			", pid=" << result["pid"].asInt());
		return result;
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
