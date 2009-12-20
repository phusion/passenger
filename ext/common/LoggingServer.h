/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2009 Phusion
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

#include "MessageServer.h"
#include "Logging.h"
#include "SystemTime.h"
#include "Exceptions.h"
#include "FileDescriptor.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace oxt;

class LoggingServer: public MessageServer::Handler {
private:
	string dir;
	
public:
	LoggingServer(const string &dir) {
		this->dir = dir;
	}
	
	virtual bool processMessage(MessageServer::CommonClientContext &commonContext,
	                            MessageServer::ClientContextPtr &handlerSpecificContext,
	                            const vector<string> &args)
	{
		if (args[0] == "open log file") {
			unsigned long long timestamp = atoll(args[1].c_str());
			if (timestamp > SystemTime::getMsec()) {
				commonContext.channel.write("error",
					"Timestamp may not be in the future", NULL);
			}
			
			string filename = TxnLogger::determineLogFilename(dir, timestamp);
			mode_t mode = S_IRUSR | S_IWUSR;
			FileDescriptor fd;
			int ret;
			
			try {
				// TODO: fix permissions
				makeDirTree(extractDirName(filename));
			} catch (const IOException &e) {
				string message = "Cannot create directory " + extractDirName(filename) +
					": " + e.what();
				commonContext.channel.write("error", message.c_str(), NULL);
				return true;
			} catch (const SystemException &e) {
				string message = "Cannot create directory " + extractDirName(filename) +
					": " + e.what();
				commonContext.channel.write("error", message.c_str(), NULL);
				return true;
			}
			
			fd = syscalls::open(filename.c_str(),
				O_CREAT | O_WRONLY | O_APPEND,
				mode);
			if (fd == -1) {
				const char *message = strerror(errno);
				commonContext.channel.write("error", message, NULL);
				return true;
			}
			
			do {
				ret = fchmod(fd, mode);
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
