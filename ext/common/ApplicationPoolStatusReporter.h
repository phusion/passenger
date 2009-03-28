/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL_STATUS_REPORTER_H_
#define _PASSENGER_APPLICATION_POOL_STATUS_REPORTER_H_

#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <oxt/thread.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/system_calls.hpp>

#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdio>
#include <unistd.h>
#include <errno.h>

#include "StandardApplicationPool.h"
#include "MessageChannel.h"
#include "Logging.h"
#include "Utils.h"

namespace Passenger {

using namespace boost;
using namespace oxt;
using namespace std;

/**
 * An ApplicationPoolStatusReporter allows commandline admin tools to inspect
 * the status of a StandardApplicationPool. It does so by creating a FIFO
 * in the Passenger temp folder.
 *
 * An ApplicationPoolStatusReporter creates a background thread, which
 * continuously sends new information through the FIFO. This thread will
 * be automatically cleaned up upon destroying the ApplicationPoolStatusReporter
 * object.
 */
class ApplicationPoolStatusReporter {
private:
	/** The application pool to monitor. */
	StandardApplicationPoolPtr pool;
	
	/** The FIFO's filename. */
	char filename[PATH_MAX];
	
	/** The background thread. */
	oxt::thread *thr;
	
	void threadMain() {
		TRACE_POINT();
		try {
			while (!this_thread::interruption_requested()) {
				struct stat buf;
				int ret;
				
				UPDATE_TRACE_POINT();
				do {
					ret = stat(filename, &buf);
				} while (ret == -1 && errno == EINTR);
				if (ret == -1 || !S_ISFIFO(buf.st_mode)) {
					// Something bad happened with the status
					// report FIFO, so we bail out.
					break;
				}
				
				UPDATE_TRACE_POINT();
				FILE *f = syscalls::fopen(filename, "w");
				if (f == NULL) {
					int e = errno;
					P_ERROR("Cannot open status report FIFO " <<
						filename << ": " <<
						strerror(e) << " (" << e << ")");
					break;
				}
				
				UPDATE_TRACE_POINT();
				MessageChannel channel(fileno(f));
				string report;
				report.append("----------- Backtraces -----------\n");
				report.append(oxt::thread::all_backtraces());
				report.append("\n\n");
				report.append(pool->toString());
				
				UPDATE_TRACE_POINT();
				try {
					channel.writeScalar(report);
					channel.writeScalar(pool->toXml());
				} catch (...) {
					// Ignore write errors.
				}
				syscalls::fclose(f);
			}
		} catch (const boost::thread_interrupted &) {
			P_TRACE(2, "Status report thread interrupted.");
		}
	}

public:
	/**
	 * Creates a new ApplicationPoolStatusReporter.
	 *
	 * @param pool The application pool to monitor.
	 * @param userSwitching Whether user switching is enabled. This is used
	 *                      for determining the optimal permissions for the
	 *                      FIFO file and the temp directory that might get
	 *                      created.
	 * @param permissions The permissions with which the FIFO should
	 *                    be created.
	 * @param uid The UID of the user who should own the FIFO file, or
	 *            -1 if the current user should be set as owner.
	 * @param gid The GID of the user who should own the FIFO file, or
	 *            -1 if the current group should be set as group.
	 * @throws SystemException An error occurred while creating the FIFO.
	 * @throws boost::thread_resource_error Something went wrong during
	 *     creation of the thread.
	 */
	ApplicationPoolStatusReporter(StandardApplicationPoolPtr &pool,
	                              bool userSwitching,
	                              mode_t permissions = S_IRUSR | S_IWUSR,
	                              uid_t uid = -1, gid_t gid = -1) {
		int ret;
		
		this->pool = pool;
		
		createPassengerTempDir(getSystemTempDir(), userSwitching,
			"nobody", geteuid(), getegid());
		
		snprintf(filename, sizeof(filename) - 1, "%s/info/status.fifo",
			getPassengerTempDir().c_str());
		filename[PATH_MAX - 1] = '\0';
		
		do {
			ret = mkfifo(filename, permissions);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1 && errno != EEXIST) {
			int e = errno;
			string message("Cannot create FIFO '");
			message.append(filename);
			message.append("'");
			throw SystemException(message, e);
		}
		
		// It seems that the permissions passed to mkfifo()
		// aren't respected, so here we chmod the file.
		do {
			ret = chmod(filename, permissions);
		} while (ret == -1 && errno == EINTR);
		
		if (uid != (uid_t) -1 && gid != (gid_t) -1) {
			do {
				ret = chown(filename, uid, gid);
			} while (ret == -1 && errno == EINTR);
			if (errno == -1) {
				int e = errno;
				char message[1024];
				
				snprintf(message, sizeof(message) - 1,
					"Cannot set the FIFO file '%s' its owner to %lld and group to %lld",
					filename, (long long) uid, (long long) gid);
				message[sizeof(message) - 1] = '\0';
				throw SystemException(message, e);
			}
		}
		
		thr = new oxt::thread(
			bind(&ApplicationPoolStatusReporter::threadMain, this),
			"Status report thread",
			1024 * 128
		);
	}
	
	~ApplicationPoolStatusReporter() {
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;
		
		thr->interrupt_and_join();
		delete thr;
		
		int ret;
		do {
			ret = unlink(filename);
		} while (ret == -1 && errno == EINTR);
	}
};

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_STATUS_REPORTER_H_ */
