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

class LoggingAgentWatcher: public AgentWatcher {
protected:
	string agentFilename;
	string socketAddress;

	virtual const char *name() const {
		return "Phusion Passenger logging agent";
	}

	virtual string getExeFilename() const {
		return agentFilename;
	}

	virtual void execProgram() const {
		execl(agentFilename.c_str(), "PassengerLoggingAgent", (char *) 0);
	}

	virtual void sendStartupArguments(pid_t pid, FileDescriptor &fd) {
		VariantMap options = agentsOptions;
		options.set("logging_agent_address", wo->loggingAgentAddress);
		options.set("logging_agent_password", wo->loggingAgentPassword);
		options.set("logging_agent_admin_address", wo->loggingAgentAdminAddress);
		options.writeToFd(fd);
	}

	virtual bool processStartupInfo(pid_t pid, FileDescriptor &fd, const vector<string> &args) {
		if (args[0] == "initialized") {
			return true;
		} else {
			return false;
		}
	}

public:
	LoggingAgentWatcher(const WorkingObjectsPtr &wo)
		: AgentWatcher(wo)
	{
		agentFilename = wo->resourceLocator->getAgentsDir() + "/PassengerLoggingAgent";
	}

	virtual void reportAgentsInformation(VariantMap &report) {
		report
			.set("logging_socket_address", wo->loggingAgentAddress)
			.set("logging_socket_password", wo->loggingAgentPassword)
			.set("logging_socket_admin_address", wo->loggingAgentAdminAddress);
	}
};
