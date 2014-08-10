/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
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

class HelperAgentWatcher: public AgentWatcher {
protected:
	string helperAgentFilename;
	VariantMap params, report;
	string requestSocketFilename;
	string messageSocketFilename;

	virtual const char *name() const {
		return "Phusion Passenger helper agent";
	}

	virtual string getExeFilename() const {
		return helperAgentFilename;
	}

	virtual void execProgram() const {
		if (hasEnvOption("PASSENGER_RUN_HELPER_AGENT_IN_VALGRIND", false)) {
			execlp("valgrind", "valgrind", "--dsymutil=yes",
				helperAgentFilename.c_str(), (char *) 0);
		} else {
			execl(helperAgentFilename.c_str(), "PassengerHelperAgent", (char *) 0);
		}
	}

	virtual void sendStartupArguments(pid_t pid, FileDescriptor &fd) {
		VariantMap options = agentsOptions;
		params.addTo(options);
		options.writeToFd(fd);
	}

	virtual bool processStartupInfo(pid_t pid, FileDescriptor &fd, const vector<string> &args) {
		if (args[0] == "initialized") {
			requestSocketFilename = args[1];
			messageSocketFilename = args[2];
			return true;
		} else {
			return false;
		}
	}

public:
	HelperAgentWatcher(const WorkingObjectsPtr &wo)
		: AgentWatcher(wo)
	{
		helperAgentFilename = wo->resourceLocator->getAgentsDir() + "/PassengerHelperAgent";

		report
			.set("request_socket_filename",
				agentsOptions.get("request_socket_filename", false,
					wo->generation->getPath() + "/request"))
			.set("request_socket_password",
				agentsOptions.get("request_socket_password", false,
					wo->randomGenerator.generateAsciiString(REQUEST_SOCKET_PASSWORD_SIZE)))
			.set("helper_agent_admin_socket_address",
				agentsOptions.get("helper_agent_admin_socket_address", false,
					"unix:" + wo->generation->getPath() + "/helper_admin"))
			.set("helper_agent_exit_password",
				agentsOptions.get("helper_agent_exit_password", false,
					wo->randomGenerator.generateAsciiString(MESSAGE_SERVER_MAX_PASSWORD_SIZE)));

		params = report;
		params
			.set("logging_agent_address", wo->loggingAgentAddress)
			.set("logging_agent_password", wo->loggingAgentPassword);
	}

	virtual void reportAgentsInformation(VariantMap &report) {
		this->report.addTo(report);
	}
};
