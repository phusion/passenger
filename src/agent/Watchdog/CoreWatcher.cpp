/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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

#include <Shared/Fundamentals/Utils.h>
#include <IOTools/MessageIO.h>

class CoreWatcher: public AgentWatcher {
protected:
	string agentFilename;

	virtual const char *name() const {
		return SHORT_PROGRAM_NAME " core";
	}

	virtual string getExeFilename() const {
		return agentFilename;
	}

	virtual void execProgram() const {
		if (getEnvBool("PASSENGER_RUN_CORE_IN_VALGRIND", false)) {
			execlp("valgrind", "valgrind", "--dsymutil=yes", "--track-origins=yes", "--leak-check=full",
				agentFilename.c_str(), "core",
				// Some extra space to allow the child process to change its process title.
				"                                                ", (char *) 0);
		} else {
			execl(agentFilename.c_str(), AGENT_EXE, "core",
				// Some extra space to allow the child process to change its process title.
				"                                                ", (char *) 0);
		}
	}

	virtual void sendStartupArguments(pid_t pid, FileDescriptor &fd) {
		Json::Value config = watchdogSchema->core.translator.translate(
			watchdogConfig->inspectEffectiveValues());

		Json::Value::iterator it, end = wo->extraConfigToPassToSubAgents.end();
		for (it = wo->extraConfigToPassToSubAgents.begin(); it != end; it++) {
			config[it.name()] = *it;
		}

		config["pid_file"] = wo->corePidFile;
		config["watchdog_fd_passing_password"] = wo->fdPassingPassword;
		config["controller_addresses"] = wo->controllerAddresses;
		config["api_server_addresses"] = wo->coreApiServerAddresses;
		config["api_server_authorizations"] = wo->coreApiServerAuthorizations;

		// The special value "-" means "don't set a controller secure headers password".
		if (config["controller_secure_headers_password"].asString() == "-") {
			config.removeMember("controller_secure_headers_password");
		}

		ConfigKit::Store filteredConfig(watchdogSchema->core.schema, config);
		writeScalarMessage(fd, filteredConfig.inspectEffectiveValues().toStyledString());
	}

	virtual bool processStartupInfo(pid_t pid, FileDescriptor &fd, const vector<string> &args) {
		return args[0] == "initialized";
	}

public:
	CoreWatcher(const WorkingObjectsPtr &wo)
		: AgentWatcher(wo)
	{
		agentFilename = Agent::Fundamentals::context->
			resourceLocator->findSupportBinary(AGENT_EXE);
	}

	virtual void reportAgentStartupResult(Json::Value &report) {
		report["core_address"] = wo->controllerAddresses[0].asString();
		report["core_password"] = watchdogConfig->get("controller_secure_headers_password").asString();
	}
};
