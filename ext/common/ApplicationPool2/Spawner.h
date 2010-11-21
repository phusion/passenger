#ifndef _PASSENGER_APPLICATION_POOL_SPAWNER_H_
#define _PASSENGER_APPLICATION_POOL_SPAWNER_H_

#include <string>
#include <map>
#include <vector>
#include <utility>
#include <boost/make_shared.hpp>
#include <boost/shared_array.hpp>
#include <boost/bind.hpp>
#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Options.h>
#include <FileDescriptor.h>
#include <Exceptions.h>
#include <ResourceLocator.h>
#include <StaticString.h>
#include <Utils/BufferedIO.h>
#include <Utils/ScopeGuard.h>
#include <Utils/IOUtils.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


class Spawner {
protected:
	struct UserSwitchingInfo {
		bool switchUser;
		string username;
		uid_t uid;
		gid_t gid;
		int ngroups;
		shared_array<gid_t> gidset;
	};
	
	void createCommandArgs(const vector<string> &command, shared_array<const char *> &args) {
		args.reset(new const char *[command.size()]);
		for (unsigned int i = 1; i < command.size(); i++) {
			args[i - 1] = command[i].c_str();
		}
		args[command.size() - 1] = NULL;
	}
	
	UserSwitchingInfo prepareUserSwitching(const Options &options) {
		UserSwitchingInfo info;
		// TODO
		return info;
	}
	
	map<string, string> prepareEnvironmentVariablesFromPool(const Options &options) {
		// $USER, $LOGNAME, $SHELL, $PYTHONUNBUFFERED=1
		// TODO
		return map<string, string>();
	}
	
	void resetSignalHandlers() {
		
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
		// TODO
	}
	
	void setEnvironmentVariables(const map<string, string> &vars) {
		
	}
	
public:
	virtual ~Spawner() { }
	virtual ProcessPtr spawn(const Options &options) = 0;
	
	virtual unsigned long long lastUsed() const {
		return 0;
	}
};
typedef shared_ptr<Spawner> SpawnerPtr;

#if 0
class SmartSpawner: public Spawner, public enable_shared_from_this<SmartSpawner> {
private:
	vector<string> command;
	PoolOptions options;
	
	boost::mutex syncher;
	pid_t pid;
	FileDescriptor adminSocket;
	string socketFilename;
	unsigned long long lastUsed;
	
	bool serverStarted() const {
		return pid != -1;
	}
	
	void processStartReply(int fd) {
		MessageChannel channel(fd);
		
	}
	
	void startServer(const PoolOptions &options) {
		assert(!serverStarted());
		
		shared_array<char *> args;
		UserSwitchingInfo userSwitchingInfo = prepareUserSwitching(options);
		FileDescriptor adminSocket[2] = createUnixSocketPair();
		pid_t pid;
		
		createCommandArgs(command, args);
		
		pid = syscalls::fork();
		if (pid == 0) {
			if (adminSocket[0] != 3) {
				dup2(adminSocket[0], 3);
			}
			closeAllFileDescriptors(3);
			resetSignalHandlers();
			switchUser(userSwitchingInfo);
			execvp(command[0].c_str(), (char * const *) args);
			
			int e = errno;
			fprintf(stderr,
				"*** ERROR ***: Cannot execute PassengerSpawnPreparationAgent (\"%s\"): %s (%d)\n",
				command[0].c_str(),
				strerror(e), e);
			_exit(1);
			
		} else if (pid == -1) {
			int = errno;
			throw SystemException("Cannot fork a new process", e);
			
		} else {
			adminSocket[0].close();
			processStartReply(adminSocket[1]);
			this->pid = pid;
			this->adminSocket = adminSocket[1];
		}
	}
	
	void stopServer() {
		if (!serverStarted()) {
			return;
		}
		if (timedWaitpid(pid, 5000) == 0) {
			P_TRACE(2, "Spawn server did not exit in time, killing it...");
			syscalls::kill(pid, SIGKILL);
			syscalls::waitpid(pid, NULL, 0);
		}
		pid = -1;
		adminSocket.close();
	}
	
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
	
	ProcessPtr sendSpawnCommand(const PoolOptions &options) {
		FileDescriptor fd = connectToUnixServer(socketFilename);
		return negotiateStartup(fd, -1, options);
	}
	
	template<typename Exception>
	void sendSpawnCommandAgain(const Exception &e, const PoolOptions &options) {
		P_WARN("The spawn server seems to have crashed, restarting it and trying again...");
		stopServer();
		startServer();
		sendSpawnCommand(options);
	}
	
public:
	SmartSpawner(const vector<string> &command, const PoolOptions &options) {
		this->command = command;
		this->options = options;
		//this->options.pin();
		pid = -1;
		lastUsed = SystemTime::getUsec();
		command.push_front("/path/to/PassengerSpawnPreparationAgent");
		command.push_front("/path/to/PassengerSpawnPreparationAgent");
	}
	
	virtual ~SmartSpawner() {
		stopServer();
	}
	
	virtual ProcessPtr spawn(const PoolOptions &options) {
		lock_guard<boost::mutex> lock(syncher);
		lastUsed = SystemTime::getUsec();
		if (!serverStarted()) {
			startServer();
		}
		try {
			return sendSpawnCommand(options);
		} catch (const SystemException &e) {
			return sendSpawnCommandAgain(e, options);
		} catch (const IOException &e) {
			return sendSpawnCommandAgain(e, options);
		}
	}
	
	virtual unsigned long long lastUsed() const {
		return lastUsed;
	}
};
#endif


class DirectSpawner: public Spawner {
private:
	ResourceLocator resourceLocator;
	RandomGeneratorPtr randomGenerator;
	
	vector<string> createCommand(const Options &options, shared_array<const char *> &args) {
		vector<string> command;
		string agentsDir = resourceLocator.getAgentsDir();
		agentsDir = "/Users/hongli/Projects/passenger/play/ApplicationPool2";
		string prepAgent = agentsDir + "/spawn-preparer";
		
		command.push_back(agentsDir + "/spawn-preparer");
		command.push_back(agentsDir + "/spawn-preparer");
		command.push_back(agentsDir + "/env-printer");
		
		if (options.appType == "rails") {
			command.push_back(options.ruby);
			command.push_back("Passenger RailsApp");
			command.push_back(agentsDir + "/rails-loader.rb");
		} else if (options.appType == "rack") {
			command.push_back(options.ruby);
			command.push_back("Passenger RackApp");
			command.push_back(agentsDir + "/rack-loader.rb");
		} else if (options.appType == "wsgi") {
			command.push_back("python");
			command.push_back("Passenger RailsApp");
			command.push_back(agentsDir + "/wsgi-loader.py");
		} else {
			throw ArgumentException("Unknown application type '" + options.appType + "'");
		}
		
		createCommandArgs(command, args);
		return command;
	}
	
	void passOptions(int fd, const string &gupid, const Options &options,
		unsigned long long &timeout)
	{
		try {
			writeExact(fd,
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
				writeExact(fd, key + ": " + value + "\n", &timeout);
			}
			writeExact(fd, "\n", &timeout);
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
	
	ProcessPtr handleStartupResponse(BufferedIO &io, pid_t pid, const string &gupid,
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
			
			throw SpawnException("Web application failed to start",
				io.readAll(&timeout), attributes["html"] == "true");
		} else {
			throw IOException("Invalid startup response");
		}
	}
	
	ProcessPtr negotiateStartup(pid_t pid, FileDescriptor &adminSocket, const Options &options) {
		BufferedIO io(adminSocket);
		unsigned long long spawnStartTime = SystemTime::getUsec();
		string gupid = randomGenerator->generateAsciiString(43);
		unsigned long long timeout = 60 * 1000000;
		
		string result = io.readLine(1024, &timeout);
		if (result == "I have control 1.0\n") {
			passOptions(adminSocket, gupid, options, timeout);
			result = io.readLine(1024, &timeout);
			if (result == "Ready\n") {
				return handleStartupResponse(io, pid, gupid,
					spawnStartTime, adminSocket, timeout);
			} else {
				handleErrorResponse(io, result, timeout);
			}
		} else {
			handleErrorResponse(io, result, timeout);
		}
		return ProcessPtr(); // Never reached.
	}
	
public:
	DirectSpawner(const ResourceLocator &_resourceLocator, const RandomGeneratorPtr &_randomGenerator)
		: resourceLocator(_resourceLocator),
		  randomGenerator(_randomGenerator)
		{ }
	
	virtual ProcessPtr spawn(const Options &options) {
		shared_array<const char *> args;
		vector<string> command = createCommand(options, args);
		UserSwitchingInfo userSwitchingInfo = prepareUserSwitching(options);
		SocketPair adminSocket = createUnixSocketPair();
		pid_t pid;
		
		pid = syscalls::fork();
		if (pid == 0) {
			resetSignalHandlers();
			int adminSocketCopy = dup2(adminSocket.first, 3);
			dup2(adminSocketCopy, 0);
			dup2(adminSocketCopy, 1);
			closeAllFileDescriptors(2);
			setWorkingDirectory(options);
			switchUser(userSwitchingInfo);
			execvp(command[0].c_str(), (char * const *) args.get());
			
			int e = errno;
			printf("Error\n\n");
			printf("Cannot execute the spawn preparation tool (\"%s\"): %s (%d)\n",
				command[0].c_str(), strerror(e), e);
			fflush(stdout);
			_exit(1);
			
		} else if (pid == -1) {
			int e = errno;
			throw SystemException("Cannot fork a new process", e);
			
		} else {
			ScopeGuard guard(boost::bind(killAndWaitpid, pid));
			adminSocket.first.close();
			ProcessPtr process = negotiateStartup(pid, adminSocket.second, options);
			guard.clear();
			return process;
		}
	}
};


class SpawnerFactory {
private:
	ResourceLocator resourceLocator;
	RandomGeneratorPtr randomGenerator;
	
	SpawnerPtr createSmartSpawner(const Options &options) {
		#if 0
		if (options.spawnMethod == "conservative" || options.spawnMethod == "direct") {
			return SpawnerPtr();
		}
		
		string key;
		vector<string> command;
		
		if (options.appType == "rails") {
			//RubyInfo info = queryRubyInfo(options.ruby);
			//if (info.supportsFork) {
				command.push_back(options.ruby);
				command.push_back("Passenger SpawnServer: " + options.appRoot());
				command.push_back("/path/to/rails/spawn-server");
			//}
		} else if (options.appType == "rack") {
			//RubyInfo info = queryRubyInfo(options.ruby);
			//if (info.supportsFork) {
				command.push_back(options.ruby);
				command.push_back("Passenger SpawnServer: " + options.appRoot());
				command.push_back("/path/to/rack/spawn-server");
			//}
		}
		
		if (key.empty()) {
			return SpawnerPtr();
		} else {
			return make_shared<SmartSpawner>(command, options);
		}
		#endif
		return SpawnerPtr();
	}
	
public:
	SpawnerFactory(const ResourceLocator &_resourceLocator,
		const RandomGeneratorPtr &randomGenerator = RandomGeneratorPtr())
		: resourceLocator(_resourceLocator)
	{
		if (randomGenerator != NULL) {
			this->randomGenerator = randomGenerator;
		} else {
			this->randomGenerator = make_shared<RandomGenerator>();
		}
	}
	
	virtual ~SpawnerFactory() { }
	
	virtual SpawnerPtr create(const Options &options) {
		SpawnerPtr spawner = createSmartSpawner(options);
		if (spawner) {
			return spawner;
		} else {
			return make_shared<DirectSpawner>(resourceLocator, randomGenerator);
		}
	}
};

typedef shared_ptr<SpawnerFactory> SpawnerFactoryPtr;


#if 0
class AppSpawner {
private:
	struct SpawnServer {
		string key;
		pid_t pid;
		FileDescriptor adminPipe;
		string socketFilename;
		time_t lastUsed;
		
		SpawnServer() {
			pid = -1;
			lastUsed = 0;
		}
		
		~SpawnServer() {
			if (pid != 0) {
				adminPipe.close();
				if (!timedWaitpid(pid, 2000)) {
					kill(pid, SIGKILL);
					waitpid(pid, NULL, 0);
				}
			}
		}
	};
	
	typedef shared_ptr<SpawnServer> SpawnServerPtr;
	typedef map<string, SpawnServerPtr> SpawnServerMap;
	
	/* struct RubyInfo {
		bool supportsFork: 1;
		bool multicoreThreading: 1;
	};
	
	typedef map<string, RubyInfo> RubyInfoMap; */
	
	boost::mutex syncher;
	SpawnServerMap spawnServers;
	RubyInfoMap rubyInfo;
	
	/*
	RubyInfo reallyQueryRubyInfo(const string &ruby) {
		//this_thread::disable_interruption di;
		//this_thread::disable_syscall_interruption dsi;
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
	} */
	
	SpawnServerPtr lookupOrCreateSpawnServer(const PoolOptions &options) {
		if (options.spawnMethod == "conservative") {
			return SpawnServerPtr();
		}
		
		string key;
		vector<string> command;
		
		if (options.appType == "rails") {
			//RubyInfo info = queryRubyInfo(options.ruby);
			//if (info.supportsFork) {
				key = "ruby\0" +
					options.ruby + "\0" +
					options.getAppGroupName() + "\0" +
					options.environment + "\0" +
					options.user + "\0" +
					options.group;
				command.push_back(options.ruby);
				command.push_back("Passenger SpawnServer: " + options.appRoot());
				command.push_back("/path/to/rails/spawn-server");
			//}
		} else if (options.appType == "rack") {
			//RubyInfo info = queryRubyInfo(options.ruby);
			//if (info.supportsFork) {
				key = "rack\0" + options.ruby + "\0" + options.getAppGroupName() +
					"\0" + options.environment;
				command.push_back(options.ruby);
				command.push_back("Passenger SpawnServer: " + options.appRoot());
				command.push_back("/path/to/rack/spawn-server");
			//}
		}
		
		if (key.empty()) {
			return SpawnServerPtr();
		} else {
			SpawnServerPtr server;
			SpawnServerMap::iterator it = spawnServers.find(key);
			if (it == spawnServers.end()) {
				server = createSpawnServer(key, command, options);
				spawnServers.insert(make_pair(key, server));
			} else {
				server = it->second;
			}
			server->lastUsed = time(NULL);
			return server;
		}
	}
	
	SpawnServerPtr createSpawnServer(const string &key, const vector<string> &command,
		const PoolOptions &options)
	{
		SpawnServerPtr server = make_shared<SpawnServer>();
		server->key = key;
		
		const char *file = command[0].c_str();
		char args[command.size()];
		for (unsigned int i = 1; i < command.size(); i++) {
			args[i - 1] = command[i].c_str();
		}
		args[command.size() - 1] = NULL;
		
		FileDescriptor adminSocket[2] = createUnixSocketPair();
		pid_t pid;
		
		pid = fork();
		if (pid == 0) {
			execvp(file, (char * const *) args);
			_exit(1);
		} else if (pid == -1) {
			throw;
		} else {
			server->pid = pid;
			return server;
		}
	}
	
	ProcessPtr spawnWithSpawnServer(const SpawnServerPtr &spawnServer, PoolOptions options) {
		return ProcessPtr();
	}
	
	ProcessPtr spawnDirectly(const PoolOptions &options) {
		return ProcessPtr();
	}
	
public:
	virtual ProcessPtr spawn(const PoolOptions &options) {
		unique_lock<boost::mutex> lock(syncher);
		SpawnServerPtr spawnServer = lookupOrCreateSpawnServer(options);
		if (spawnServer != NULL) {
			return spawnWithSpawnServer(spawnServer, options);
		} else {
			return spawnDirectly(options);
		}
	}
};
#endif


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SPAWNER_H_ */
