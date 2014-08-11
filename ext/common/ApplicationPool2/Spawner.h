/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2014 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
#ifndef _PASSENGER_APPLICATION_POOL_SPAWNER_H_
#define _PASSENGER_APPLICATION_POOL_SPAWNER_H_

/*
 * This file implements application spawning support. Several classes
 * are provided which all implement the Spawner interface. The spawn()
 * method spawns an application process based on the given options
 * and returns a Process object which contains information about the
 * spawned process.
 *
 * The DirectSpawner class spawns application processes directly.
 *
 * The SmartSpawner class spawns application processes through a
 * preloader process. The preloader process loads the application
 * code into its address space and then listens on a socket for spawn
 * commands. Upon receiving a spawn command, it will fork() itself.
 * This makes spawning multiple application processes much faster.
 * Note that a single SmartSpawner instance is only usable for a
 * single application.
 *
 * DummySpawner doesn't do anything. It returns dummy Process objects.
 *
 * DirectSpawner, SmartSpawner and DummySpawner all implement the Spawner interface.
 *
 * SpawnerFactory is a convenience class which takes an Options objects
 * and figures out, based on options.spawnMethod, whether to create
 * a DirectSpawner or a SmartSpawner. In case of the smart spawning
 * method, SpawnerFactory also automatically figures out which preloader
 * to use based on options.appType.
 */

#include <string>
#include <map>
#include <vector>
#include <utility>
#include <algorithm>
#include <boost/make_shared.hpp>
#include <boost/shared_array.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Options.h>
#include <ApplicationPool2/PipeWatcher.h>
#include <FileDescriptor.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <ServerInstanceDir.h>
#include <Utils.h>
#include <Utils/BufferedIO.h>
#include <Utils/ScopeGuard.h>
#include <Utils/Timer.h>
#include <Utils/IOUtils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/Base64.h>
#include <Utils/ProcessMetricsCollector.h>

namespace tut {
	struct ApplicationPool2_DirectSpawnerTest;
	struct ApplicationPool2_SmartSpawnerTest;
}

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


class Spawner {
protected:
	friend struct tut::ApplicationPool2_DirectSpawnerTest;
	friend struct tut::ApplicationPool2_SmartSpawnerTest;

	/**
	 * Given a file descriptor, captures its output in a background thread
	 * and also forwards it immediately to a target file descriptor.
	 * Call stop() to stop the background thread and to obtain the captured
	 * output so far.
	 */
	class BackgroundIOCapturer {
	private:
		FileDescriptor fd;
		pid_t pid;
		const char *channelName;
		boost::mutex dataSyncher;
		string data;
		oxt::thread *thr;

		void capture() {
			TRACE_POINT();
			while (!this_thread::interruption_requested()) {
				char buf[1024 * 8];
				ssize_t ret;

				UPDATE_TRACE_POINT();
				ret = syscalls::read(fd, buf, sizeof(buf));
				int e = errno;
				this_thread::disable_syscall_interruption dsi;
				if (ret == 0) {
					break;
				} else if (ret == -1) {
					if (e != EAGAIN && e != EWOULDBLOCK) {
						P_WARN("Background I/O capturer error: " <<
							strerror(e) << " (errno=" << e << ")");
						break;
					}
				} else {
					{
						boost::lock_guard<boost::mutex> l(dataSyncher);
						data.append(buf, ret);
					}
					UPDATE_TRACE_POINT();
					if (ret == 1 && buf[0] == '\n') {
						printAppOutput(pid, channelName, "", 0);
					} else {
						vector<StaticString> lines;
						if (ret > 0 && buf[ret - 1] == '\n') {
							ret--;
						}
						split(StaticString(buf, ret), '\n', lines);
						foreach (const StaticString line, lines) {
							printAppOutput(pid, channelName, line.data(), line.size());
						}
					}
				}
			}
		}

	public:
		BackgroundIOCapturer(const FileDescriptor &_fd, pid_t _pid, const char *_channelName)
			: fd(_fd),
			  pid(_pid),
			  channelName(_channelName),
			  thr(NULL)
			{ }

		~BackgroundIOCapturer() {
			TRACE_POINT();
			if (thr != NULL) {
				this_thread::disable_interruption di;
				this_thread::disable_syscall_interruption dsi;
				thr->interrupt_and_join();
				delete thr;
				thr = NULL;
			}
		}

		const FileDescriptor &getFd() const {
			return fd;
		}

		void start() {
			assert(thr == NULL);
			thr = new oxt::thread(boost::bind(&BackgroundIOCapturer::capture, this),
				"Background I/O capturer", 64 * 1024);
		}

		string stop() {
			TRACE_POINT();
			assert(thr != NULL);
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			thr->interrupt_and_join();
			delete thr;
			thr = NULL;
			boost::lock_guard<boost::mutex> l(dataSyncher);
			return data;
		}

		void appendToBuffer(const StaticString &dataToAdd) {
			TRACE_POINT();
			boost::lock_guard<boost::mutex> l(dataSyncher);
			data.append(dataToAdd.data(), dataToAdd.size());
		}
	};

	typedef boost::shared_ptr<BackgroundIOCapturer> BackgroundIOCapturerPtr;

	/**
	 * A temporary directory for spawned child processes to write
	 * debugging information to. It is removed after spawning has
	 * determined to be successful or failed.
	 */
	class DebugDir {
	private:
		string path;

		static void doClosedir(DIR *dir) {
			closedir(dir);
		}

	public:
		DebugDir(uid_t uid, gid_t gid) {
			char buf[PATH_MAX] = "/tmp/passenger.spawn-debug.XXXXXXXXXX";
			const char *result = mkdtemp(buf);
			if (result == NULL) {
				int e = errno;
				throw SystemException("Cannot create a temporary directory "
					"in the format of '/tmp/passenger-spawn-debug.XXX'", e);
			} else {
				path = result;
				this_thread::disable_interruption di;
				this_thread::disable_syscall_interruption dsi;
				syscalls::chown(result, uid, gid);
			}
		}

		~DebugDir() {
			removeDirTree(path);
		}

		const string &getPath() const {
			return path;
		}

		map<string, string> readAll() {
			map<string, string> result;
			DIR *dir = opendir(path.c_str());
			ScopeGuard guard(boost::bind(doClosedir, dir));
			struct dirent *ent;

			while ((ent = readdir(dir)) != NULL) {
				if (ent->d_name[0] != '.') {
					try {
						result.insert(make_pair<string, string>(
							ent->d_name,
							Passenger::readAll(path + "/" + ent->d_name)));
					} catch (const SystemException &) {
						// Do nothing.
					}
				}
			}
			return result;
		}
	};

	typedef boost::shared_ptr<DebugDir> DebugDirPtr;

	/**
	 * Contains information that will be used after fork()ing but before exec()ing,
	 * such as the intended app root, the UID it should switch to, the
	 * groups it should assume, etc. This structure is allocated before forking
	 * because after forking and before exec() it may not be safe to allocate memory.
	 */
	struct SpawnPreparationInfo {
		// General

		/** Absolute application root path. */
		string appRoot;
		/** Absolute pre-exec chroot path. If no chroot is configured, then this is "/". */
		string chrootDir;
		/** Absolute application root path inside the chroot. If no chroot is
		 * configured then this is is equal to appRoot. */
		string appRootInsideChroot;
		/** A list of all parent directories of the appRoot, as well as appRoot itself.
		 * The pre-exec chroot directory is included, and this list goes no futher than that.
		 * For example if appRoot is /var/jail/foo/bar/baz and the chroot is /var/jail,
		 * then this list contains:
		 *   /var/jail/foo
		 *   /var/jail/foo/bar
		 *   /var/jail/foo/bar/baz
		 */
		vector<string> appRootPaths;
		/** Same as appRootPaths, but without the chroot component. For example if
		 * appRoot is /var/jail/foo/bar/baz and the chroot is /var/jail, then this list
		 * contains:
		 *   /foo
		 *   /foo/bar
		 *   /foo/bar/baz
		 */
		vector<string> appRootPathsInsideChroot;

		// User switching
		bool switchUser;
		string username;
		string groupname;
		string home;
		string shell;
		uid_t uid;
		gid_t gid;
		int ngroups;
		shared_array<gid_t> gidset;

		// Other information
		string codeRevision;
	};

	/**
	 * Structure containing arguments and working state for negotiating
	 * the spawning protocol.
	 */
	struct NegotiationDetails {
		/****** Arguments ******/

		/** The preparation info of the process we're negotiating with. It's used
		 * by security validators to check whether the information sent back by the
		 * process make any sense. */
		SpawnPreparationInfo *preparation;
		/** This object captures the process's stderr while negotiation is in progress.
		 * (Recall that negotiation is performed over the process's stdout while stderr
		 * is used purely for outputting messages.)
		 * If the negotiation protocol fails, then any output captured by this object
		 * will be stored into the resulting SpawnException's error page. */
		BackgroundIOCapturerPtr stderrCapturer;
		/** The PID of the process we're negotiating with. */
		pid_t pid;
		FileDescriptor adminSocket;
		FileDescriptor errorPipe;
		const Options *options;
		DebugDirPtr debugDir;

		/****** Working state ******/
		BufferedIO io;
		string gupid;
		string connectPassword;
		unsigned long long spawnStartTime;
		unsigned long long timeout;

		NegotiationDetails() {
			preparation = NULL;
			pid = 0;
			options = NULL;
			spawnStartTime = 0;
			timeout = 0;
		}
	};


private:
	/**
	 * Appends key + "\0" + value + "\0" to 'output'.
	 */
	static void appendNullTerminatedKeyValue(string &output, const StaticString &key,
		const StaticString &value)
	{
		unsigned int minCapacity = key.size() + value.size() + 2;
		if (output.capacity() < minCapacity) {
			output.reserve(minCapacity + 1024);
		}
		output.append(key.data(), key.size());
		output.append(1, '\0');
		output.append(value.data(), value.size());
		output.append(1, '\0');
	}

	void sendSpawnRequest(NegotiationDetails &details) {
		TRACE_POINT();
		try {
			string data = "You have control 1.0\n"
				"passenger_root: " + config->resourceLocator.getRoot() + "\n"
				"passenger_version: " PASSENGER_VERSION "\n"
				"ruby_libdir: " + config->resourceLocator.getRubyLibDir() + "\n"
				"generation_dir: " + generation->getPath() + "\n"
				"gupid: " + details.gupid + "\n"
				"connect_password: " + details.connectPassword + "\n";

			vector<string> args;
			vector<string>::const_iterator it, end;
			details.options->toVector(args, config->resourceLocator, Options::SPAWN_OPTIONS);
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
				throw;
			}
		}
	}

	ProcessPtr handleSpawnResponse(NegotiationDetails &details) {
		TRACE_POINT();
		SocketListPtr sockets = boost::make_shared<SocketList>();
		while (true) {
			string line;

			try {
				line = readMessageLine(details);
			} catch (const SystemException &e) {
				throwAppSpawnException("An error occurred while starting the "
					"web application. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					details);
			} catch (const TimeoutException &) {
				throwAppSpawnException("An error occurred while starting the "
					"web application: it did not write a startup response in time.",
					SpawnException::APP_STARTUP_TIMEOUT,
					details);
			}

			if (line.empty()) {
				throwAppSpawnException("An error occurred while starting the "
					"web application. It unexpected closed the connection while "
					"sending its startup response.",
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					details);
			} else if (line[line.size() - 1] != '\n') {
				throwAppSpawnException("An error occurred while starting the "
					"web application. It sent a line without a newline character "
					"in its startup response.",
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					details);
			} else if (line == "\n") {
				break;
			}

			string::size_type pos = line.find(": ");
			if (pos == string::npos) {
				throwAppSpawnException("An error occurred while starting the "
					"web application. It sent a startup response line without "
					"separator.",
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					details);
			}

			string key = line.substr(0, pos);
			string value = line.substr(pos + 2, line.size() - pos - 3);
			if (key == "socket") {
				// socket: <name>;<address>;<protocol>;<concurrency>
				// TODO: in case of TCP sockets, check whether it points to localhost
				// TODO: in case of unix sockets, check whether filename is absolute
				// and whether owner is correct
				vector<string> args;
				split(value, ';', args);
				if (args.size() == 4) {
					string error = validateSocketAddress(details, args[1]);
					if (!error.empty()) {
						throwAppSpawnException(
							"An error occurred while starting the web application. " + error,
							SpawnException::APP_STARTUP_PROTOCOL_ERROR,
							details);
					}
					sockets->add(args[0],
						fixupSocketAddress(*details.options, args[1]),
						args[2],
						atoi(args[3]));
				} else {
					throwAppSpawnException("An error occurred while starting the "
						"web application. It reported a wrongly formatted 'socket'"
						"response value: '" + value + "'",
						SpawnException::APP_STARTUP_PROTOCOL_ERROR,
						details);
				}
			} else if (key == "pid") {
				// pid: <PID>
				pid_t pid = atoi(value);
				ProcessMetricsCollector collector;
				vector<pid_t> pids;

				pids.push_back(pid);
				ProcessMetricMap result = collector.collect(pids);
				if (result[pid].uid != details.preparation->uid) {
					throwAppSpawnException("An error occurred while starting the "
						"web application. The PID that the loader has returned does "
						"not have the same UID as the loader itself.",
						SpawnException::APP_STARTUP_PROTOCOL_ERROR,
						details);
				}
				details.pid = pid;
			} else {
				throwAppSpawnException("An error occurred while starting the "
					"web application. It sent an unknown startup response line "
					"called '" + key + "'.",
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					details);
			}
		}

		if (sockets->hasSessionSockets() == 0) {
			throwAppSpawnException("An error occured while starting the web "
				"application. It did not advertise any session sockets.",
				SpawnException::APP_STARTUP_PROTOCOL_ERROR,
				details);
		}

		ProcessPtr process = boost::make_shared<Process>(
			details.pid,
			details.gupid, details.connectPassword,
			details.adminSocket, details.errorPipe,
			sockets, creationTime, details.spawnStartTime);
		process->codeRevision = details.preparation->codeRevision;
		return process;
	}

protected:
	ServerInstanceDir::GenerationPtr generation;
	SpawnerConfigPtr config;

	static void nonInterruptableKillAndWaitpid(pid_t pid) {
		this_thread::disable_syscall_interruption dsi;
		syscalls::kill(pid, SIGKILL);
		syscalls::waitpid(pid, NULL, 0);
	}

	/**
	 * Behaves like <tt>waitpid(pid, status, WNOHANG)</tt>, but waits at most
	 * <em>timeout</em> miliseconds for the process to exit.
	 */
	static int timedWaitpid(pid_t pid, int *status, unsigned long long timeout) {
		Timer timer;
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

	static string fixupSocketAddress(const Options &options, const string &address) {
		TRACE_POINT();
		if (!options.preexecChroot.empty() && !options.postexecChroot.empty()) {
			ServerAddressType type = getSocketAddressType(address);
			if (type == SAT_UNIX) {
				string filename = parseUnixSocketAddress(address);
				string fixedAddress = "unix:";
				if (!options.preexecChroot.empty()) {
					fixedAddress.append(options.preexecChroot.data(),
						options.preexecChroot.size());
				}
				if (!options.postexecChroot.empty()) {
					fixedAddress.append(options.postexecChroot.data(),
						options.postexecChroot.size());
				}
				fixedAddress.append(filename);
				return fixedAddress;
			} else {
				return address;
			}
		} else {
			return address;
		}
	}

	bool isAbsolutePath(const StaticString &path) const {
		if (path.empty() || path[0] != '/') {
			return false;
		} else {
			vector<string> components;
			string component;

			split(path, '/', components);
			components.erase(components.begin());
			foreach (component, components) {
				if (component.empty() || component == "." || component == "..") {
					return false;
				}
			}
			return true;
		}
	}

	/**
	 * Given a 'socket:' information string obtained from the spawned process,
	 * validates whether it is correct.
	 */
	string validateSocketAddress(NegotiationDetails &details, const string &_address) const {
		string address = _address;
		stringstream error;

		switch (getSocketAddressType(address)) {
		case SAT_UNIX: {
			address = fixupSocketAddress(*details.options, address);
			string filename = parseUnixSocketAddress(address);

			// Verify that the socket filename is absolute.
			if (!isAbsolutePath(filename)) {
				error << "It reported a non-absolute socket filename: \"" <<
					cEscapeString(filename) << "\"";
				break;
			}

			// Verify that the process owns the socket.
			struct stat buf;
			if (lstat(filename.c_str(), &buf) == -1) {
				int e = errno;
				error << "It reported an inaccessible socket filename: \"" <<
					cEscapeString(filename) << "\" (lstat() failed with errno " <<
					e << ": " << strerror(e) << ")";
				break;
			}
			if (buf.st_uid != details.preparation->uid) {
				error << "It advertised a Unix domain socket that has a different " <<
					"owner than expected (should be UID " << details.preparation->uid <<
					", but actual UID was " << buf.st_uid << ")";
				break;
			}
			break;
		}
		case SAT_TCP:
			// TODO: validate that the socket is localhost.
			break;
		default:
			error << "It reported an unsupported socket address type: \"" <<
				cEscapeString(address) << "\"";
			break;
		}

		return error.str();
	}

	static void checkChrootDirectories(const Options &options) {
		if (!options.preexecChroot.empty()) {
			// TODO: check whether appRoot is a child directory of preexecChroot
			// and whether postexecChroot is a child directory of appRoot.
		}
	}

	static void createCommandArgs(const vector<string> &command,
		shared_array<const char *> &args)
	{
		args.reset(new const char *[command.size()]);
		for (unsigned int i = 1; i < command.size(); i++) {
			args[i - 1] = command[i].c_str();
		}
		args[command.size() - 1] = NULL;
	}

	void possiblyRaiseInternalError(const Options &options) {
		if (options.raiseInternalError) {
			throw RuntimeException("An internal error!");
		}
	}

	void throwAppSpawnException(const string &msg,
		SpawnException::ErrorKind errorKind,
		NegotiationDetails &details)
	{
		TRACE_POINT();
		// Stop the stderr capturing thread and get the captured stderr
		// output so far.
		string stderrOutput;
		if (details.stderrCapturer != NULL) {
			stderrOutput = details.stderrCapturer->stop();
		}

		// If the exception wasn't due to a timeout, try to capture the
		// remaining stderr output for at most 2 seconds.
		if (errorKind != SpawnException::PRELOADER_STARTUP_TIMEOUT
		 && errorKind != SpawnException::APP_STARTUP_TIMEOUT
		 && details.stderrCapturer != NULL)
		{
			bool done = false;
			unsigned long long timeout = 2000;
			while (!done) {
				char buf[1024 * 32];
				unsigned int ret;

				try {
					ret = readExact(details.stderrCapturer->getFd(), buf,
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
		details.stderrCapturer.reset();

		// Now throw SpawnException with the captured stderr output
		// as error response.
		SpawnException e(msg,
			createErrorPageFromStderrOutput(msg, errorKind, stderrOutput),
			true,
			errorKind);
		annotateAppSpawnException(e, details);
		throwSpawnException(e, *details.options);
	}

	void throwSpawnException(SpawnException &e, const Options &options) {
		processAndLogNewSpawnException(e, options, config);
		throw e;
	}

	virtual void annotateAppSpawnException(SpawnException &e, NegotiationDetails &details) {
		if (details.debugDir != NULL) {
			e.addAnnotations(details.debugDir->readAll());
		}
	}

	string createErrorPageFromStderrOutput(const string &msg,
		SpawnException::ErrorKind errorKind,
		const string &stderrOutput)
	{
		// These kinds of SpawnExceptions are not supposed to be handled through this function.
		assert(errorKind != SpawnException::PRELOADER_STARTUP_EXPLAINABLE_ERROR);
		assert(errorKind != SpawnException::APP_STARTUP_EXPLAINABLE_ERROR);

		string result = escapeHTML(msg);

		if (errorKind == SpawnException::PRELOADER_STARTUP_TIMEOUT
		 || errorKind == SpawnException::APP_STARTUP_TIMEOUT
		 || errorKind == SpawnException::PRELOADER_STARTUP_ERROR
		 || errorKind == SpawnException::APP_STARTUP_ERROR)
		{
			result.append(" Please read <a href=\"https://github.com/phusion/passenger/wiki/Debugging-application-startup-problems\">this article</a> "
				"for more information about this problem.");
		}
		result.append("<br>\n<h2>Raw process output:</h2>\n");

		if (strip(stderrOutput).empty()) {
			result.append("(empty)");
		} else {
			result.append("<pre>");
			result.append(escapeHTML(stderrOutput));
			result.append("</pre>");
		}

		return result;
	}

	template<typename Details>
	string readMessageLine(Details &details) {
		TRACE_POINT();
		while (true) {
			string result = details.io.readLine(1024 * 4, &details.timeout);
			string line = result;
			if (!line.empty() && line[line.size() - 1] == '\n') {
				line.erase(line.size() - 1, 1);
			}

			if (result.empty()) {
				// EOF
				return result;
			} else if (startsWith(result, "!> ")) {
				P_DEBUG("[App " << details.pid << " stdout] " << line);
				result.erase(0, sizeof("!> ") - 1);
				return result;
			} else {
				if (details.stderrCapturer != NULL) {
					details.stderrCapturer->appendToBuffer(result);
				}
				printAppOutput(details.pid, "stdout", line.data(), line.size());
			}
		}
	}

	SpawnPreparationInfo prepareSpawn(const Options &options) {
		TRACE_POINT();
		SpawnPreparationInfo info;
		prepareChroot(info, options);
		prepareUserSwitching(info, options);
		prepareSwitchingWorkingDirectory(info, options);
		inferApplicationInfo(info);
		return info;
	}

	void prepareChroot(SpawnPreparationInfo &info, const Options &options) {
		TRACE_POINT();
		info.appRoot = absolutizePath(options.appRoot);
		if (options.preexecChroot.empty()) {
			info.chrootDir = "/";
		} else {
			info.chrootDir = absolutizePath(options.preexecChroot);
		}
		if (info.appRoot != info.chrootDir && startsWith(info.appRoot, info.chrootDir + "/")) {
			SpawnException e("Invalid configuration: '" + info.chrootDir +
				"' has been configured as the chroot jail, but the application " +
				"root directory '" + info.appRoot + "' is not a subdirectory of the " +
				"chroot directory, which it must be.");
			throwSpawnException(e, options);
		}
		if (info.appRoot == info.chrootDir) {
			info.appRootInsideChroot = "/";
		} else if (info.chrootDir == "/") {
			info.appRootInsideChroot = info.appRoot;
		} else {
			info.appRootInsideChroot = info.appRoot.substr(info.chrootDir.size());
		}
	}

	void prepareUserSwitching(SpawnPreparationInfo &info, const Options &options) const {
		TRACE_POINT();
		if (geteuid() != 0) {
			struct passwd pwd, *userInfo;
			long bufSize;
			shared_array<char> strings;

			// _SC_GETPW_R_SIZE_MAX is not a maximum:
			// http://tomlee.co/2012/10/problems-with-large-linux-unix-groups-and-getgrgid_r-getgrnam_r/
			bufSize = std::max<long>(1024 * 128, sysconf(_SC_GETPW_R_SIZE_MAX));
			strings.reset(new char[bufSize]);

			userInfo = (struct passwd *) NULL;
			if (getpwuid_r(geteuid(), &pwd, strings.get(), bufSize, &userInfo) != 0
			 || userInfo == (struct passwd *) NULL)
			{
				throw RuntimeException("Cannot get user database entry for user " +
					getProcessUsername() + "; it looks like your system's " +
					"user database is broken, please fix it.");
			}

			info.switchUser = false;
			info.username = userInfo->pw_name;
			info.groupname = getGroupName(userInfo->pw_gid);
			info.home = userInfo->pw_dir;
			info.shell = userInfo->pw_shell;
			info.uid = geteuid();
			info.gid = getegid();
			info.ngroups = 0;
			return;
		}

		UPDATE_TRACE_POINT();
		string defaultGroup;
		string startupFile = absolutizePath(options.getStartupFile(), info.appRoot);
		struct passwd pwd, *userInfo;
		struct group  grp;
		gid_t  groupId = (gid_t) -1;
		long pwdBufSize, grpBufSize;
		shared_array<char> pwdBuf, grpBuf;
		int ret;

		// _SC_GETPW_R_SIZE_MAX/_SC_GETGR_R_SIZE_MAX are not maximums:
		// http://tomlee.co/2012/10/problems-with-large-linux-unix-groups-and-getgrgid_r-getgrnam_r/
		pwdBufSize = std::max<long>(1024 * 128, sysconf(_SC_GETPW_R_SIZE_MAX));
		pwdBuf.reset(new char[pwdBufSize]);
		grpBufSize = std::max<long>(1024 * 128, sysconf(_SC_GETGR_R_SIZE_MAX));
		grpBuf.reset(new char[grpBufSize]);

		if (options.defaultGroup.empty()) {
			struct passwd *info;
			struct group *group;

			info = (struct passwd *) NULL;
			ret = getpwnam_r(options.defaultUser.c_str(), &pwd, pwdBuf.get(),
				pwdBufSize, &info);
			if (ret != 0) {
				info = (struct passwd *) NULL;
			}
			if (info == (struct passwd *) NULL) {
				throw RuntimeException("Cannot get user database entry for username '" +
					options.defaultUser + "'");
			}

			group = (struct group *) NULL;
			ret = getgrgid_r(info->pw_gid, &grp, grpBuf.get(), grpBufSize, &group);
			if (ret != 0) {
				group = (struct group *) NULL;
			}
			if (group == (struct group *) NULL) {
				throw RuntimeException(string("Cannot get group database entry for ") +
					"the default group belonging to username '" +
					options.defaultUser + "'");
			}
			defaultGroup = group->gr_name;
		} else {
			defaultGroup = options.defaultGroup;
		}

		UPDATE_TRACE_POINT();
		userInfo = (struct passwd *) NULL;
		if (!options.user.empty()) {
			ret = getpwnam_r(options.user.c_str(), &pwd, pwdBuf.get(),
				pwdBufSize, &userInfo);
			if (ret != 0) {
				userInfo = (struct passwd *) NULL;
			}
		} else {
			struct stat buf;
			if (syscalls::lstat(startupFile.c_str(), &buf) == -1) {
				int e = errno;
				throw SystemException("Cannot lstat(\"" + startupFile +
					"\")", e);
			}
			ret = getpwuid_r(buf.st_uid, &pwd, pwdBuf.get(),
				pwdBufSize, &userInfo);
			if (ret != 0) {
				userInfo = (struct passwd *) NULL;
			}
		}
		if (userInfo == (struct passwd *) NULL || userInfo->pw_uid == 0) {
			userInfo = (struct passwd *) NULL;
			ret = getpwnam_r(options.defaultUser.c_str(), &pwd,
				pwdBuf.get(), pwdBufSize, &userInfo);
			if (ret != 0) {
				userInfo = (struct passwd *) NULL;
			}
		}

		UPDATE_TRACE_POINT();
		if (!options.group.empty()) {
			struct group *groupInfo = (struct group *) NULL;

			if (options.group == "!STARTUP_FILE!") {
				struct stat buf;

				if (syscalls::lstat(startupFile.c_str(), &buf) == -1) {
					int e = errno;
					throw SystemException("Cannot lstat(\"" +
						startupFile + "\")", e);
				}

				ret = getgrgid_r(buf.st_gid, &grp, grpBuf.get(), grpBufSize,
					&groupInfo);
				if (ret != 0) {
					groupInfo = (struct group *) NULL;
				}
				if (groupInfo != NULL) {
					groupId = buf.st_gid;
				} else {
					groupId = (gid_t) -1;
				}
			} else {
				ret = getgrnam_r(options.group.c_str(), &grp, grpBuf.get(),
					grpBufSize, &groupInfo);
				if (ret != 0) {
					groupInfo = (struct group *) NULL;
				}
				if (groupInfo != NULL) {
					groupId = groupInfo->gr_gid;
				} else {
					groupId = (gid_t) -1;
				}
			}
		} else if (userInfo != (struct passwd *) NULL) {
			groupId = userInfo->pw_gid;
		}
		if (groupId == 0 || groupId == (gid_t) -1) {
			groupId = lookupGid(defaultGroup);
		}

		UPDATE_TRACE_POINT();
		if (userInfo == (struct passwd *) NULL) {
			throw RuntimeException("Cannot determine a user to lower privilege to");
		}
		if (groupId == (gid_t) -1) {
			throw RuntimeException("Cannot determine a group to lower privilege to");
		}

		UPDATE_TRACE_POINT();
		#ifdef __APPLE__
			int groups[1024];
			info.ngroups = sizeof(groups) / sizeof(int);
		#else
			gid_t groups[1024];
			info.ngroups = sizeof(groups) / sizeof(gid_t);
		#endif
		info.switchUser = true;
		info.username = userInfo->pw_name;
		info.groupname = getGroupName(groupId);
		info.home = userInfo->pw_dir;
		info.shell = userInfo->pw_shell;
		info.uid = userInfo->pw_uid;
		info.gid = groupId;
		#if !defined(HAVE_GETGROUPLIST) && (defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__))
			#define HAVE_GETGROUPLIST
		#endif
		#ifdef HAVE_GETGROUPLIST
			ret = getgrouplist(userInfo->pw_name, groupId,
				groups, &info.ngroups);
			if (ret == -1) {
				int e = errno;
				throw SystemException("getgrouplist() failed", e);
			}
			info.gidset = shared_array<gid_t>(new gid_t[info.ngroups]);
			for (int i = 0; i < info.ngroups; i++) {
				info.gidset[i] = groups[i];
			}
		#endif
	}

	void prepareSwitchingWorkingDirectory(SpawnPreparationInfo &info, const Options &options) const {
		vector<string> components;
		split(info.appRootInsideChroot, '/', components);
		assert(components.front() == "");
		components.erase(components.begin());

		for (unsigned int i = 0; i < components.size(); i++) {
			string path;
			for (unsigned int j = 0; j <= i; j++) {
				path.append("/");
				path.append(components[j]);
			}
			if (path.empty()) {
				path = "/";
			}
			if (info.chrootDir == "/") {
				info.appRootPaths.push_back(path);
			} else {
				info.appRootPaths.push_back(info.chrootDir + path);
			}
			info.appRootPathsInsideChroot.push_back(path);
		}

		assert(info.appRootPathsInsideChroot.back() == info.appRootInsideChroot);
	}

	void inferApplicationInfo(SpawnPreparationInfo &info) const {
		info.codeRevision = readFromRevisionFile(info);
		if (info.codeRevision.empty()) {
			info.codeRevision = inferCodeRevisionFromCapistranoSymlink(info);
		}
	}

	string readFromRevisionFile(const SpawnPreparationInfo &info) const {
		string filename = info.appRoot + "/REVISION";
		try {
			if (fileExists(filename)) {
				return strip(readAll(filename));
			}
		} catch (const SystemException &e) {
			P_WARN("Cannot access " << filename << ": " << e.what());
		}
		return string();
	}

	string inferCodeRevisionFromCapistranoSymlink(const SpawnPreparationInfo &info) const {
		if (extractBaseName(info.appRoot) == "current") {
			char buf[PATH_MAX + 1];
			ssize_t ret;

			do {
				ret = readlink(info.appRoot.c_str(), buf, PATH_MAX);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				if (errno == EINVAL) {
					return string();
				} else {
					int e = errno;
					P_WARN("Cannot read symlink " << info.appRoot << ": " << strerror(e));
				}
			}

			buf[ret] = '\0';
			return extractBaseName(buf);
		} else {
			return string();
		}
	}

	bool shouldLoadShellEnvvars(const Options &options, const SpawnPreparationInfo &preparation) const {
		if (options.loadShellEnvvars) {
			string shellName = extractBaseName(preparation.shell);
			return shellName == "bash" || shellName == "zsh" || shellName == "ksh";
		} else {
			return false;
		}
	}

	string serializeEnvvarsFromPoolOptions(const Options &options) const {
		vector< pair<StaticString, StaticString> >::const_iterator it, end;
		string result;

		appendNullTerminatedKeyValue(result, "IN_PASSENGER", "1");
		appendNullTerminatedKeyValue(result, "PYTHONUNBUFFERED", "1");
		appendNullTerminatedKeyValue(result, "NODE_PATH", config->resourceLocator.getNodeLibDir());
		appendNullTerminatedKeyValue(result, "RAILS_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "RACK_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "WSGI_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "NODE_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "PASSENGER_APP_ENV", options.environment);
		if (!options.baseURI.empty() && options.baseURI != "/") {
			appendNullTerminatedKeyValue(result,
				"RAILS_RELATIVE_URL_ROOT",
				options.baseURI);
			appendNullTerminatedKeyValue(result,
				"RACK_BASE_URI",
				options.baseURI);
			appendNullTerminatedKeyValue(result,
				"PASSENGER_BASE_URI",
				options.baseURI);
		}

		it  = options.environmentVariables.begin();
		end = options.environmentVariables.end();
		while (it != end) {
			appendNullTerminatedKeyValue(result, it->first, it->second);
			it++;
		}

		return Base64::encode(result);
	}

	void switchUser(const SpawnPreparationInfo &info) {
		if (info.switchUser) {
			bool setgroupsCalled = false;
			#ifdef HAVE_GETGROUPLIST
				if (info.ngroups <= NGROUPS_MAX) {
					setgroupsCalled = true;
					if (setgroups(info.ngroups, info.gidset.get()) == -1) {
						int e = errno;
						printf("!> Error\n");
						printf("!> \n");
						printf("setgroups(%d, ...) failed: %s (errno=%d)\n",
							info.ngroups, strerror(e), e);
						fflush(stdout);
						_exit(1);
					}
				}
			#endif
			if (!setgroupsCalled && initgroups(info.username.c_str(), info.gid) == -1) {
				int e = errno;
				printf("!> Error\n");
				printf("!> \n");
				printf("initgroups() failed: %s (errno=%d)\n",
					strerror(e), e);
				fflush(stdout);
				_exit(1);
			}
			if (setgid(info.gid) == -1) {
				int e = errno;
				printf("!> Error\n");
				printf("!> \n");
				printf("setgid() failed: %s (errno=%d)\n",
					strerror(e), e);
				fflush(stdout);
				_exit(1);
			}
			if (setuid(info.uid) == -1) {
				int e = errno;
				printf("!> Error\n");
				printf("!> \n");
				printf("setuid() failed: %s (errno=%d)\n",
					strerror(e), e);
				fflush(stdout);
				_exit(1);
			}

			// We set these environment variables here instead of
			// in the SpawnPreparer because SpawnPreparer might
			// be executed by bash, but these environment variables
			// must be set before bash.
			setenv("USER", info.username.c_str(), 1);
			setenv("LOGNAME", info.username.c_str(), 1);
			setenv("SHELL", info.shell.c_str(), 1);
			setenv("HOME", info.home.c_str(), 1);
		}
	}

	void setChroot(const SpawnPreparationInfo &info) {
		if (info.chrootDir != "/") {
			int ret = chroot(info.chrootDir.c_str());
			if (ret == -1) {
				int e = errno;
				fprintf(stderr, "Cannot chroot() to '%s': %s (errno=%d)\n",
					info.chrootDir.c_str(),
					strerror(e),
					e);
				fflush(stderr);
				_exit(1);
			}
		}
	}

	void setWorkingDirectory(const SpawnPreparationInfo &info) {
		vector<string>::const_iterator it, end = info.appRootPathsInsideChroot.end();
		int ret;

		for (it = info.appRootPathsInsideChroot.begin(); it != end; it++) {
			struct stat buf;
			ret = stat(it->c_str(), &buf);
			if (ret == -1 && errno == EACCES) {
				char parent[PATH_MAX];
				const char *end = strrchr(it->c_str(), '/');
				memcpy(parent, it->c_str(), end - it->c_str());
				parent[end - it->c_str()] = '\0';

				printf("!> Error\n");
				printf("!> \n");
				printf("This web application process is being run as user '%s' and group '%s' "
					"and must be able to access its application root directory '%s'. "
					"However, the parent directory '%s' has wrong permissions, thereby "
					"preventing this process from accessing its application root directory. "
					"Please fix the permissions of the directory '%s' first.\n",
					info.username.c_str(),
					info.groupname.c_str(),
					info.appRootPaths.back().c_str(),
					parent,
					parent);
				fflush(stdout);
				_exit(1);
			} else if (ret == -1) {
				int e = errno;
				printf("!> Error\n");
				printf("!> \n");
				printf("Unable to stat() directory '%s': %s (errno=%d)\n",
					it->c_str(), strerror(e), e);
				fflush(stdout);
				_exit(1);
			}
		}

		ret = chdir(info.appRootPathsInsideChroot.back().c_str());
		if (ret == 0) {
			setenv("PWD", info.appRootPathsInsideChroot.back().c_str(), 1);
		} else if (ret == -1 && errno == EACCES) {
			printf("!> Error\n");
			printf("!> \n");
			printf("This web application process is being run as user '%s' and group '%s' "
				"and must be able to access its application root directory '%s'. "
				"However this directory is not accessible because it has wrong permissions. "
				"Please fix these permissions first.\n",
				info.username.c_str(),
				info.groupname.c_str(),
				info.appRootPaths.back().c_str());
			fflush(stdout);
			_exit(1);
		} else {
			int e = errno;
			printf("!> Error\n");
			printf("!> \n");
			printf("Unable to change working directory to '%s': %s (errno=%d)\n",
				info.appRootPathsInsideChroot.back().c_str(), strerror(e), e);
			fflush(stdout);
			_exit(1);
		}
	}

	/**
	 * Execute the process spawning negotiation protocol.
	 */
	ProcessPtr negotiateSpawn(NegotiationDetails &details) {
		TRACE_POINT();
		details.spawnStartTime = SystemTime::getUsec();
		details.gupid = integerToHex(SystemTime::get() / 60) + "-" +
			config->randomGenerator->generateAsciiString(11);
		details.connectPassword = config->randomGenerator->generateAsciiString(43);
		details.timeout = details.options->startTimeout * 1000;

		string result;
		try {
			result = readMessageLine(details);
		} catch (const SystemException &e) {
			throwAppSpawnException("An error occurred while starting the "
				"web application. There was an I/O error while reading its "
				"handshake message: " + e.sys(),
				SpawnException::APP_STARTUP_PROTOCOL_ERROR,
				details);
		} catch (const TimeoutException &) {
			throwAppSpawnException("An error occurred while starting the "
				"web application: it did not write a handshake message in time.",
				SpawnException::APP_STARTUP_TIMEOUT,
				details);
		}

		protocol_begin:
		if (result == "I have control 1.0\n") {
			UPDATE_TRACE_POINT();
			sendSpawnRequest(details);
			try {
				result = readMessageLine(details);
			} catch (const SystemException &e) {
				throwAppSpawnException("An error occurred while starting the "
					"web application. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					details);
			} catch (const TimeoutException &) {
				throwAppSpawnException("An error occurred while starting the "
					"web application: it did not write a startup response in time.",
					SpawnException::APP_STARTUP_TIMEOUT,
					details);
			}
			if (result == "Ready\n") {
				return handleSpawnResponse(details);
			} else if (result == "Error\n") {
				handleSpawnErrorResponse(details);
			} else if (result == "I have control 1.0\n") {
				goto protocol_begin;
			} else {
				handleInvalidSpawnResponseType(result, details);
			}
		} else {
			UPDATE_TRACE_POINT();
			if (result == "Error\n") {
				handleSpawnErrorResponse(details);
			} else {
				handleInvalidSpawnResponseType(result, details);
			}
		}
		return ProcessPtr(); // Never reached.
	}

	void handleSpawnErrorResponse(NegotiationDetails &details) {
		TRACE_POINT();
		map<string, string> attributes;

		while (true) {
			string line = readMessageLine(details);
			if (line.empty()) {
				throwAppSpawnException("An error occurred while starting the "
					"web application. It unexpected closed the connection while "
					"sending its startup response.",
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					details);
			} else if (line[line.size() - 1] != '\n') {
				throwAppSpawnException("An error occurred while starting the "
					"web application. It sent a line without a newline character "
					"in its startup response.",
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					details);
			} else if (line == "\n") {
				break;
			}

			string::size_type pos = line.find(": ");
			if (pos == string::npos) {
				throwAppSpawnException("An error occurred while starting the "
					"web application. It sent a startup response line without "
					"separator.",
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					details);
			}

			string key = line.substr(0, pos);
			string value = line.substr(pos + 2, line.size() - pos - 3);
			attributes[key] = value;
		}

		try {
			string message = details.io.readAll(&details.timeout);
			SpawnException e("An error occured while starting the web application.",
				message,
				attributes["html"] == "true",
				SpawnException::APP_STARTUP_EXPLAINABLE_ERROR);
			annotateAppSpawnException(e, details);
			throwSpawnException(e, *details.options);
		} catch (const SystemException &e) {
			throwAppSpawnException("An error occurred while starting the "
				"web application. It tried to report an error message, but "
				"an I/O error occurred while reading this error message: " +
				e.sys(),
				SpawnException::APP_STARTUP_PROTOCOL_ERROR,
				details);
		} catch (const TimeoutException &) {
			throwAppSpawnException("An error occurred while starting the "
				"web application. It tried to report an error message, but "
				"it took too much time doing that.",
				SpawnException::APP_STARTUP_TIMEOUT,
				details);
		}
	}

	void handleInvalidSpawnResponseType(const string &line, NegotiationDetails &details) {
		if (line.empty()) {
			throwAppSpawnException("An error occurred while starting "
				"the web application. It exited before signalling successful "
				"startup back to " PROGRAM_NAME ".",
				SpawnException::APP_STARTUP_ERROR,
				details);
		} else {
			throwAppSpawnException("An error occurred while starting "
				"the web application. It sent an unknown response type \"" +
				cEscapeString(line) + "\".",
				SpawnException::APP_STARTUP_PROTOCOL_ERROR,
				details);
		}
	}

public:
	/**
	 * Timestamp at which this Spawner was created. Microseconds resolution.
	 */
	const unsigned long long creationTime;

	Spawner(const SpawnerConfigPtr &_config)
		: config(_config),
		  creationTime(SystemTime::getUsec())
		{ }

	virtual ~Spawner() { }
	virtual ProcessPtr spawn(const Options &options) = 0;

	/** Does not depend on the event loop. */
	virtual bool cleanable() const {
		return false;
	}

	virtual void cleanup() { }

	/** Does not depend on the event loop. */
	virtual unsigned long long lastUsed() const {
		return 0;
	}

	SpawnerConfigPtr getConfig() const {
		return config;
	}
};
typedef boost::shared_ptr<Spawner> SpawnerPtr;


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SPAWNER_H_ */
