#include <oxt/thread.hpp>
#include "HelperServerStarter.h"
#include "HelperServerStarter.hpp"
#include <cerrno>
#include <cstring>

using namespace std;
using namespace boost;
using namespace oxt;


HelperServerStarter *
helper_server_starter_new(HelperServerStarterType type, char **error_message) {
	Passenger::HelperServerStarter::Type theType;
	if (type == HSST_APACHE) {
		theType = Passenger::HelperServerStarter::APACHE;
	} else {
		theType = Passenger::HelperServerStarter::NGINX;
	}
	return (HelperServerStarter *) new Passenger::HelperServerStarter(theType);
}

int
helper_server_starter_start(HelperServerStarter *hps,
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
	Passenger::HelperServerStarter *helperServerStarter = (Passenger::HelperServerStarter *) hps;
	this_thread::disable_syscall_interruption dsi;
	try {
		function<void ()> afterForkFunctionObject;
		
		if (afterFork != NULL) {
			afterForkFunctionObject = boost::bind(afterFork, callbackArgument);
		}
		helperServerStarter->start(logLevel, webServerPid, tempDir, userSwitching,
			defaultUser, workerUid, workerGid, passengerRoot, rubyCommand,
			maxPoolSize, maxInstancesPerApp, poolIdleTime, afterForkFunctionObject);
		return 1;
	} catch (const Passenger::SystemException &e) {
		errno = e.code();
		*errorMessage = strdup(e.what());
		return 0;
	} catch (const exception &e) {
		*errorMessage = strdup(e.what());
		return 0;
	}
}

const char *
helper_server_starter_get_request_socket_filename(HelperServerStarter *hps, unsigned int *size) {
	Passenger::HelperServerStarter *helperServerStarter = (Passenger::HelperServerStarter *) hps;
	if (size != NULL) {
		*size = helperServerStarter->getRequestSocketFilename().size();
	}
	return helperServerStarter->getRequestSocketFilename().c_str();
}

const char *
helper_server_starter_get_request_socket_password(HelperServerStarter *hps, unsigned int *size) {
	Passenger::HelperServerStarter *helperServerStarter = (Passenger::HelperServerStarter *) hps;
	if (size != NULL) {
		*size = helperServerStarter->getRequestSocketPassword().size();
	}
	return helperServerStarter->getRequestSocketPassword().c_str();
}

const char *helper_server_starter_get_server_instance_dir(HelperServerStarter *hps) {
	Passenger::HelperServerStarter *helperServerStarter = (Passenger::HelperServerStarter *) hps;
	return helperServerStarter->getServerInstanceDir()->getPath().c_str();
}

const char *helper_server_starter_get_generation_dir(HelperServerStarter *hps) {
	Passenger::HelperServerStarter *helperServerStarter = (Passenger::HelperServerStarter *) hps;
	return helperServerStarter->getGeneration()->getPath().c_str();
}

pid_t helper_server_starter_get_pid(HelperServerStarter *hps) {
	Passenger::HelperServerStarter *helperServerStarter = (Passenger::HelperServerStarter *) hps;
	return helperServerStarter->getPid();
}

void
helper_server_starter_free(HelperServerStarter *hps) {
	Passenger::HelperServerStarter *helperServerStarter = (Passenger::HelperServerStarter *) hps;
	delete helperServerStarter;
}
