
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
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Options.h>
#include <FileDescriptor.h>
#include <SafeLibev.h>
#include <Exceptions.h>
#include <ResourceLocator.h>
#include <StaticString.h>
#include <Utils/BufferedIO.h>
#include <Utils/ScopeGuard.h>
#include <Utils/Timer.h>
#include <Utils/IOUtils.h>

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
private:
	friend class tut::ApplicationPool2_DirectSpawnerTest;
	friend class tut::ApplicationPool2_SmartSpawnerTest;
	
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
	
	void sendSpawnRequest(int connection, const string &gupid, const Options &options,
		unsigned long long &timeout)
	{
		try {
			writeExact(connection,
				"You have control 1.0\n"
				"passenger_root: " + resourceLocator.getRoot() + "\n"
				"passenger_version: " PASSENGER_VERSION "\n"
				"ruby_libdir: " + resourceLocator.getRubyLibDir() + "\n"
				"generation_dir: " + generation->getPath() + "\n"
				"gupid: " + gupid + "\n",
				&timeout);
			
			vector<string> args;
			vector<string>::const_iterator it, end;
			options.toVector(args);
			for (it = args.begin(); it != args.end(); it++) {
				const string &key = *it;
				it++;
				const string &value = *it;
				writeExact(connection, key + ": " + value + "\n", &timeout);
			}
			writeExact(connection, "\n", &timeout);
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
	
	ProcessPtr handleSpawnResponse(BufferedIO &io, pid_t pid, const string &gupid,
		unsigned long long spawnStartTime, FileDescriptor &adminSocket,
		unsigned long long &timeout)
	{
		SocketListPtr sockets = make_shared<SocketList>();
		while (true) {
			string line = io.readLine(1024 * 4, &timeout);
			if (line.empty()) {
				throw EOFException("Premature end-of-stream");
			} else if (line[line.size() - 1] != '\n') {
				throw IOException("Invalid line: no newline character found");
			} else if (line == "\n") {
				break;
			}
			
			string::size_type pos = line.find(": ");
			if (pos == string::npos) {
				throw IOException("Invalid line: no separator found");
			}
			
			string key = line.substr(0, pos);
			string value = line.substr(pos + 2, line.size() - pos - 3);
			if (key == "socket") {
				// socket: <name>;<address>;<protocol>;<concurrency>
				vector<string> args;
				split(value, ';', args);
				if (args.size() == 4) {
					sockets->add(args[0], args[1], args[2], atoi(args[3]));
				} else {
					throw IOException("Invalid response format for 'socket' option");
				}
			} else {
				throw IOException("Unknown key '" + key + "'");
			}
		}
		
		return make_shared<Process>(pid, gupid, adminSocket, sockets, spawnStartTime);
	}
	
protected:
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
	
	ResourceLocator resourceLocator;
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
	
	static void createCommandArgs(const vector<string> &command,
		shared_array<const char *> &args)
	{
		args.reset(new const char *[command.size()]);
		for (unsigned int i = 1; i < command.size(); i++) {
			args[i - 1] = command[i].c_str();
		}
		args[command.size() - 1] = NULL;
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
		
		appendNullTerminatedKeyValue(result, "PYTHONUNBUFFERED", "1");
		appendNullTerminatedKeyValue(result, "RAILS_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "RACK_ENV", options.environment);
		appendNullTerminatedKeyValue(result, "WSGI_ENV", options.environment);
		if (!options.baseURI.empty() && options.baseURI != "/") {
			appendNullTerminatedKeyValue(result, "RAILS_RELATIVE_URL_ROOT", options.environment);
			appendNullTerminatedKeyValue(result, "RACK_BASE_URI", options.environment);
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
		if (chdir(options.appRoot.c_str()) == -1) {
			int e = errno;
			printf("Error\n\n");
			printf("Cannot change working directory to '%s': %s (%d)\n",
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
			// in the SpawnPreparer because SpawnPreparer mightt
			// be executed by bash, but these environment variables
			// must be set before bash.
			setenv("USER", info.username.c_str(), 1);
			setenv("LOGNAME", info.username.c_str(), 1);
			setenv("SHELL", info.shell.c_str(), 1);
			setenv("HOME", info.home.c_str(), 1);
		}
	}
	
	ProcessPtr negotiateSpawn(pid_t pid, FileDescriptor &adminSocket,
		const RandomGeneratorPtr &randomGenerator, const Options &options)
	{
		BufferedIO io(adminSocket);
		unsigned long long spawnStartTime = SystemTime::getUsec();
		string gupid = randomGenerator->generateAsciiString(43);
		unsigned long long timeout = options.startTimeout * 1000;
		
		string result = io.readLine(1024, &timeout);
		if (result == "I have control 1.0\n") {
			sendSpawnRequest(adminSocket, gupid, options, timeout);
			result = io.readLine(1024, &timeout);
			if (result == "Ready\n") {
				return handleSpawnResponse(io, pid, gupid,
					spawnStartTime, adminSocket, timeout);
			} else {
				handleSpawnErrorResponse(io, result, timeout);
			}
		} else {
			handleSpawnErrorResponse(io, result, timeout);
		}
		return ProcessPtr(); // Never reached.
	}
	
	void handleSpawnErrorResponse(BufferedIO &io, const string &line, unsigned long long &timeout) {
		if (line == "Error\n") {
			map<string, string> attributes;
			
			while (true) {
				string line = io.readLine(1024 * 4, &timeout);
				if (line.empty()) {
					throw EOFException("Premature end-of-stream in error response");
				} else if (line[line.size() - 1] != '\n') {
					throw IOException("Invalid line in error response: no newline character found");
				} else if (line == "\n") {
					break;
				}
				
				string::size_type pos = line.find(": ");
				if (pos == string::npos) {
					throw IOException("Invalid line in error response: no separator found");
				}
				
				string key = line.substr(0, pos);
				string value = line.substr(pos + 2, line.size() - pos - 3);
				attributes[key] = value;
			}
			
			throw SpawnException("Web application failed to start",
				io.readAll(&timeout), attributes["html"] == "true");
		} else {
			throw IOException("Invalid startup response: \"" +
				cEscapeString(line) + "\"");
		}
	}
	
public:
	Spawner(const ResourceLocator &_resourceLocator)
		: resourceLocator(_resourceLocator)
		{ }
	
	virtual ~Spawner() { }
	virtual ProcessPtr spawn(const Options &options) = 0;
	
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
	};
	
	SafeLibev *libev;
	vector<string> preloaderCommand;
	RandomGeneratorPtr randomGenerator;
	Options options;
	
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
		} else {
			write(1, buf, ret);
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
	
	void throwPreloaderSpawnException(const string &msg, SpawnException::ErrorKind errorKind) {
		throw SpawnException(msg, errorKind)
			.setPreloaderCommand(getPreloaderCommandString());
	}
	
	bool preloaderStarted() const {
		return pid != -1;
	}
	
	void startPreloader() {
		assert(!preloaderStarted());
		
		shared_array<const char *> args;
		vector<string> command = createRealPreloaderCommand(options, args);
		UserSwitchingInfo userSwitchingInfo = prepareUserSwitching(options);
		SocketPair adminSocket = createUnixSocketPair();
		pid_t pid;
		
		pid = syscalls::fork();
		if (pid == 0) {
			resetSignalHandlersAndMask();
			int adminSocketCopy = dup2(adminSocket.first, 3);
			dup2(adminSocketCopy, 0);
			dup2(adminSocketCopy, 1);
			closeAllFileDescriptors(2);
			setWorkingDirectory(options);
			switchUser(userSwitchingInfo);
			execvp(command[0].c_str(), (char * const *) args.get());
			
			int e = errno;
			fpurge(stdout);
			fpurge(stderr);
			printf("Error\n\n");
			printf("Cannot execute \"%s\": %s (%d)\n", command[0].c_str(),
				strerror(e), e);
			fprintf(stderr, "Cannot execute \"%s\": %s (%d)\n",
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
			this->socketAddress = negotiateStartup(adminSocket.second);
			this->pid = pid;
			this->adminSocket = adminSocket.second;
			preloaderOutputWatcher.set(adminSocket.second, ev::READ);
			libev->start(preloaderOutputWatcher);
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
		// Delete socket after the process has exited so that it
		// doesn't crash upon deleting a nonexistant file.
		if (getSocketAddressType(socketAddress) == SAT_UNIX) {
			string filename = parseUnixSocketAddress(socketAddress);
			syscalls::unlink(filename.c_str());
		}
		pid = -1;
		socketAddress.clear();
	}
	
	void sendStartupRequest(int fd, unsigned long long &timeout) {
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
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			}
		} catch (const TimeoutException &) {
			throwPreloaderSpawnException("An error occurred while starting up the "
				"preloader: it did not read the startup request message in time.",
				SpawnException::PRELOADER_STARTUP_TIMEOUT);
		}
	}
	
	string handleStartupResponse(BufferedIO &io, unsigned long long &timeout) {
		string socketAddress;
		
		while (true) {
			string line;
			
			try {
				line = io.readLine(1024 * 4, &timeout);
			} catch (const SystemException &e) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			} catch (const TimeoutException &) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader: it did not write a startup response in time.",
					SpawnException::PRELOADER_STARTUP_TIMEOUT);
			}
			
			if (line.empty()) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It unexpected closed the connection while "
					"sending its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			} else if (line[line.size() - 1] != '\n') {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a line without a newline character "
					"in its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			} else if (line == "\n") {
				break;
			}
			
			string::size_type pos = line.find(": ");
			if (pos == string::npos) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a startup response line without "
					"separator.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			}
			
			string key = line.substr(0, pos);
			string value = line.substr(pos + 2, line.size() - pos - 3);
			if (key == "socket") {
				socketAddress = value;
			} else {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent an unknown startup response line "
					"called '" + key + "'.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			}
		}
		
		if (socketAddress.empty()) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It did not report a socket address in its "
				"startup response.",
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
		}
		
		return socketAddress;
	}
	
	void handleErrorResponse(BufferedIO &io, const string &line, unsigned long long &timeout) {
		map<string, string> attributes;
		
		while (true) {
			string line;
			
			try {
				line = io.readLine(1024 * 4, &timeout);
			} catch (const SystemException &e) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			} catch (const TimeoutException &) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader: it did not write a startup response in time.",
					SpawnException::PRELOADER_STARTUP_TIMEOUT);
			}
			
			if (line.empty()) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It unexpected closed the connection while "
					"sending its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			} else if (line[line.size() - 1] != '\n') {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a line without a newline character "
					"in its startup response.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			} else if (line == "\n") {
				break;
			}
			
			string::size_type pos = line.find(": ");
			if (pos == string::npos) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. It sent a startup response line without "
					"separator.",
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			}
			
			string key = line.substr(0, pos);
			string value = line.substr(pos + 2, line.size() - pos - 3);
			attributes[key] = value;
		}
		
		try {
			string message = io.readAll(&timeout);
			throw SpawnException("An error occured while starting up the preloader.",
				message,
				attributes["html"] == "true")
				.setPreloaderCommand(getPreloaderCommandString());
		} catch (const SystemException &e) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It tried to report an error message, but "
				"an I/O error occurred while reading this error message: " +
				e.sys(),
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
		} catch (const TimeoutException &) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. It tried to report an error message, but "
				"it took too much time doing that.",
				SpawnException::PRELOADER_STARTUP_TIMEOUT);
		}
	}
	
	void handleInvalidResponseType(const string &line) {
		throwPreloaderSpawnException("An error occurred while starting up "
			"the preloader. It sent an unknown response type \"" +
			cEscapeString(line) + "\".",
			SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
	}
	
	string negotiateStartup(FileDescriptor &fd) {
		BufferedIO io(fd);
		unsigned long long timeout = 60 * 1000000;
		
		string result;
		try {
			result = io.readLine(1024, &timeout);
		} catch (const SystemException &e) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader. There was an I/O error while reading its "
				"handshake message: " + e.sys(),
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
		} catch (const TimeoutException &) {
			throwPreloaderSpawnException("An error occurred while starting up "
				"the preloader: it did not write a handshake message in time.",
				SpawnException::PRELOADER_STARTUP_TIMEOUT);
		}
		
		if (result == "I have control 1.0\n") {
			sendStartupRequest(fd, timeout);
			try {
				result = io.readLine(1024, &timeout);
			} catch (const SystemException &e) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader. There was an I/O error while reading its "
					"startup response: " + e.sys(),
					SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			} catch (const TimeoutException &) {
				throwPreloaderSpawnException("An error occurred while starting up "
					"the preloader: it did not write a startup response in time.",
					SpawnException::PRELOADER_STARTUP_TIMEOUT);
			}
			if (result == "Ready\n") {
				return handleStartupResponse(io, timeout);
			} else if (result == "Error\n") {
				handleErrorResponse(io, result, timeout);
			} else {
				handleInvalidResponseType(result);
			}
		} else {
			if (result == "Error\n") {
				handleErrorResponse(io, result, timeout);
			} else {
				handleInvalidResponseType(result);
			}
		}
		
		// Never reached, shut up compiler warning.
		abort();
		return "";
	}
	
	SpawnResult sendSpawnCommand(const Options &options) {
		FileDescriptor fd = connectToServer(socketAddress);
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
			FileDescriptor adminSocket;
			
			spawnedPid = atoi(io.readLine(1024, &timeout).c_str());
			if (spawnedPid <= 0) {
				throw IOException("Application preloader returned an invalid PID");
			}
			// TODO: we really should be checking UID
			if (getsid(spawnedPid) != getsid(pid)) {
				throw SecurityException("Application preloader returned a PID that doesn't belong to the same session");
			}
			
			writeExact(fd, "Ready to receive FD\n");
			adminSocket = readFileDescriptor(fd);
			writeExact(fd, "Received FD\n");
			
			SpawnResult result;
			result.pid = spawnedPid;
			result.adminSocket = adminSocket;
			return result;
			
		} else if (result == "Error\n") {
			handleSpawnErrorResponse(io, result, timeout);
			
		} else {
			throw IOException("Invalid spawn command response: \"" +
				cEscapeString(result) + "\"");
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
	
public:
	ev::io preloaderOutputWatcher;
	
	SmartSpawner(SafeLibev *_libev,
		const ResourceLocator &_resourceLocator,
		const ServerInstanceDir::GenerationPtr &_generation,
		const vector<string> &_preloaderCommand,
		const RandomGeneratorPtr &_randomGenerator,
		const Options &_options)
		: Spawner(_resourceLocator),
		  libev(_libev),
		  preloaderCommand(_preloaderCommand),
		  randomGenerator(_randomGenerator)
	{
		if (preloaderCommand.size() < 2) {
			throw ArgumentException("preloaderCommand must have at least 2 elements");
		}
		
		generation = _generation;
		options    = _options.copyAndPersist();
		pid        = -1;
		m_lastUsed = SystemTime::getUsec();
		
		preloaderOutputWatcher.set<SmartSpawner, &SmartSpawner::onPreloaderOutputReadable>(this);
	}
	
	virtual ~SmartSpawner() {
		lock_guard<boost::mutex> lock(syncher);
		stopPreloader();
	}
	
	virtual ProcessPtr spawn(const Options &options) {
		assert(options.appType == this->options.appType);
		assert(options.appRoot == this->options.appRoot);
		assert(options.spawnMethod == "smart" || options.spawnMethod == "smart-lv2");
		
		lock_guard<boost::mutex> lock(syncher);
		m_lastUsed = SystemTime::getUsec();
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
		}
		return negotiateSpawn(result.pid, result.adminSocket, randomGenerator, options);
	}
	
	virtual unsigned long long lastUsed() const {
		lock_guard<boost::mutex> lock(syncher);
		return m_lastUsed;
	}
	
	pid_t getPreloaderPid() const {
		return pid;
	}
};


class DirectSpawner: public Spawner {
private:
	RandomGeneratorPtr randomGenerator;
	
	static void *detachProcessMain(void *arg) {
		this_thread::disable_syscall_interruption dsi;
		pid_t pid = (pid_t) (long) arg;
		syscalls::waitpid(pid, NULL, 0);
		return NULL;
	}
	
	void detachProcess(pid_t pid) {
		// Using raw pthread API because we don't want to register such
		// trivial threads on the oxt::thread list.
		pthread_t thr;
		pthread_attr_t attr;
		size_t stack_size = 64 * 1024;
		
		unsigned long min_stack_size;
		bool stack_min_size_defined;
		bool round_stack_size;
		
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
		pthread_create(&thr, &attr, detachProcessMain, (void *) (long) pid);
		pthread_attr_destroy(&attr);
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
	DirectSpawner(const ResourceLocator &_resourceLocator,
		const ServerInstanceDir::GenerationPtr &_generation,
		const RandomGeneratorPtr &_randomGenerator = RandomGeneratorPtr())
		: Spawner(_resourceLocator)
	{
		generation = _generation;
		if (_randomGenerator == NULL) {
			randomGenerator = make_shared<RandomGenerator>();
		} else {
			randomGenerator = _randomGenerator;
		}
	}
	
	virtual ProcessPtr spawn(const Options &options) {
		assert(options.spawnMethod == "conservative" || options.spawnMethod == "direct");
		
		shared_array<const char *> args;
		vector<string> command = createCommand(options, args);
		UserSwitchingInfo userSwitchingInfo = prepareUserSwitching(options);
		SocketPair adminSocket = createUnixSocketPair();
		pid_t pid;
		
		pid = syscalls::fork();
		if (pid == 0) {
			resetSignalHandlersAndMask();
			int adminSocketCopy = dup2(adminSocket.first, 3);
			dup2(adminSocketCopy, 0);
			dup2(adminSocketCopy, 1);
			closeAllFileDescriptors(2);
			setWorkingDirectory(options);
			switchUser(userSwitchingInfo);
			execvp(args[0], (char * const *) args.get());
			
			int e = errno;
			fpurge(stdout);
			fpurge(stderr);
			printf("Error\n\n");
			printf("Cannot execute \"%s\": %s (%d)\n", command[0].c_str(),
				strerror(e), e);
			fprintf(stderr, "Cannot execute \"%s\": %s (%d)\n",
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
			ProcessPtr process = negotiateSpawn(pid, adminSocket.second,
				randomGenerator, options);
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
	DummySpawner(const ResourceLocator &resourceLocator)
		: Spawner(resourceLocator)
	{
		count = 0;
	}
	
	virtual ProcessPtr spawn(const Options &options) {
		SocketPair adminSocket = createUnixSocketPair();
		SocketListPtr sockets = make_shared<SocketList>();
		sockets->add("main", "tcp://127.0.0.1:1234", "session", 1);
		
		lock_guard<boost::mutex> l(lock);
		count++;
		return make_shared<Process>(count, toString(count), adminSocket.second,
			sockets, SystemTime::getUsec());
	}
};


class SpawnerFactory {
private:
	SafeLibev *libev;
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
		} else if (options.appType == "wsgi") {
			preloaderCommand.push_back("python");
			preloaderCommand.push_back(dir + "/wsgi-preloader.py");
		} else {
			return SpawnerPtr();
		}
		return make_shared<SmartSpawner>(libev, resourceLocator,
			generation, preloaderCommand, randomGenerator, options);
	}
	
public:
	SpawnerFactory(SafeLibev *_libev,
		const ResourceLocator &_resourceLocator,
		const ServerInstanceDir::GenerationPtr &_generation,
		const RandomGeneratorPtr &randomGenerator = RandomGeneratorPtr())
		: libev(_libev),
		  resourceLocator(_resourceLocator),
		  generation(_generation)
	{
		if (randomGenerator != NULL) {
			this->randomGenerator = randomGenerator;
		} else {
			this->randomGenerator = make_shared<RandomGenerator>();
		}
	}
	
	virtual ~SpawnerFactory() { }
	
	virtual SpawnerPtr create(const Options &options) {
		if (options.spawnMethod == "smart" || options.spawnMethod == "smart-lv2") {
			SpawnerPtr spawner = tryCreateSmartSpawner(options);
			if (spawner == NULL) {
				spawner = make_shared<DirectSpawner>(resourceLocator,
					generation, randomGenerator);
			}
			return spawner;
		} else if (options.spawnMethod == "direct" || options.spawnMethod == "conservative") {
			return make_shared<DirectSpawner>(resourceLocator, generation,
				randomGenerator);
		} else if (options.spawnMethod == "dummy") {
			return make_shared<DummySpawner>(resourceLocator);
		} else {
			throw ArgumentException("Unknown spawn method '" + options.spawnMethod + "'");
		}
	}
};

typedef shared_ptr<SpawnerFactory> SpawnerFactoryPtr;


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SPAWNER_H_ */
