/* C wrappers for Passenger::HelperServerStarter. */
#ifndef _PASSENGER_HELPER_SERVER_STARTER_H_
#define _PASSENGER_HELPER_SERVER_STARTER_H_

#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
	extern "C" {
#endif


typedef void HelperServerStarter;

typedef enum {
	HSST_APACHE,
	HSST_NGINX
} HelperServerStarterType;

typedef void (*AfterForkCallback)(void *);

HelperServerStarter *helper_server_starter_new(HelperServerStarterType type, char **error_message);
int  helper_server_starter_start(HelperServerStarter *hps,
                                 unsigned int logLevel, pid_t webServerPid,
                                 const char *tempDir, int userSwitching,
                                 const char *defaultUser, uid_t workerUid,
                                 gid_t workerGid, const char *passengerRoot,
                                 const char *rubyCommand, unsigned int maxPoolSize,
                                 unsigned int maxInstancesPerApp,
                                 unsigned int poolIdleTime,
                                 const AfterForkCallback afterFork,
                                 void *callbackArgument,
                                 char **errorMessage);
const char *helper_server_starter_get_request_socket_filename(HelperServerStarter *hps, unsigned int *size);
const char *helper_server_starter_get_request_socket_password(HelperServerStarter *hps, unsigned int *size);
const char *helper_server_starter_get_server_instance_dir(HelperServerStarter *hps);
const char *helper_server_starter_get_generation_dir(HelperServerStarter *hps);
pid_t       helper_server_starter_get_pid(HelperServerStarter *hps);
void        helper_server_starter_free(HelperServerStarter *hps);


#ifdef __cplusplus
	}
#endif

#endif /* _PASSENGER_HELPER_SERVER_STARTER_H_ */
