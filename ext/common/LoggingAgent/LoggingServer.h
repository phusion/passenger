/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_LOGGING_SERVER_H_
#define _PASSENGER_LOGGING_SERVER_H_

#include <oxt/system_calls.hpp>

#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <grp.h>

#include "../MessageServer.h"
#include "../Logging.h"
#include "../Exceptions.h"
#include "../FileDescriptor.h"
#include "../Utils.h"
#include "../Utils/SystemTime.h"
#include "../Utils/FileHandleGuard.h"
#include "../Utils/Base64.h"

namespace Passenger {

using namespace std;
using namespace oxt;

class LoggingServer: public MessageServer::Handler {
private:
	RandomGenerator randomGenerator;
	string dir;
	string dirPermissions;
	mode_t filePermissions;
	gid_t gid;
	
	string generateUuid() {
		return Base64::encodeForUrl(randomGenerator.generateByteString(32));
	}
	
public:
	LoggingServer(const string &dir, const string &permissions = "u=rwx,g=rx,o=rx", gid_t gid = GROUP_NOT_GIVEN) {
		this->dir = dir;
		this->gid = gid;
		dirPermissions = permissions;
		filePermissions = parseModeString(permissions) & ~(S_IXUSR | S_IXGRP | S_IXOTH);
	}
	
	virtual bool processMessage(MessageServer::CommonClientContext &commonContext,
	                            MessageServer::ClientContextPtr &handlerSpecificContext,
	                            const vector<string> &args)
	{
		if (args[0] == "open log file") {
			if (args.size() != 5) {
				commonContext.channel.write("error",
					"Invalid arguments sent for the 'open log file' command",
					NULL);
				return true;
			}
			
			string groupName = args[1];
			unsigned long long timestamp = atoll(args[2].c_str());
			string nodeName = args[3];
			string category = args[4];
			
			if (timestamp > SystemTime::getUsec()) {
				commonContext.channel.write("error",
					"Timestamp may not be in the future", NULL);
				return true;
			}
			
			string groupDir, nodeDir, filename;
			try {
				AnalyticsLogger::determineGroupAndNodeDir(dir,
					groupName, nodeName, groupDir, nodeDir);
				filename = AnalyticsLogger::determineLogFilename(dir,
					groupName, nodeName, category, timestamp);
			} catch (const ArgumentException &e) {
				commonContext.channel.write("error", e.what(), NULL);
				return true;
			}
			
			FileDescriptor fd;
			int ret;
			
			try {
				makeDirTree(extractDirName(filename), dirPermissions,
					USER_NOT_GIVEN, gid);
			} catch (const FileSystemException &e) {
				commonContext.channel.write("error", e.what(), NULL);
				return true;
			}
			
			try {
				createFile(groupDir + "/group_name.txt", groupName,
					S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
					USER_NOT_GIVEN, GROUP_NOT_GIVEN,
					false);
				if (getFileType(groupDir + "/uuid.txt") == FT_NONEXISTANT) {
					createFile(groupDir + "/uuid.txt", generateUuid(),
						S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
						USER_NOT_GIVEN, GROUP_NOT_GIVEN,
						false);
				}
			} catch (const FileSystemException &e) {
				commonContext.channel.write("error", e.what(), NULL);
				return true;
			}
			
			try {
				createFile(nodeDir + "/node_name.txt", nodeName,
					S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
					USER_NOT_GIVEN, GROUP_NOT_GIVEN,
					false);
				if (getFileType(nodeDir + "/uuid.txt") == FT_NONEXISTANT) {
					createFile(nodeDir + "/uuid.txt", generateUuid(),
						S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
						USER_NOT_GIVEN, GROUP_NOT_GIVEN,
						false);
				}
			} catch (const FileSystemException &e) {
				commonContext.channel.write("error", e.what(), NULL);
				return true;
			}
			
			fd = syscalls::open(filename.c_str(),
				O_CREAT | O_WRONLY | O_APPEND,
				filePermissions);
			if (fd == -1) {
				const char *message = strerror(errno);
				commonContext.channel.write("error", message, NULL);
				return true;
			}
			
			FileHandleGuard guard(fd);
			do {
				ret = fchmod(fd, filePermissions);
			} while (ret == -1 && errno == EINTR);
			commonContext.channel.write("ok", NULL);
			commonContext.channel.writeFileDescriptor(fd);
			return true;
		} else {
			return false;
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_LOGGING_SERVER_H_ */
