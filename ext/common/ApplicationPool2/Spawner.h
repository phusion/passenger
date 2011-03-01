#ifndef _PASSENGER_APPLICATION_POOL_SPAWNER_H_
#define _PASSENGER_APPLICATION_POOL_SPAWNER_H_

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
	
	void sendSpawnRequest(int connection, const string &gupid, const Options &options,
		unsigned long long &timeout)
	{
		try {
			writeExact(connection,
				"You have control 1.0\n"
				"passenger_version: " PASSENGER_VERSION "\n"
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
	
	void createCommandArgs(const vector<string> &command, shared_array<const char *> &args) {
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
	
	map<string, string> prepareEnvironmentVariablesFromPool(const Options &options,
		const UserSwitchingInfo &info)
	{
		map<string, string> result;
		
		result["USER"] = info.username;
		result["LOGNAME"] = info.username;
		result["SHELL"] = info.shell;
		result["PYTHONUNBUFFERED"] = "1";
		result["HOME"] = info.home;
		
		if (options.environmentVariables != NULL) {
			vector<string> strings = *options.environmentVariables->getItems();
			for (unsigned int i = 0; i < strings.size(); i += 2) {
				result[strings[i]] = strings[i + 1];
			}
		}
		
		return result;
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
		}
	}
	
	void setEnvironmentVariables(const map<string, string> &vars) {
		map<string, string>::const_iterator it, end = vars.end();
		for (it = vars.begin(); it != end; it++) {
			setenv(it->first.c_str(), it->second.c_str(), 1);
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
	ResourceLocator resourceLocator;
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
	
	vector<string> createRealPreloaderCommand(shared_array<const char *> &args) {
		vector<string> command;
		
		if (options.loadShellEnvvars) {
			string agentsDir = resourceLocator.getAgentsDir();
			command.push_back(agentsDir + "/SpawnPreparer");
			command.push_back(agentsDir + "/SpawnPreparer");
			command.push_back(agentsDir + "/EnvPrinter");
		}
		command.push_back(preloaderCommand[0]);
		command.push_back("Passenger AppPreloader: " + options.appRoot);
		for (unsigned int i = 1; i < preloaderCommand.size(); i++) {
			command.push_back(preloaderCommand[i]);
		}
		
		createCommandArgs(command, args);
		return command;
	}
	
	bool serverStarted() const {
		return pid != -1;
	}
	
	void startServer() {
		assert(!serverStarted());
		
		shared_array<const char *> args;
		vector<string> command = createRealPreloaderCommand(args);
		UserSwitchingInfo userSwitchingInfo = prepareUserSwitching(options);
		map<string, string> envvars = prepareEnvironmentVariablesFromPool(options, userSwitchingInfo);
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
			setEnvironmentVariables(envvars);
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
	
	void stopServer() {
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		
		if (!serverStarted()) {
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
				"passenger_version: " PASSENGER_VERSION "\n"
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
				throw;
			}
		}
	}
	
	string handleStartupResponse(BufferedIO &io, unsigned long long &timeout) {
		string socketAddress;
		
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
				socketAddress = value;
			} else {
				throw IOException("Unknown key '" + key + "'");
			}
		}
		
		if (socketAddress.empty()) {
			throw RuntimeException("Preloader application did not report a socket address");
		}
		
		return socketAddress;
	}
	
	void handleErrorResponse(BufferedIO &io, const string &line, unsigned long long &timeout) {
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
			
			throw SpawnException("Application preloader failed to start",
				io.readAll(&timeout), attributes["html"] == "true");
		} else {
			throw IOException("Invalid startup response: \"" +
				cEscapeString(line) + "\"");
		}
	}
	
	string negotiateStartup(FileDescriptor &fd) {
		BufferedIO io(fd);
		unsigned long long timeout = 60 * 1000000;
		
		string result = io.readLine(1024, &timeout);
		if (result == "I have control 1.0\n") {
			sendStartupRequest(fd, timeout);
			result = io.readLine(1024, &timeout);
			if (result == "Ready\n") {
				return handleStartupResponse(io, timeout);
			} else {
				handleErrorResponse(io, result, timeout);
			}
		} else {
			handleErrorResponse(io, result, timeout);
		}
		return ""; // Never reached.
	}
	
	SpawnResult sendSpawnCommand(const Options &options) {
		FileDescriptor fd = connectToServer(socketAddress);
		MessageChannel channel(fd);
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
			adminSocket = channel.readFileDescriptor(false);
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
		stopServer();
		startServer();
		ScopeGuard guard(boost::bind(&SmartSpawner::stopServer, this));
		SpawnResult result = sendSpawnCommand(options);
		guard.clear();
		return result;
	}
	
	/*
	RubyInfo reallyQueryRubyInfo(const string &ruby) {
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		FileDescriptor pipe[2] = createPipe();
		pid_t pid = syscalls::fork();
		if (pid == 0) {
			dup2(pipe[1], 1);
			closeAllFileDescriptors(2);
			execlp(ruby.c_str(), ruby.c_str(),
				"/path/to/ruby/info/script",
				(char *) 0);
			
			int e = errno;
			fprintf(stderr, "***ERROR***: Cannot exec(\"%s\"): %s (%d)",
				ruby.c_str(), strerror(e), e);
			fflush(stderr);
			_exit(1);
		} else if (pid == -1) {
			int e = errno;
			throw SystemException("Cannot fork a new process", e);
		} else {
			ScopeGuard guard(boost::bind(killAndWaitpid, pid));
			pipe[1].close();
			string data;
			{
				//this_thread::restore_interruption ri(di);
				//this_thread::restore_interruption rsi(dsi);
				// Spend up to 1 minute executing this script.
				// Cold booting JRuby can take quite some time.
				bool ret = readAllData(pipe[0], data, 60000);
				if (!ret) {
					throw RuntimeException(
						"Unable to query the capabilities of Ruby "
						"interpreter '" + ruby + "': "
						"the info query script failed to finish "
						"within 1 minute");
				}
			}
			if (data.size() != 2) {
				throw RuntimeException(
					"Unable to query the capabilities of Ruby "
					"interpreter '" + ruby + "': "
					"the info query script returned " +
					toString(data.size()) + " bytes "
					"instead of the expected 2 bytes");
			}
			
			RubyInfo info;
			info.supportsFork = data[0] == '1';
			info.multicoreThreading = data[1] == '1';
			return info;
		}
	}
	
	RubyInfo queryRubyInfo(const string &ruby) {
		RubyInfoMap::iterator it = rubyInfo.find(ruby);
		if (it == rubyInfo.end() || ruby changed) {
			rubyInfo.erase(ruby);
			rubyInfo.insert(make_pair(ruby, reallyQueryRubyInfo(ruby)));
		} else {
			return it->second;
		}
	}
	*/
	
public:
	ev::io preloaderOutputWatcher;
	
	SmartSpawner(SafeLibev *_libev,
		const ResourceLocator &_resourceLocator,
		const vector<string> &_preloaderCommand,
		const RandomGeneratorPtr &_randomGenerator,
		const Options &_options)
		: resourceLocator(_resourceLocator),
		  preloaderCommand(_preloaderCommand),
		  randomGenerator(_randomGenerator)
	{
		if (preloaderCommand.size() < 2) {
			throw ArgumentException("preloaderCommand must have at least 2 elements");
		}
		
		libev = _libev;
		options = _options.copyAndPersist();
		pid = -1;
		m_lastUsed = SystemTime::getUsec();
		
		preloaderOutputWatcher.set<SmartSpawner, &SmartSpawner::onPreloaderOutputReadable>(this);
	}
	
	virtual ~SmartSpawner() {
		lock_guard<boost::mutex> lock(syncher);
		stopServer();
	}
	
	virtual ProcessPtr spawn(const Options &options) {
		assert(options.appType == this->options.appType);
		assert(options.appRoot == this->options.appRoot);
		assert(options.spawnMethod == "smart");
		
		lock_guard<boost::mutex> lock(syncher);
		m_lastUsed = SystemTime::getUsec();
		if (!serverStarted()) {
			startServer();
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
	ResourceLocator resourceLocator;
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
	
	vector<string> createCommand(const Options &options, shared_array<const char *> &args) {
		vector<string> startCommand;
		string processTitle;
		
		split(options.getStartCommand(resourceLocator), '\1', startCommand);
		if (startCommand.empty()) {
			throw RuntimeException("No startCommand given");
		}
		if (options.getProcessTitle().empty()) {
			processTitle = startCommand[0];
		} else {
			processTitle = options.getProcessTitle() + ": " + options.appRoot;
		}
		
		vector<string> command;
		
		if (options.loadShellEnvvars) {
			string agentsDir = resourceLocator.getAgentsDir();
			command.push_back(agentsDir + "/SpawnPreparer");
			command.push_back(agentsDir + "/SpawnPreparer");
			command.push_back(agentsDir + "/EnvPrinter");
		}
		command.push_back(startCommand[0]);
		command.push_back(processTitle);
		for (unsigned int i = 1; i < startCommand.size(); i++) {
			command.push_back(startCommand[i]);
		}
		
		createCommandArgs(command, args);
		return command;
	}
	
public:
	DirectSpawner(const ResourceLocator &_resourceLocator,
		const RandomGeneratorPtr &_randomGenerator = RandomGeneratorPtr())
		: resourceLocator(_resourceLocator),
		  randomGenerator(_randomGenerator)
	{
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
		map<string, string> envvars = prepareEnvironmentVariablesFromPool(options, userSwitchingInfo);
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
			setEnvironmentVariables(envvars);
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
	DummySpawner() {
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
			preloaderCommand, randomGenerator, options);
	}
	
public:
	SpawnerFactory(SafeLibev *_libev,
		const ResourceLocator &_resourceLocator,
		const RandomGeneratorPtr &randomGenerator = RandomGeneratorPtr())
		: libev(_libev),
		  resourceLocator(_resourceLocator)
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
				spawner = make_shared<DirectSpawner>(resourceLocator, randomGenerator);
			}
			return spawner;
		} else if (options.spawnMethod == "direct" || options.spawnMethod == "conservative") {
			return make_shared<DirectSpawner>(resourceLocator, randomGenerator);
		} else if (options.spawnMethod == "dummy") {
			return make_shared<DummySpawner>();
		} else {
			throw ArgumentException("Unknown spawn method '" + options.spawnMethod + "'");
		}
	}
};

typedef shared_ptr<SpawnerFactory> SpawnerFactoryPtr;


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SPAWNER_H_ */
