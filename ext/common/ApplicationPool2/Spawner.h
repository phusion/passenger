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
#include <unistd.h>
#include <pthread.h>
#include <limits.h>  // for PTHREAD_STACK_MIN
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Options.h>
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
	friend class tut::ApplicationPool2_DirectSpawnerTest;
	friend class tut::ApplicationPool2_SmartSpawnerTest;
	
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
		oxt::thread thr;
		string data;
		bool stopped;
		
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
					P_WARN("Background I/O capturer error: " <<
						strerror(e) << " (errno=" << e << ")");
					break;
				} else {
					data.append(buf, ret);
					if (target != -1) {
						UPDATE_TRACE_POINT();
						writeExact(target, buf, ret);
					}
				}
			}
		}
		
	public:
		BackgroundIOCapturer(const FileDescriptor &_fd, int _target)
			: fd(_fd),
			  target(_target),
			  thr(boost::bind(&BackgroundIOCapturer::capture, this),
			      "Background I/O capturer", 64 * 1024),
			  stopped(false)
			{ }
		
		~BackgroundIOCapturer() {
			if (!stopped) {
				this_thread::disable_interruption di;
				this_thread::disable_syscall_interruption dsi;
				thr.interrupt_and_join();
			}
		}
		
		const FileDescriptor &getFd() const {
			return fd;
		}
		
		const string &stop() {
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			thr.interrupt_and_join();
			stopped = true;
			return data;
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
			// TODO: merge back to removeDirTree()
			vector<string> command;
			command.push_back("rm");
			command.push_back("rm");
			command.push_back("-rf");
			command.push_back(path);
			spawnProcess(command);
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

		void spawnProcess(vector<string> &command) {
			shared_array<const char *> args;
			Spawner::createCommandArgs(command, args);

			pid_t pid = syscalls::fork();
			if (pid == 0) {
				resetSignalHandlersAndMask();
				closeAllFileDescriptors(2);
				execvp(command[0].c_str(), (char * const *) args.get());
				_exit(1);
			} else if (pid == -1) {
				int e = errno;
				throw SystemException("Cannot fork a new process", e);
			} else {
				syscalls::waitpid(pid, 0, NULL);
			}
		}
	};

	typedef shared_ptr<DebugDir> DebugDirPtr;

	struct UserSwitchingInfo {
		bool switchUser;
		string username;
		string home;
		string shell;
		uid_t uid;
		gid_t gid;
		int ngroups;
		shared_array<gid_t> gidset;
	};
	
	// Structure to be passed to negotiateSpawn().
	// Contains arguments as well as working state.
	struct NegotiationDetails {
		// Arguments.
		SafeLibev *libev;
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
			libev = (SafeLibev *) NULL;
			pid = 0;
			options = NULL;
			forwardStderr = false;
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
		try {
			writeExact(details.adminSocket,
				"You have control 1.0\n"
				"passenger_root: " + resourceLocator.getRoot() + "\n"
				"passenger_version: " PASSENGER_VERSION "\n"
				"ruby_libdir: " + resourceLocator.getRubyLibDir() + "\n"
				"generation_dir: " + generation->getPath() + "\n"
				"gupid: " + details.gupid + "\n"
				"connect_password: " + details.connectPassword + "\n",
				&details.timeout);
			
			vector<string> args;
			vector<string>::const_iterator it, end;
			details.options->toVector(args);
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
				throw;
			}
		}
	}
	
	ProcessPtr handleSpawnResponse(NegotiationDetails &details) {
		SocketListPtr sockets = make_shared<SocketList>();
		while (true) {
			string line;
			
			try {
				line = details.io.readLine(1024 * 4, &details.timeout);
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
		
		return make_shared<Process>(details.libev, details.pid,
			details.gupid, details.connectPassword,
			details.adminSocket, details.errorPipe,
			sockets, details.spawnStartTime, details.forwardStderr);
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
	
	UserSwitchingInfo prepareUserSwitching(const Options &options) {
		UserSwitchingInfo info;
		if (geteuid() != 0) {
			struct passwd *userInfo = getpwuid(geteuid());
			if (userInfo == NULL) {
				throw RuntimeException("Cannot get user database entry for user " +
					getProcessUsername() + "; it looks like your system's " +
					"user database is broken, please fix it.");
			}
			
			info.switchUser = false;
			info.username = userInfo->pw_name;
			info.home = userInfo->pw_dir;
			info.shell = userInfo->pw_shell;
			info.uid = geteuid();
			info.gid = getegid();
			info.ngroups = 0;
			return info;
		}
		
		string defaultGroup;
		string startupFile = options.getStartupFile();
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
		return info;
	}
	
	string serializeEnvvarsFromPoolOptions(const Options &options) const {
		vector< pair<StaticString, StaticString> >::const_iterator it, end;
		string result;
		
		appendNullTerminatedKeyValue(result, "IN_PASSENGER", "1");
		appendNullTerminatedKeyValue(result, "PYTHONUNBUFFERED", "1");
		appendNullTerminatedKeyValue(result, "RAILS_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "RACK_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "WSGI_ENV", options.environment);
		if (!options.baseURI.empty() && options.baseURI != "/") {
			appendNullTerminatedKeyValue(result,
				"RAILS_RELATIVE_URL_ROOT",
				options.environment);
			appendNullTerminatedKeyValue(result,
				"RACK_BASE_URI",
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
	
	void setWorkingDirectory(const Options &options) {
		if (chdir(options.appRoot.c_str()) == 0) {
			setenv("PWD", options.appRoot.c_str(), 1);
		} else {
			int e = errno;
			printf("Error\n\n");
			printf("Cannot change working directory to '%s': %s (errno=%d)\n",
				options.appRoot.c_str(), strerror(e), e);
			fflush(stdout);
			_exit(1);
		}
	}
	
	void switchUser(const UserSwitchingInfo &info) {
		if (info.switchUser) {
			if (setgroups(info.ngroups, info.gidset.get()) == -1) {
				int e = errno;
				throw SystemException("setgroups() failed", e);
			}
			if (setgid(info.gid) == -1) {
				int e = errno;
				throw SystemException("setsid() failed", e);
			}
			if (setuid(info.uid) == -1) {
				int e = errno;
				throw SystemException("setuid() failed", e);
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
	
	void setChroot(const Options &options) {
		if (!options.preexecChroot.empty()) {
			int ret = chroot(options.preexecChroot.c_str());
			if (ret == -1) {
				int e = errno;
				fprintf(stderr, "Cannot chroot() to '%s': %s (errno=%d)\n",
					options.preexecChroot.c_str(),
					strerror(e),
					e);
				fflush(stderr);
				_exit(1);
			}
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
			result = details.io.readLine(1024, &details.timeout);
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
				result = details.io.readLine(1024, &details.timeout);
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
			string line = details.io.readLine(1024 * 4, &details.timeout);
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
	Spawner(const ResourceLocator &_resourceLocator)
		: resourceLocator(_resourceLocator)
		{ }
	
	virtual ~Spawner() { }
	virtual ProcessPtr spawn(const Options &options) = 0;
	
	virtual bool cleanable() const {
		return false;
	}

	virtual void cleanup() { }

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
	
	SafeLibevPtr libev;
	const vector<string> preloaderCommand;
	map<string, string> preloaderAnnotations;
	Options options;
	
	// Protects everything else.
	mutable boost::mutex syncher;
	// Protects m_lastUsed;
	mutable boost::mutex simpleFieldSyncher;

	pid_t pid;
	FileDescriptor adminSocket;
	FileDescriptor errorPipe;
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
	
	void onPreloaderErrorReadable(ev::io &io, int revents) {
		char buf[1024 * 8];
		ssize_t ret;
		
		ret = syscalls::read(errorPipe, buf, sizeof(buf));
		if (ret <= 0) {
			preloaderErrorWatcher.stop();
		} else if (forwardStderr) {
			write(STDERR_FILENO, buf, ret);
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
		UserSwitchingInfo userSwitchingInfo = prepareUserSwitching(options);
		SocketPair adminSocket = createUnixSocketPair();
		Pipe errorPipe = createPipe();
		DebugDirPtr debugDir = make_shared<DebugDir>(userSwitchingInfo.uid, userSwitchingInfo.gid);
		pid_t pid;
		
		pid = syscalls::fork();
		if (pid == 0) {
			setenv("PASSENGER_DEBUG_DIR", debugDir->getPath().c_str(), 1);
			purgeStdio(stdout);
			purgeStdio(stderr);
			resetSignalHandlersAndMask();
			int adminSocketCopy = dup2(adminSocket.first, 3);
			int errorPipeCopy = dup2(errorPipe.second, 4);
			dup2(adminSocketCopy, 0);
			dup2(adminSocketCopy, 1);
			dup2(errorPipeCopy, 2);
			closeAllFileDescriptors(2);
			// TODO: shouldn't we set the working directory after switching user?
			setWorkingDirectory(options);
			setChroot(options);
			switchUser(userSwitchingInfo);
			execvp(command[0].c_str(), (char * const *) args.get());
			
			int e = errno;
			printf("Error\n\n");
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
			
			BackgroundIOCapturerPtr stderrCapturer =
				make_shared<BackgroundIOCapturer>(
					errorPipe.first,
					forwardStderr ? STDERR_FILENO : -1);
			
			this->socketAddress = negotiatePreloaderStartup(adminSocket.second,
				stderrCapturer, debugDir, options);
			this->pid = pid;
			this->adminSocket = adminSocket.second;
			this->errorPipe = errorPipe.first;
			preloaderOutputWatcher.set(adminSocket.second, ev::READ);
			preloaderErrorWatcher.set(errorPipe.first, ev::READ);
			libev->start(preloaderOutputWatcher);
			libev->start(preloaderErrorWatcher);
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
		errorPipe.close();
		if (timedWaitpid(pid, NULL, 5000) == 0) {
			P_TRACE(2, "Spawn server did not exit in time, killing it...");
			syscalls::kill(pid, SIGKILL);
			syscalls::waitpid(pid, NULL, 0);
		}
		libev->stop(preloaderOutputWatcher);
		libev->stop(preloaderErrorWatcher);
		// Delete socket after the process has exited so that it
		// doesn't crash upon deleting a nonexistant file.
		if (getSocketAddressType(socketAddress) == SAT_UNIX) {
			string filename = parseUnixSocketAddress(socketAddress);
			syscalls::unlink(filename.c_str());
		}
		pid = -1;
		socketAddress.clear();
	}
	
	void sendStartupRequest(int fd,
		BackgroundIOCapturerPtr &stderrCapturer,
		const DebugDirPtr &debugDir,
		unsigned long long &timeout)
	{
		try {
			writeExact(fd,
				"You have control 1.0\n"
				"passenger_root: " + resourceLocator.getRoot() + "\n"
				"ruby_libdir: " + resourceLocator.getRubyLibDir() + "\n"
				"passenger_version: " PASSENGER_VERSION "\n"
				"generation_dir: " + generation->getPath() + "\n"
				"app_root: " + options.appRoot + "\n"
				"\n",
				&timeout);
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
					stderrCapturer,
					debugDir);
			}
		} catch (const TimeoutException &) {
			throwPreloaderSpawnException("An error occurred while starting up the "
				"preloader: it did not read the startup request message in time.",
				SpawnException::PRELOADER_STARTUP_TIMEOUT,
				stderrCapturer,
				debugDir);
		}
	}
	
	string handleStartupResponse(BufferedIO &io,
		BackgroundIOCapturerPtr &stderrCapturer,
		const DebugDirPtr &debugDir,
		const Options &options,
		unsigned long long &timeout)
	{
		string socketAddress;
		
		while (true) {
			string line;
			
			try {
				line = io.readLine(1024 * 4, &timeout);
			} catch (const SystemException &e) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					debugDir);
			} catch (const TimeoutException &) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader: it did not write a startup response in time.",
					SpawnException::PRELOADER_STARTUP_TIMEOUT,
					stderrCapturer,
					debugDir);
			}
			
			if (line.empty()) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It unexpected closed the connection while "
					"sending its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					debugDir);
			} else if (line[line.size() - 1] != '\n') {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a line without a newline character "
					"in its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					debugDir);
			} else if (line == "\n") {
				break;
			}
			
			string::size_type pos = line.find(": ");
			if (pos == string::npos) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a startup response line without "
					"separator.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					debugDir);
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
					stderrCapturer,
					debugDir);
			}
		}
		
		if (socketAddress.empty()) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It did not report a socket address in its "
				"startup response.",
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
				stderrCapturer,
				debugDir);
		}
		
		return socketAddress;
	}
	
	void handleErrorResponse(BufferedIO &io,
		BackgroundIOCapturerPtr &stderrCapturer,
		const DebugDirPtr &debugDir,
		unsigned long long &timeout)
	{
		map<string, string> attributes;
		
		while (true) {
			string line;
			
			try {
				line = io.readLine(1024 * 4, &timeout);
			} catch (const SystemException &e) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					debugDir);
			} catch (const TimeoutException &) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader: it did not write a startup response in time.",
					SpawnException::PRELOADER_STARTUP_TIMEOUT,
					stderrCapturer,
					debugDir);
			}
			
			if (line.empty()) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It unexpected closed the connection while "
					"sending its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					debugDir);
			} else if (line[line.size() - 1] != '\n') {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a line without a newline character "
					"in its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					debugDir);
			} else if (line == "\n") {
				break;
			}
			
			string::size_type pos = line.find(": ");
			if (pos == string::npos) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a startup response line without "
					"separator.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					debugDir);
			}
			
			string key = line.substr(0, pos);
			string value = line.substr(pos + 2, line.size() - pos - 3);
			attributes[key] = value;
		}
		
		try {
			string message = io.readAll(&timeout);
			SpawnException e("An error occured while starting up the preloader.",
				message,
				attributes["html"] == "true",
				SpawnException::PRELOADER_STARTUP_EXPLAINABLE_ERROR);
			e.setPreloaderCommand(getPreloaderCommandString());
			annotatePreloaderException(e, debugDir);
			throw e;
		} catch (const SystemException &e) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It tried to report an error message, but "
				"an I/O error occurred while reading this error message: " +
				e.sys(),
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
				stderrCapturer,
				debugDir);
		} catch (const TimeoutException &) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It tried to report an error message, but "
				"it took too much time doing that.",
				SpawnException::PRELOADER_STARTUP_TIMEOUT,
				stderrCapturer,
				debugDir);
		}
	}
	
	void handleInvalidResponseType(const string &line, BackgroundIOCapturerPtr &stderrCapturer,
		const DebugDirPtr &debugDir)
	{
		throwPreloaderSpawnException("An error occurred while starting up "
			"the preloader. It sent an unknown response type \"" +
			cEscapeString(line) + "\".",
			SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
			stderrCapturer,
			debugDir);
	}
	
	string negotiatePreloaderStartup(FileDescriptor &fd,
		BackgroundIOCapturerPtr &stderrCapturer,
		const DebugDirPtr &debugDir,
		const Options &options)
	{
		BufferedIO io(fd);
		unsigned long long timeout = options.startTimeout * 1000;
		
		string result;
		try {
			result = io.readLine(1024, &timeout);
		} catch (const SystemException &e) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. There was an I/O error while reading its "
				"handshake message: " + e.sys(),
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
				stderrCapturer,
				debugDir);
		} catch (const TimeoutException &) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader: it did not write a handshake message in time.",
				SpawnException::PRELOADER_STARTUP_TIMEOUT,
				stderrCapturer,
				debugDir);
		}
		
		if (result == "I have control 1.0\n") {
			sendStartupRequest(fd, stderrCapturer, debugDir, timeout);
			try {
				result = io.readLine(1024, &timeout);
			} catch (const SystemException &e) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR,
					stderrCapturer,
					debugDir);
			} catch (const TimeoutException &) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader: it did not write a startup response in time.",
					SpawnException::PRELOADER_STARTUP_TIMEOUT,
					stderrCapturer,
					debugDir);
			}
			if (result == "Ready\n") {
				return handleStartupResponse(io, stderrCapturer, debugDir, options, timeout);
			} else if (result == "Error\n") {
				handleErrorResponse(io, stderrCapturer, debugDir, timeout);
			} else {
				handleInvalidResponseType(result, stderrCapturer, debugDir);
			}
		} else {
			if (result == "Error\n") {
				handleErrorResponse(io, stderrCapturer, debugDir, timeout);
			} else {
				handleInvalidResponseType(result, stderrCapturer, debugDir);
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
		options.toVector(args);
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
			// TODO: we really should be checking UID
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
	ev::io preloaderOutputWatcher;
	ev::io preloaderErrorWatcher;
	
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
		preloaderErrorWatcher.set<SmartSpawner, &SmartSpawner::onPreloaderErrorReadable>(this);
		
		if (_randomGenerator == NULL) {
			randomGenerator = make_shared<RandomGenerator>();
		} else {
			randomGenerator = _randomGenerator;
		}
	}
	
	virtual ~SmartSpawner() {
		lock_guard<boost::mutex> lock(syncher);
		stopPreloader();
	}
	
	virtual ProcessPtr spawn(const Options &options) {
		assert(options.appType == this->options.appType);
		assert(options.appRoot == this->options.appRoot);
		
		lock_guard<boost::mutex> lock(syncher);
		{
			lock_guard<boost::mutex> lock2(simpleFieldSyncher);
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
		details.libev = libev.get();
		details.pid = result.pid;
		details.adminSocket = result.adminSocket;
		details.io = result.io;
		details.options = &options;
		return negotiateSpawn(details);
	}

	virtual bool cleanable() const {
		return true;
	}
	
	virtual void cleanup() {
		{
			lock_guard<boost::mutex> lock2(simpleFieldSyncher);
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
		pthread_attr_getstacksize(&attr, &stack_size);
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
		shared_array<const char *> args;
		vector<string> command = createCommand(options, args);
		UserSwitchingInfo userSwitchingInfo = prepareUserSwitching(options);
		SocketPair adminSocket = createUnixSocketPair();
		Pipe errorPipe = createPipe();
		DebugDirPtr debugDir = make_shared<DebugDir>(userSwitchingInfo.uid, userSwitchingInfo.gid);
		pid_t pid;
		
		pid = syscalls::fork();
		if (pid == 0) {
			setenv("PASSENGER_DEBUG_DIR", debugDir->getPath().c_str(), 1);
			purgeStdio(stdout);
			purgeStdio(stderr);
			resetSignalHandlersAndMask();
			int adminSocketCopy = dup2(adminSocket.first, 3);
			int errorPipeCopy = dup2(errorPipe.second, 4);
			dup2(adminSocketCopy, 0);
			dup2(adminSocketCopy, 1);
			dup2(errorPipeCopy, 2);
			closeAllFileDescriptors(2);
			// TODO: shouldn't we set the working directory after switching user?
			setWorkingDirectory(options);
			setChroot(options);
			switchUser(userSwitchingInfo);
			execvp(args[0], (char * const *) args.get());
			
			int e = errno;
			printf("Error\n\n");
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
			details.libev = libev.get();
			details.stderrCapturer =
				make_shared<BackgroundIOCapturer>(
					errorPipe.first,
					forwardStderr ? STDERR_FILENO : -1);
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
	
	DummySpawner(const ResourceLocator &resourceLocator)
		: Spawner(resourceLocator)
	{
		count = 0;
		concurrency = 1;
		spawnTime = 0;
	}
	
	virtual ProcessPtr spawn(const Options &options) {
		SocketPair adminSocket = createUnixSocketPair();
		SocketListPtr sockets = make_shared<SocketList>();
		sockets->add("main", "tcp://127.0.0.1:1234", "session", concurrency);
		syscalls::usleep(spawnTime);
		
		lock_guard<boost::mutex> l(lock);
		count++;
		return make_shared<Process>((SafeLibev *) NULL,
			(pid_t) count, string(),
			toString(count),
			adminSocket.second, FileDescriptor(), sockets,
			SystemTime::getUsec());
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
