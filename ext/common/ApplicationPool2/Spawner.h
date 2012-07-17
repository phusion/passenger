/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2011, 2012 Phusion
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
#include <boost/make_shared.hpp>
#include <boost/shared_array.hpp>
#include <boost/bind.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>  // for PTHREAD_STACK_MIN
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Options.h>
#include <ApplicationPool2/PipeWatcher.h>
#include <FileDescriptor.h>
#include <SafeLibev.h>
#include <Exceptions.h>
#include <ResourceLocator.h>
#include <StaticString.h>
#include <ServerInstanceDir.h>
#include <Utils/BufferedIO.h>
#include <Utils/ScopeGuard.h>
#include <Utils/Timer.h>
#include <Utils/IOUtils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/Base64.h>

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
		int target;
		boost::mutex dataSyncher;
		string data;
		oxt::thread *thr;
		
		void capture() {
			TRACE_POINT();
			try {
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
						P_WARN("Background I/O capturer error: " <<
							strerror(e) << " (errno=" << e << ")");
						break;
					} else {
						{
							lock_guard<boost::mutex> l(dataSyncher);
							data.append(buf, ret);
						}
						if (target != -1) {
							UPDATE_TRACE_POINT();
							writeExact(target, buf, ret);
						}
					}
				}
			} catch (const thread_interrupted &) {
				// Return.
			}
		}
		
	public:
		BackgroundIOCapturer(const FileDescriptor &_fd, int _target)
			: fd(_fd),
			  target(_target),
			  thr(NULL)
			{ }
		
		~BackgroundIOCapturer() {
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
			assert(thr != NULL);
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			thr->interrupt_and_join();
			delete thr;
			thr = NULL;
			lock_guard<boost::mutex> l(dataSyncher);
			return data;
		}

		void appendToBuffer(const StaticString &dataToAdd) {
			lock_guard<boost::mutex> l(dataSyncher);
			data.append(dataToAdd.data(), dataToAdd.size());
		}
	};
	
	typedef shared_ptr<BackgroundIOCapturer> BackgroundIOCapturerPtr;
	
	/**
	 * A temporary directory for spawned child processes to write
	 * debugging information to. It is removed after spawning has
	 * determined to be successful or failed.
	 */
	struct DebugDir {
		string path;

		DebugDir(uid_t uid, gid_t gid) {
			path = "/tmp/passenger.spawn-debug.";
			path.append(toString(getpid()));
			path.append("-");
			path.append(pointerToIntString(this));

			if (syscalls::mkdir(path.c_str(), 0700) == -1) {
				int e = errno;
				throw FileSystemException("Cannot create directory '" +
					path + "'", e, path);
			}
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			syscalls::chown(path.c_str(), uid, gid);
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
			ScopeGuard guard(boost::bind(closedir, dir));
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

	typedef shared_ptr<DebugDir> DebugDirPtr;

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
	};

	/**
	 * Structure containing arguments and working state for negotiating
	 * the spawning protocol.
	 */
	struct NegotiationDetails {
		// Arguments.
		SafeLibevPtr libev;
		BackgroundIOCapturerPtr stderrCapturer;
		pid_t pid;
		FileDescriptor adminSocket;
		FileDescriptor errorPipe;
		const Options *options;
		bool forwardStderr;
		DebugDirPtr debugDir;
		
		// Working state.
		BufferedIO io;
		string gupid;
		string connectPassword;
		unsigned long long spawnStartTime;
		unsigned long long timeout;
		
		NegotiationDetails() {
			pid = 0;
			options = NULL;
			forwardStderr = false;
			spawnStartTime = 0;
			timeout = 0;
		}
	};

	/**
	 * Structure containing arguments and working state for negotiating
	 * the preloader startup protocol.
	 */
	struct StartupDetails {
		// Arguments.
		FileDescriptor adminSocket;
		BufferedIO io;
		BackgroundIOCapturerPtr stderrCapturer;
		DebugDirPtr debugDir;
		const Options *options;
		bool forwardStderr;

		// Working state.
		unsigned long long timeout;

		StartupDetails() {
			options = NULL;
			forwardStderr = false;
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
		try {
			string data = "You have control 1.0\n"
				"passenger_root: " + resourceLocator.getRoot() + "\n"
				"passenger_version: " PASSENGER_VERSION "\n"
				"ruby_libdir: " + resourceLocator.getRubyLibDir() + "\n"
				"generation_dir: " + generation->getPath() + "\n"
				"gupid: " + details.gupid + "\n"
				"connect_password: " + details.connectPassword + "\n";

			vector<string> args;
			vector<string>::const_iterator it, end;
			details.options->toVector(args, resourceLocator);
			for (it = args.begin(); it != args.end(); it++) {
				const string &key = *it;
				it++;
				const string &value = *it;
				data.append(key + ": " + value + "\n");
			}

			writeExact(details.adminSocket, data, &details.timeout);
			P_TRACE(2, "Spawn request for " << details.options->appRoot << ":\n" << data);
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
		SocketListPtr sockets = make_shared<SocketList>();
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
		
		return make_shared<Process>(details.libev, details.pid,
			details.gupid, details.connectPassword,
			details.adminSocket, details.errorPipe,
			sockets, creationTime, details.spawnStartTime,
			details.forwardStderr);
	}
	
protected:
	ResourceLocator resourceLocator;
	RandomGeneratorPtr randomGenerator;
	ServerInstanceDir::GenerationPtr generation;
	
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
		 && details.stderrCapturer != NULL) {
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
		SpawnException e(msg, stderrOutput, false, errorKind);
		annotateAppSpawnException(e, details);
		throw e;
	}

	virtual void annotateAppSpawnException(SpawnException &e, NegotiationDetails &details) {
		if (details.debugDir != NULL) {
			e.addAnnotations(details.debugDir->readAll());
		}
	}

	template<typename Details>
	static string readMessageLine(Details &details) {
		while (true) {
			string result = details.io.readLine(1024 * 4, &details.timeout);
			if (result.empty()) {
				return result;
			} else if (startsWith(result, "!> ")) {
				result.erase(0, sizeof("!> ") - 1);
				return result;
			} else {
				if (details.stderrCapturer != NULL) {
					details.stderrCapturer->appendToBuffer(result);
				}
				if (details.forwardStderr) {
					write(STDOUT_FILENO, result.data(), result.size());
				}
			}
		}
	}
	
	SpawnPreparationInfo prepareSpawn(const Options &options) const {
		SpawnPreparationInfo info;
		prepareChroot(info, options);
		prepareUserSwitching(info, options);
		prepareSwitchingWorkingDirectory(info, options);
		return info;
	}

	void prepareChroot(SpawnPreparationInfo &info, const Options &options) const {
		info.appRoot = absolutizePath(options.appRoot);
		if (options.preexecChroot.empty()) {
			info.chrootDir = "/";
		} else {
			info.chrootDir = absolutizePath(options.preexecChroot);
		}
		if (info.appRoot != info.chrootDir && startsWith(info.appRoot, info.chrootDir + "/")) {
			throw SpawnException("Invalid configuration: '" + info.chrootDir +
				"' has been configured as the chroot jail, but the application " +
				"root directory '" + info.appRoot + "' is not a subdirectory of the " +
				"chroot directory, which it must be.");
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
		if (geteuid() != 0) {
			struct passwd *userInfo = getpwuid(geteuid());
			if (userInfo == NULL) {
				throw RuntimeException("Cannot get user database entry for user " +
					getProcessUsername() + "; it looks like your system's " +
					"user database is broken, please fix it.");
			}
			struct group *groupInfo = getgrgid(userInfo->pw_gid);
			if (groupInfo == NULL) {
				throw RuntimeException(string("Cannot get group database entry for ") +
					"the default group belonging to username '" +
					getProcessUsername() + "'; it looks like your system's " +
					"user database is broken, please fix it.");
			}
			
			info.switchUser = false;
			info.username = userInfo->pw_name;
			info.groupname = groupInfo->gr_name;
			info.home = userInfo->pw_dir;
			info.shell = userInfo->pw_shell;
			info.uid = geteuid();
			info.gid = getegid();
			info.ngroups = 0;
			return;
		}
		
		string defaultGroup;
		string startupFile = absolutizePath(options.getStartupFile(), info.appRoot);
		struct passwd *userInfo = NULL;
		struct group *groupInfo = NULL;
		
		if (options.defaultGroup.empty()) {
			struct passwd *info = getpwnam(options.defaultUser.c_str());
			if (info == NULL) {
				throw RuntimeException("Cannot get user database entry for username '" +
					options.defaultUser + "'");
			}
			struct group *group = getgrgid(info->pw_gid);
			if (group == NULL) {
				throw RuntimeException(string("Cannot get group database entry for ") +
					"the default group belonging to username '" +
					options.defaultUser + "'");
			}
			defaultGroup = group->gr_name;
		} else {
			defaultGroup = options.defaultGroup;
		}
		
		if (!options.user.empty()) {
			userInfo = getpwnam(options.user.c_str());
		} else {
			struct stat buf;
			if (syscalls::lstat(startupFile.c_str(), &buf) == -1) {
				int e = errno;
				throw SystemException("Cannot lstat(\"" + startupFile +
					"\")", e);
			}
			userInfo = getpwuid(buf.st_uid);
		}
		if (userInfo == NULL || userInfo->pw_uid == 0) {
			userInfo = getpwnam(options.defaultUser.c_str());
		}
		
		if (!options.group.empty()) {
			if (options.group == "!STARTUP_FILE!") {
				struct stat buf;
				if (syscalls::lstat(startupFile.c_str(), &buf) == -1) {
					int e = errno;
					throw SystemException("Cannot lstat(\"" +
						startupFile + "\")", e);
				}
				groupInfo = getgrgid(buf.st_gid);
			} else {
				groupInfo = getgrnam(options.group.c_str());
			}
		} else if (userInfo != NULL) {
			groupInfo = getgrgid(userInfo->pw_gid);
		}
		if (groupInfo == NULL || groupInfo->gr_gid == 0) {
			groupInfo = getgrnam(defaultGroup.c_str());
		}
		
		if (userInfo == NULL) {
			throw RuntimeException("Cannot determine a user to lower privilege to");
		}
		if (groupInfo == NULL) {
			throw RuntimeException("Cannot determine a group to lower privilege to");
		}
		
		#ifdef __APPLE__
			int groups[1024];
			info.ngroups = sizeof(groups) / sizeof(int);
		#else
			gid_t groups[1024];
			info.ngroups = sizeof(groups) / sizeof(gid_t);
		#endif
		int ret;
		info.switchUser = true;
		info.username = userInfo->pw_name;
		info.groupname = groupInfo->gr_name;
		info.home = userInfo->pw_dir;
		info.shell = userInfo->pw_shell;
		info.uid = userInfo->pw_uid;
		info.gid = groupInfo->gr_gid;
		ret = getgrouplist(userInfo->pw_name, groupInfo->gr_gid,
			groups, &info.ngroups);
		if (ret == -1) {
			int e = errno;
			throw SystemException("getgrouplist() failed", e);
		}
		info.gidset = shared_array<gid_t>(new gid_t[info.ngroups]);
		for (int i = 0; i < info.ngroups; i++) {
			info.gidset[i] = groups[i];
		}
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
	
	string serializeEnvvarsFromPoolOptions(const Options &options) const {
		vector< pair<StaticString, StaticString> >::const_iterator it, end;
		string result;
		
		appendNullTerminatedKeyValue(result, "IN_PASSENGER", "1");
		appendNullTerminatedKeyValue(result, "PYTHONUNBUFFERED", "1");
		appendNullTerminatedKeyValue(result, "RAILS_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "RACK_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "WSGI_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "PASSENGER_ENV", options.environment);
		if (!options.baseURI.empty() && options.baseURI != "/") {
			appendNullTerminatedKeyValue(result,
				"RAILS_RELATIVE_URL_ROOT",
				options.environment);
			appendNullTerminatedKeyValue(result,
				"RACK_BASE_URI",
				options.environment);
			appendNullTerminatedKeyValue(result,
				"PASSENGER_BASE_URI",
				options.environment);
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
			if (setgroups(info.ngroups, info.gidset.get()) == -1) {
				int e = errno;
				printf("!> Error\n");
				printf("!> \n");
				printf("setgroups() failed: %s (errno=%d)\n",
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
	
	ProcessPtr negotiateSpawn(NegotiationDetails &details) {
		details.spawnStartTime = SystemTime::getUsec();
		details.gupid = integerToHex(SystemTime::get() / 60) + "-" +
			randomGenerator->generateAsciiString(11);
		details.connectPassword = randomGenerator->generateAsciiString(43);
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
		
		if (result == "I have control 1.0\n") {
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
			} else {
				handleInvalidSpawnResponseType(result, details);
			}
		} else {
			if (result == "Error\n") {
				handleSpawnErrorResponse(details);
			} else {
				handleInvalidSpawnResponseType(result, details);
			}
		}
		return ProcessPtr(); // Never reached.
	}
	
	void handleSpawnErrorResponse(NegotiationDetails &details) {
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
			throw e;
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
		throwAppSpawnException("An error occurred while starting "
			"the web application. It sent an unknown response type \"" +
			cEscapeString(line) + "\".",
			SpawnException::APP_STARTUP_PROTOCOL_ERROR,
			details);
	}
	
public:
	/**
	 * Timestamp at which this Spawner was created. Microseconds resolution.
	 */
	const unsigned long long creationTime;

	Spawner(const ResourceLocator &_resourceLocator)
		: resourceLocator(_resourceLocator),
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
};
typedef shared_ptr<Spawner> SpawnerPtr;


class SmartSpawner: public Spawner, public enable_shared_from_this<SmartSpawner> {
private:
	struct SpawnResult {
		pid_t pid;
		FileDescriptor adminSocket;
		BufferedIO io;
	};

	/** The event loop that created Process objects should use, and that I/O forwarding
	 * functions should use. For example data on the error pipe is forwarded using this event loop.
	 */
	SafeLibevPtr libev;
	const vector<string> preloaderCommand;
	map<string, string> preloaderAnnotations;
	Options options;
	ev::io preloaderOutputWatcher;
	shared_ptr<PipeWatcher> preloaderErrorWatcher;
	
	// Protects m_lastUsed and pid.
	mutable boost::mutex simpleFieldSyncher;
	// Protects everything else.
	mutable boost::mutex syncher;

	pid_t pid;
	FileDescriptor adminSocket;
	string socketAddress;
	unsigned long long m_lastUsed;
	
	void onPreloaderOutputReadable(ev::io &io, int revents) {
		char buf[1024 * 8];
		ssize_t ret;
		
		ret = syscalls::read(adminSocket, buf, sizeof(buf));
		if (ret <= 0) {
			preloaderOutputWatcher.stop();
		} else if (forwardStdout) {
			write(STDOUT_FILENO, buf, ret);
		}
	}

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
		string agentsDir = resourceLocator.getAgentsDir();
		vector<string> command;
		
		if (options.loadShellEnvvars) {
			command.push_back("bash");
			command.push_back("bash");
			command.push_back("-lc");
			command.push_back("exec \"$@\"");
			command.push_back("SpawnPreparerShell");
		} else {
			command.push_back(agentsDir + "/SpawnPreparer");
		}
		command.push_back(agentsDir + "/SpawnPreparer");
		command.push_back(serializeEnvvarsFromPoolOptions(options));
		command.push_back(preloaderCommand[0]);
		command.push_back("Passenger AppPreloader: " + options.appRoot);
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
			details.debugDir);
	}

	void throwPreloaderSpawnException(const string &msg,
		SpawnException::ErrorKind errorKind,
		BackgroundIOCapturerPtr &stderrCapturer,
		const DebugDirPtr &debugDir)
	{
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
		 && stderrCapturer != NULL) {
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
		SpawnException e(msg, stderrOutput, false, errorKind);
		e.setPreloaderCommand(getPreloaderCommandString());
		annotatePreloaderException(e, debugDir);
		throw e;
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
		assert(!preloaderStarted());
		checkChrootDirectories(options);
		
		shared_array<const char *> args;
		vector<string> command = createRealPreloaderCommand(options, args);
		SpawnPreparationInfo preparation = prepareSpawn(options);
		SocketPair adminSocket = createUnixSocketPair();
		Pipe errorPipe = createPipe();
		DebugDirPtr debugDir = make_shared<DebugDir>(preparation.uid, preparation.gid);
		pid_t pid;
		
		pid = syscalls::fork();
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
			ScopeGuard guard(boost::bind(nonInterruptableKillAndWaitpid, pid));
			adminSocket.first.close();
			errorPipe.second.close();
			
			StartupDetails details;
			details.adminSocket = adminSocket.second;
			details.io = BufferedIO(adminSocket.second);
			details.stderrCapturer =
				make_shared<BackgroundIOCapturer>(
					errorPipe.first,
					forwardStderr ? STDERR_FILENO : -1);
			details.stderrCapturer->start();
			details.debugDir = debugDir;
			details.options = &options;
			details.timeout = options.startTimeout * 1000;
			details.forwardStderr = forwardStderr;
			
			this->socketAddress = negotiatePreloaderStartup(details);
			this->pid = pid;
			this->adminSocket = adminSocket.second;
			{
				lock_guard<boost::mutex> l(simpleFieldSyncher);
				this->pid = pid;
			}
			preloaderOutputWatcher.set(adminSocket.second, ev::READ);
			libev->start(preloaderOutputWatcher);
			preloaderErrorWatcher = make_shared<PipeWatcher>(libev,
				errorPipe.first, forwardStderr ? STDERR_FILENO : -1);
			preloaderErrorWatcher->start();
			preloaderAnnotations = debugDir->readAll();
			guard.clear();
		}
	}
	
	void stopPreloader() {
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		
		if (!preloaderStarted()) {
			return;
		}
		adminSocket.close();
		if (timedWaitpid(pid, NULL, 5000) == 0) {
			P_TRACE(2, "Spawn server did not exit in time, killing it...");
			syscalls::kill(pid, SIGKILL);
			syscalls::waitpid(pid, NULL, 0);
		}
		libev->stop(preloaderOutputWatcher);
		// Detach the error pipe; it will truly be closed after the error
		// pipe has reached EOF.
		preloaderErrorWatcher.reset();
		// Delete socket after the process has exited so that it
		// doesn't crash upon deleting a nonexistant file.
		// TODO: in Passenger 4 we must check whether the file really was
		// owned by the preloader, otherwise this is a potential security flaw.
		if (getSocketAddressType(socketAddress) == SAT_UNIX) {
			string filename = parseUnixSocketAddress(socketAddress);
			syscalls::unlink(filename.c_str());
		}
		{
			lock_guard<boost::mutex> l(simpleFieldSyncher);
			pid = -1;
		}
		socketAddress.clear();
	}
	
	void sendStartupRequest(StartupDetails &details) {
		try {
			writeExact(details.adminSocket,
				"You have control 1.0\n"
				"passenger_root: " + resourceLocator.getRoot() + "\n"
				"ruby_libdir: " + resourceLocator.getRubyLibDir() + "\n"
				"passenger_version: " PASSENGER_VERSION "\n"
				"generation_dir: " + generation->getPath() + "\n",
				&details.timeout);

			vector<string> args;
			vector<string>::const_iterator it, end;
			details.options->toVector(args, resourceLocator);
			for (it = args.begin(); it != args.end(); it++) {
				const string &key = *it;
				it++;
				const string &value = *it;
				writeExact(details.adminSocket,
					key + ": " + value + "\n",
					&details.timeout);
			}
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
			SpawnException e("An error occured while starting up the preloader.",
				message,
				attributes["html"] == "true",
				SpawnException::PRELOADER_STARTUP_EXPLAINABLE_ERROR);
			e.setPreloaderCommand(getPreloaderCommandString());
			annotatePreloaderException(e, details.debugDir);
			throw e;
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
		throwPreloaderSpawnException("An error occurred while starting up "
			"the preloader. It sent an unknown response type \"" +
			cEscapeString(line) + "\".",
			SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
			details);
	}
	
	string negotiatePreloaderStartup(StartupDetails &details) {
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
	
	SpawnResult sendSpawnCommand(const Options &options) {
		FileDescriptor fd;
		try {
			fd = connectToServer(socketAddress);
		} catch (const SystemException &e) {
			BackgroundIOCapturerPtr stderrCapturer;
			throwPreloaderSpawnException("An error occurred while starting "
				"the application. Unable to connect to the preloader's "
				"socket: " + string(e.what()),
				SpawnException::APP_STARTUP_PROTOCOL_ERROR,
				stderrCapturer,
				DebugDirPtr());
		}
		
		BufferedIO io(fd);
		unsigned long long timeout = options.startTimeout * 1000;
		string result;
		vector<string> args;
		vector<string>::const_iterator it;
		
		writeExact(fd, "spawn\n", &timeout);
		options.toVector(args, resourceLocator);
		for (it = args.begin(); it != args.end(); it++) {
			const string &key = *it;
			it++;
			const string &value = *it;
			writeExact(fd, key + ": " + value + "\n", &timeout);
		}
		writeExact(fd, "\n", &timeout);
		
		result = io.readLine(1024, &timeout);
		if (result == "OK\n") {
			pid_t spawnedPid;
			
			spawnedPid = atoi(io.readLine(1024, &timeout).c_str());
			if (spawnedPid <= 0) {
				BackgroundIOCapturerPtr stderrCapturer;
				throwPreloaderSpawnException("An error occurred while starting "
					"the web application. Its preloader responded to the "
					"'spawn' command with an invalid PID: '" +
					toString(spawnedPid) + "'",
					SpawnException::APP_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
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
					DebugDirPtr());
			}
			
			SpawnResult result;
			result.pid = spawnedPid;
			result.adminSocket = fd;
			result.io = io;
			return result;
			
		} else if (result == "Error\n") {
			NegotiationDetails details;
			details.io = io;
			details.timeout = timeout;
			handleSpawnErrorResponse(details);
			
		} else {
			NegotiationDetails details;
			handleInvalidSpawnResponseType(result, details);
		}
		
		return SpawnResult(); // Never reached.
	}
	
	template<typename Exception>
	SpawnResult sendSpawnCommandAgain(const Exception &e, const Options &options) {
		P_WARN("An error occurred while spawning a process: " << e.what());
		P_WARN("The application preloader seems to have crashed, restarting it and trying again...");
		stopPreloader();
		startPreloader();
		ScopeGuard guard(boost::bind(&SmartSpawner::stopPreloader, this));
		SpawnResult result = sendSpawnCommand(options);
		guard.clear();
		return result;
	}
	
protected:
	virtual void annotateAppSpawnException(SpawnException &e, NegotiationDetails &details) {
		Spawner::annotateAppSpawnException(e, details);
		e.addAnnotations(preloaderAnnotations);
	}

public:
	/** Whether to forward the preloader process's stdout to our stdout. True by default. */
	bool forwardStdout;
	/** Whether to forward the preloader process's stderr to our stderr. True by default. */
	bool forwardStderr;
	
	SmartSpawner(const SafeLibevPtr &_libev,
		const ResourceLocator &_resourceLocator,
		const ServerInstanceDir::GenerationPtr &_generation,
		const vector<string> &_preloaderCommand,
		const Options &_options,
		const RandomGeneratorPtr &_randomGenerator = RandomGeneratorPtr())
		: Spawner(_resourceLocator),
		  libev(_libev),
		  preloaderCommand(_preloaderCommand)
	{
		if (preloaderCommand.size() < 2) {
			throw ArgumentException("preloaderCommand must have at least 2 elements");
		}
		
		forwardStdout = true;
		forwardStderr = true;
		
		generation = _generation;
		options    = _options.copyAndPersist();
		pid        = -1;
		m_lastUsed = SystemTime::getUsec();
		
		preloaderOutputWatcher.set<SmartSpawner, &SmartSpawner::onPreloaderOutputReadable>(this);

		if (_randomGenerator == NULL) {
			randomGenerator = make_shared<RandomGenerator>();
		} else {
			randomGenerator = _randomGenerator;
		}
	}
	
	virtual ~SmartSpawner() {
		lock_guard<boost::mutex> l(syncher);
		stopPreloader();
	}
	
	virtual ProcessPtr spawn(const Options &options) {
		TRACE_POINT();
		assert(options.appType == this->options.appType);
		assert(options.appRoot == this->options.appRoot);
		
		P_DEBUG("Spawning new process: appRoot=" << options.appRoot);
		possiblyRaiseInternalError(options);

		{
			lock_guard<boost::mutex> l(simpleFieldSyncher);
			m_lastUsed = SystemTime::getUsec();
		}
		if (!preloaderStarted()) {
			startPreloader();
		}
		
		SpawnResult result;
		try {
			result = sendSpawnCommand(options);
		} catch (const SystemException &e) {
			result = sendSpawnCommandAgain(e, options);
		} catch (const IOException &e) {
			result = sendSpawnCommandAgain(e, options);
		} catch (const SpawnException &e) {
			result = sendSpawnCommandAgain(e, options);
		}
		
		NegotiationDetails details;
		details.libev = libev;
		details.pid = result.pid;
		details.adminSocket = result.adminSocket;
		details.io = result.io;
		details.options = &options;
		details.forwardStderr = forwardStderr;
		ProcessPtr process = negotiateSpawn(details);
		P_DEBUG("Process spawning done: appRoot=" << options.appRoot <<
			", pid=" << process->pid);
		return process;
	}

	virtual bool cleanable() const {
		return true;
	}
	
	virtual void cleanup() {
		{
			lock_guard<boost::mutex> l(simpleFieldSyncher);
			m_lastUsed = SystemTime::getUsec();
		}
		lock_guard<boost::mutex> lock(syncher);
		stopPreloader();
	}

	virtual unsigned long long lastUsed() const {
		lock_guard<boost::mutex> lock(simpleFieldSyncher);
		return m_lastUsed;
	}
	
	pid_t getPreloaderPid() const {
		lock_guard<boost::mutex> lock(simpleFieldSyncher);
		return pid;
	}
};


class DirectSpawner: public Spawner {
private:
	SafeLibevPtr libev;
	
	static int startBackgroundThread(void *(*mainFunction)(void *), void *arg) {
		// Using raw pthread API because we don't want to register such
		// trivial threads on the oxt::thread list.
		pthread_t thr;
		pthread_attr_t attr;
		size_t stack_size = 96 * 1024;
		
		unsigned long min_stack_size;
		bool stack_min_size_defined;
		bool round_stack_size;
		int ret;
		
		#ifdef PTHREAD_STACK_MIN
			// PTHREAD_STACK_MIN may not be a constant macro so we need
			// to evaluate it dynamically.
			min_stack_size = PTHREAD_STACK_MIN;
			stack_min_size_defined = true;
		#else
			// Assume minimum stack size is 128 KB.
			min_stack_size = 128 * 1024;
			stack_min_size_defined = false;
		#endif
		if (stack_size != 0 && stack_size < min_stack_size) {
			stack_size = min_stack_size;
			round_stack_size = !stack_min_size_defined;
		} else {
			round_stack_size = true;
		}
		
		if (round_stack_size) {
			// Round stack size up to page boundary.
			long page_size;
			#if defined(_SC_PAGESIZE)
				page_size = sysconf(_SC_PAGESIZE);
			#elif defined(_SC_PAGE_SIZE)
				page_size = sysconf(_SC_PAGE_SIZE);
			#elif defined(PAGESIZE)
				page_size = sysconf(PAGESIZE);
			#elif defined(PAGE_SIZE)
				page_size = sysconf(PAGE_SIZE);
			#else
				page_size = getpagesize();
			#endif
			if (stack_size % page_size != 0) {
				stack_size = stack_size - (stack_size % page_size) + page_size;
			}
		}
		
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, 1);
		pthread_attr_setstacksize(&attr, stack_size);
		ret = pthread_create(&thr, &attr, mainFunction, arg);
		pthread_attr_destroy(&attr);
		return ret;
	}
	
	static void *detachProcessMain(void *arg) {
		this_thread::disable_syscall_interruption dsi;
		pid_t pid = (pid_t) (long) arg;
		syscalls::waitpid(pid, NULL, 0);
		return NULL;
	}
	
	void detachProcess(pid_t pid) {
		startBackgroundThread(detachProcessMain, (void *) (long) pid);
	}
	
	vector<string> createCommand(const Options &options, shared_array<const char *> &args) const {
		vector<string> startCommandArgs;
		string processTitle;
		string agentsDir = resourceLocator.getAgentsDir();
		vector<string> command;
		
		split(options.getStartCommand(resourceLocator), '\1', startCommandArgs);
		if (startCommandArgs.empty()) {
			throw RuntimeException("No startCommand given");
		}
		if (options.getProcessTitle().empty()) {
			processTitle = startCommandArgs[0];
		} else {
			processTitle = options.getProcessTitle() + ": " + options.appRoot;
		}
		
		if (options.loadShellEnvvars) {
			command.push_back("bash");
			command.push_back("bash");
			command.push_back("-lc");
			command.push_back("exec \"$@\"");
			command.push_back("SpawnPreparerShell");
		} else {
			command.push_back(agentsDir + "/SpawnPreparer");
		}
		command.push_back(agentsDir + "/SpawnPreparer");
		command.push_back(serializeEnvvarsFromPoolOptions(options));
		command.push_back(startCommandArgs[0]);
		command.push_back(processTitle);
		for (unsigned int i = 1; i < startCommandArgs.size(); i++) {
			command.push_back(startCommandArgs[i]);
		}
		
		createCommandArgs(command, args);
		return command;
	}
	
public:
	/** Whether to forward spawned processes' stderr to our stderr. True by default. */
	bool forwardStderr;
	
	DirectSpawner(const SafeLibevPtr &_libev,
		const ResourceLocator &_resourceLocator,
		const ServerInstanceDir::GenerationPtr &_generation,
		const RandomGeneratorPtr &_randomGenerator = RandomGeneratorPtr())
		: Spawner(_resourceLocator),
		  libev(_libev)
	{
		forwardStderr = true;
		generation = _generation;
		if (_randomGenerator == NULL) {
			randomGenerator = make_shared<RandomGenerator>();
		} else {
			randomGenerator = _randomGenerator;
		}
	}
	
	virtual ProcessPtr spawn(const Options &options) {
		TRACE_POINT();
		P_DEBUG("Spawning new process: appRoot=" << options.appRoot);
		possiblyRaiseInternalError(options);

		shared_array<const char *> args;
		vector<string> command = createCommand(options, args);
		SpawnPreparationInfo preparation = prepareSpawn(options);
		SocketPair adminSocket = createUnixSocketPair();
		Pipe errorPipe = createPipe();
		DebugDirPtr debugDir = make_shared<DebugDir>(preparation.uid, preparation.gid);
		pid_t pid;
		
		pid = syscalls::fork();
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
			switchUser(preparation);
			setWorkingDirectory(preparation);
			execvp(args[0], (char * const *) args.get());
			
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
			ScopeGuard guard(boost::bind(nonInterruptableKillAndWaitpid, pid));
			adminSocket.first.close();
			errorPipe.second.close();
			
			NegotiationDetails details;
			details.libev = libev;
			details.stderrCapturer =
				make_shared<BackgroundIOCapturer>(
					errorPipe.first,
					forwardStderr ? STDERR_FILENO : -1);
			details.stderrCapturer->start();
			details.pid = pid;
			details.adminSocket = adminSocket.second;
			details.io = BufferedIO(adminSocket.second);
			details.errorPipe = errorPipe.first;
			details.options = &options;
			details.forwardStderr = forwardStderr;
			details.debugDir = debugDir;
			
			ProcessPtr process = negotiateSpawn(details);
			detachProcess(process->pid);
			guard.clear();
			P_DEBUG("Process spawning done: appRoot=" << options.appRoot <<
				", pid=" << process->pid);
			return process;
		}
	}
};


class DummySpawner: public Spawner {
private:
	boost::mutex lock;
	unsigned int count;
	
public:
	unsigned int concurrency;
	unsigned int spawnTime;
	unsigned int cleanCount;
	
	DummySpawner(const ResourceLocator &resourceLocator)
		: Spawner(resourceLocator)
	{
		count = 0;
		concurrency = 1;
		spawnTime = 0;
		cleanCount = 0;
	}
	
	virtual ProcessPtr spawn(const Options &options) {
		TRACE_POINT();
		possiblyRaiseInternalError(options);

		SocketPair adminSocket = createUnixSocketPair();
		SocketListPtr sockets = make_shared<SocketList>();
		sockets->add("main", "tcp://127.0.0.1:1234", "session", concurrency);
		syscalls::usleep(spawnTime);
		
		lock_guard<boost::mutex> l(lock);
		count++;
		return make_shared<Process>(SafeLibevPtr(),
			(pid_t) count, "gupid-" + toString(count),
			toString(count),
			adminSocket.second, FileDescriptor(), sockets,
			SystemTime::getUsec(), SystemTime::getUsec());
	}

	virtual bool cleanable() const {
		return true;
	}

	virtual void cleanup() {
		cleanCount++;
	}
};

typedef shared_ptr<DummySpawner> DummySpawnerPtr;


class SpawnerFactory {
private:
	SafeLibevPtr libev;
	ResourceLocator resourceLocator;
	ServerInstanceDir::GenerationPtr generation;
	RandomGeneratorPtr randomGenerator;
	
	SpawnerPtr tryCreateSmartSpawner(const Options &options) {
		string dir = resourceLocator.getHelperScriptsDir();
		vector<string> preloaderCommand;
		if (options.appType == "classic-rails") {
			preloaderCommand.push_back(options.ruby);
			preloaderCommand.push_back(dir + "/classic-rails-preloader.rb");
		} else if (options.appType == "rack") {
			preloaderCommand.push_back(options.ruby);
			preloaderCommand.push_back(dir + "/rack-preloader.rb");
		} else {
			return SpawnerPtr();
		}
		return make_shared<SmartSpawner>(libev, resourceLocator,
			generation, preloaderCommand, options,
			randomGenerator);
	}
	
public:
	// Properties for DummySpawner
	unsigned int dummyConcurrency;
	unsigned int dummySpawnerCreationSleepTime;
	unsigned int dummySpawnTime;

	// Properties for SmartSpawner and DirectSpawner.
	bool forwardStderr;

	SpawnerFactory(const SafeLibevPtr &_libev,
		const ResourceLocator &_resourceLocator,
		const ServerInstanceDir::GenerationPtr &_generation,
		const RandomGeneratorPtr &_randomGenerator = RandomGeneratorPtr())
		: libev(_libev),
		  resourceLocator(_resourceLocator),
		  generation(_generation)
	{
		dummyConcurrency = 1;
		dummySpawnerCreationSleepTime = 0;
		dummySpawnTime   = 0;
		forwardStderr    = true;
		if (randomGenerator == NULL) {
			randomGenerator = make_shared<RandomGenerator>();
		} else {
			randomGenerator = _randomGenerator;
		}
	}
	
	virtual ~SpawnerFactory() { }
	
	virtual SpawnerPtr create(const Options &options) {
		if (options.spawnMethod == "smart" || options.spawnMethod == "smart-lv2") {
			SpawnerPtr spawner = tryCreateSmartSpawner(options);
			if (spawner == NULL) {
				spawner = make_shared<DirectSpawner>(libev,
					resourceLocator, generation,
					randomGenerator);
				static_pointer_cast<DirectSpawner>(spawner)->forwardStderr = forwardStderr;
			} else {
				static_pointer_cast<SmartSpawner>(spawner)->forwardStderr = forwardStderr;
			}
			return spawner;
		} else if (options.spawnMethod == "direct" || options.spawnMethod == "conservative") {
			shared_ptr<DirectSpawner> spawner = make_shared<DirectSpawner>(libev, resourceLocator,
				generation, randomGenerator);
			spawner->forwardStderr = forwardStderr;
			return spawner;
		} else if (options.spawnMethod == "dummy") {
			syscalls::usleep(dummySpawnerCreationSleepTime);
			DummySpawnerPtr spawner = make_shared<DummySpawner>(resourceLocator);
			spawner->concurrency = dummyConcurrency;
			spawner->spawnTime   = dummySpawnTime;
			return spawner;
		} else {
			throw ArgumentException("Unknown spawn method '" + options.spawnMethod + "'");
		}
	}
};

typedef shared_ptr<SpawnerFactory> SpawnerFactoryPtr;


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SPAWNER_H_ */
