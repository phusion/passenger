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
	 * @throws SystemError An error occurred while creating the FIFO.
	 * @throws boost::thread_resource_error Something went wrong during
	 *     creation of the thread.
	 */
	ApplicationPoolStatusReporter(StandardApplicationPoolPtr &pool) {
		int ret;
		
		this->pool = pool;
		
		createPassengerTempDir();
		snprintf(filename, sizeof(filename) - 1, "%s/status.fifo",
			getPassengerTempDir().c_str());
		filename[PATH_MAX - 1] = '\0';
		
		do {
			ret = mkfifo(filename, S_IRUSR | S_IWUSR);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1 && errno != EEXIST) {
			int e = errno;
			string message("Cannot create FIFO '");
			message.append(filename);
			message.append("'");
			throw SystemException(message, e);
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
