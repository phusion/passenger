#include <boost/bind.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/thread.hpp>

#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "AccountsDatabase.h"
#include "Account.h"
#include "ServerInstanceDir.h"
#include "LoggingServer.h"
#include "Utils.h"

using namespace oxt;
using namespace Passenger;

static ServerInstanceDirPtr serverInstanceDir;
static ServerInstanceDir::GenerationPtr generation;

static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
}

int
main(int argc, char *argv[]) {
	pid_t  webServerPid     = (pid_t) atoll(argv[1]);
	string tempDir          = argv[2];
	int    generationNumber = atoi(argv[3]);
	string loggingDir       = argv[4];
	uid_t  user             = (uid_t) atoll(argv[5]);
	gid_t  group            = (gid_t) atoll(argv[6]);
	
	/* Become the process group leader so that the watchdog can kill this
	 * app as well as all descendant processes. */
	setpgrp();
	
	ignoreSigpipe();
	setup_syscall_interruption_support();
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	
	// Change process title.
	strncpy(argv[0], "PassengerLoggingAgent", strlen(argv[0]));
	for (int i = 1; i < argc; i++) {
		memset(argv[i], '\0', strlen(argv[i]));
	}
	
	serverInstanceDir = ptr(new ServerInstanceDir(webServerPid, tempDir, false));
	generation = serverInstanceDir->getGeneration(generationNumber);
	accountsDatabase = ptr(new AccountsDatabase());
	
	messageServer = ptr(new MessageServer(generation->getPath() + "/logging.socket", accountsDatabase));
	messageServer->add(ptr(new LoggingServer(loggingDir)));
	messageServerThread = ptr(new oxt::thread(
		boost::bind(&MessageServer::mainLoop, messageServer.get())
	));
	
	// wait until web server is dead
	
	messageServerThread->interrupt_and_join();
	
	return 0;
}
