#include <oxt/thread.hpp>
#include "AgentsStarter.h"
#include "AgentsStarter.hpp"
#include <cerrno>
#include <cstring>

using namespace std;
using namespace boost;
using namespace oxt;


AgentsStarter *
agents_starter_new(AgentsStarterType type, char **error_message) {
	Passenger::AgentsStarter::Type theType;
	if (type == AS_APACHE) {
		theType = Passenger::AgentsStarter::APACHE;
	} else {
		theType = Passenger::AgentsStarter::NGINX;
	}
	return (AgentsStarter *) new Passenger::AgentsStarter(theType);
}

int
agents_starter_start(AgentsStarter *as,
                            unsigned int logLevel, pid_t webServerPid,
                            const char *tempDir, int userSwitching,
                            const char *defaultUser, uid_t workerUid,
                            gid_t workerGid, const char *passengerRoot,
                            const char *rubyCommand, unsigned int maxPoolSize,
                            unsigned int maxInstancesPerApp,
                            unsigned int poolIdleTime,
                            const AfterForkCallback afterFork,
                            void *callbackArgument,
                            char **errorMessage)
{
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	this_thread::disable_syscall_interruption dsi;
	try {
		function<void ()> afterForkFunctionObject;
		
		if (afterFork != NULL) {
			afterForkFunctionObject = boost::bind(afterFork, callbackArgument);
		}
		agentsStarter->start(logLevel, webServerPid, tempDir, userSwitching,
			defaultUser, workerUid, workerGid, passengerRoot, rubyCommand,
			maxPoolSize, maxInstancesPerApp, poolIdleTime, afterForkFunctionObject);
		return 1;
	} catch (const Passenger::SystemException &e) {
		errno = e.code();
		*errorMessage = strdup(e.what());
		return 0;
	} catch (const exception &e) {
		errno = -1;
		*errorMessage = strdup(e.what());
		return 0;
	}
}

const char *
agents_starter_get_request_socket_filename(AgentsStarter *as, unsigned int *size) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	if (size != NULL) {
		*size = agentsStarter->getRequestSocketFilename().size();
	}
	return agentsStarter->getRequestSocketFilename().c_str();
}

const char *
agents_starter_get_request_socket_password(AgentsStarter *as, unsigned int *size) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	if (size != NULL) {
		*size = agentsStarter->getRequestSocketPassword().size();
	}
	return agentsStarter->getRequestSocketPassword().c_str();
}

const char *agents_starter_get_server_instance_dir(AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getServerInstanceDir()->getPath().c_str();
}

const char *agents_starter_get_generation_dir(AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getGeneration()->getPath().c_str();
}

pid_t agents_starter_get_pid(AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getPid();
}

void
agents_starter_free(AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	delete agentsStarter;
}
