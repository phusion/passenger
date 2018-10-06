/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_INSTANCE_DIRECTORY_H_
#define _PASSENGER_INSTANCE_DIRECTORY_H_

#ifdef USE_SELINUX
	#include <selinux/selinux.h>
#endif

#include <boost/shared_ptr.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cassert>
#include <ctime>
#include <string>
#include <Constants.h>
#include <Exceptions.h>
#include <RandomGenerator.h>
#include <FileTools/FileManip.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>
#include <IOTools/IOUtils.h>
#include <SystemTools/SystemTime.h>
#include <jsoncpp/json.h>

namespace Passenger {

using namespace std;


class InstanceDirectory {
public:
	struct CreationOptions {
		string prefix;
		uid_t originalUid;
		bool userSwitching;
		uid_t defaultUid;
		gid_t defaultGid;
		Json::Value properties;

		CreationOptions()
			: prefix("passenger"),
			  originalUid(geteuid()),
			  userSwitching(true),
			  defaultUid(USER_NOT_GIVEN),
			  defaultGid(GROUP_NOT_GIVEN)
			{ }
	};

private:
	const string path;
	bool owner;

	static string createUniquePath(const string &registryDir, const string &prefix) {
		RandomGenerator generator;

		for (int i = 0; i < 250; i++) {
			string suffix = generator.generateAsciiString(7);
			string path = registryDir + "/" + prefix + "." + suffix;
			if (createPath(registryDir, path)) {
				return path;
			}
		}
		throw RuntimeException("Unable to create a unique directory inside "
			" instance registry directory " + registryDir + ", even after 250 tries");
	}

	static bool createPath(const string &registryDir, const string &path) {
		if (mkdir(path.c_str(), parseModeString("u=rwx,g=rx,o=rx")) == -1) {
			if (errno == EEXIST) {
				return false;
			} else {
				int e = errno;
				throw FileSystemException("Cannot create a subdirectory inside"
					" instance registry directory " + registryDir, e, registryDir);
			}
		}
		// Explicitly chmod the directory in case the umask is interfering.
		if (chmod(path.c_str(), parseModeString("u=rwx,g=rx,o=rx")) == -1) {
			int e = errno;
			throw FileSystemException("Cannot set permissions on instance directory " +
				path, e, path);
		}
		// The parent directory may have the setgid bit enabled, so we
		// explicitly chown it.
		if (chown(path.c_str(), geteuid(), getegid()) == -1) {
			int e = errno;
			throw FileSystemException("Cannot change the permissions of the instance "
				"directory " + path, e, path);
		}
		return true;
	}

	void initializeInstanceDirectory(const CreationOptions &options) {
		createPropertyFile(options);
		createWebServerInfoSubdir(options);
		createAgentSocketsSubdir(options);
		createAppSocketsSubdir(options);
		createLockFile();
	}

	bool runningAsRoot(const CreationOptions &options) const {
		return options.originalUid == 0;
	}

	#ifdef USE_SELINUX
		void selinuxRelabel(const string &path, const char *newLabel) {
			security_context_t currentCon;
			string newCon;
			int e;

			if (getfilecon(path.c_str(), &currentCon) == -1) {
				e = errno;
				P_DEBUG("Unable to obtain SELinux context for file " <<
					path <<": " << strerror(e) << " (errno=" << e << ")");
				return;
			}

			P_DEBUG("SELinux context for " << path << ": " << currentCon);

			if (strstr(currentCon, ":object_r:passenger_instance_content_t:") == NULL) {
				goto cleanup;
			}

			newCon = replaceString(currentCon,
				":object_r:passenger_instance_content_t:",
				StaticString(":object_r:") + newLabel + ":");
			P_DEBUG("Relabeling " << path << " to: " << newCon);
			if (setfilecon(path.c_str(), (security_context_t) newCon.c_str()) == -1) {
				e = errno;
				P_WARN("Cannot set SELinux context for " << path <<
					" to " << newCon << ": " << strerror(e) <<
					" (errno=" << e << ")");
				goto cleanup;
			}

			cleanup:
			freecon(currentCon);
		}
	#endif

	void createWebServerInfoSubdir(const CreationOptions &options) {
		makeDirTree(path + "/web_server_info", "u=rwx,g=rx,o=rx");
		#ifdef USE_SELINUX
			// We relabel the directory here instead of using setfscreatecon()
			// for thread-safety. It isn't specified whether InstanceDirectory
			// should be thread-safe, but let's do it this way to prevent
			// future problems.
			selinuxRelabel(path + "/web_server_info",
				"passenger_instance_httpd_dir_t");
		#endif
	}

	void createAgentSocketsSubdir(const CreationOptions &options) {
		if (runningAsRoot(options)) {
			/* The server socket must be accessible by the web server
			 * and by the apps, which may run as complete different users,
			 * so this subdirectory must be world-accessible.
			 */
			makeDirTree(path + "/agents.s", "u=rwx,g=rx,o=rx");
		} else {
			makeDirTree(path + "/agents.s", "u=rwx,g=,o=");
		}
	}

	void createAppSocketsSubdir(const CreationOptions &options) {
		if (runningAsRoot(options)) {
			if (options.userSwitching) {
				/* Each app may be running as a different user,
				 * so the apps.s subdirectory must be world-writable.
				 * However we don't want everybody to be able to know the
				 * sockets' filenames, so the directory is not readable.
				 */
				makeDirTree(path + "/apps.s", "u=rwx,g=wx,o=wx,+t");
			} else {
				/* All apps are running as defaultUser/defaultGroup,
				 * so make defaultUser/defaultGroup the owner and group of the
				 * subdirecory.
				 *
				 * The directory is not readable as a security precaution:
				 * nobody should be able to know the sockets' filenames without
				 * having access to the application pool.
				 */
				makeDirTree(path + "/apps.s", "u=rwx,g=x,o=x",
					options.defaultUid, options.defaultGid);
			}
		} else {
			/* All apps are running as the same user as the web server,
			 * so only allow access for this user.
			 */
			makeDirTree(path + "/apps.s", "u=rwx,g=,o=");
		}
	}

	void createPropertyFile(const CreationOptions &options) {
		Json::Value props;

		props["instance_dir"]["major_version"] = SERVER_INSTANCE_DIR_STRUCTURE_MAJOR_VERSION;
		props["instance_dir"]["minor_version"] = SERVER_INSTANCE_DIR_STRUCTURE_MINOR_VERSION;
		props["instance_dir"]["created_at"] = (Json::Int64) time(NULL);
		props["instance_dir"]["created_at_monotonic_usec"] = (Json::UInt64) SystemTime::getMonotonicUsec();
		props["passenger_version"] = PASSENGER_VERSION;
		props["watchdog_pid"] = (Json::UInt64) getpid();
		props["instance_id"] = generateInstanceId();

		Json::Value::Members members = options.properties.getMemberNames();
		Json::Value::Members::const_iterator it, end = members.end();
		for (it = members.begin(); it != end; it++) {
			props[*it] = options.properties.get(*it, Json::Value());
		}

		createFile(path + "/properties.json", props.toStyledString());
	}

	void createLockFile() {
		createFile(path + "/lock", "");
	}

public:
	InstanceDirectory(const CreationOptions &options)
		: path(createUniquePath(getSystemTempDir(), options.prefix)),
		  owner(true)
	{
		initializeInstanceDirectory(options);
	}

	InstanceDirectory(const CreationOptions &options, const string &registryDir)
		: path(createUniquePath(registryDir, options.prefix)),
		  owner(true)
	{
		initializeInstanceDirectory(options);
	}

	InstanceDirectory(const string &dir)
		: path(dir),
		  owner(false)
		{ }

	~InstanceDirectory() {
		if (owner) {
			destroy();
		}
	}

	void finalizeCreation() {
		assert(owner);
		createFile(path + "/creation_finalized", "");
	}

	// The 'const string &' here is on purpose. The WatchdogLauncher C
	// functions return the string pointer directly.
	const string &getPath() const {
		return path;
	}

	void detach() {
		owner = false;
	}

	bool isOwner() const {
		return owner;
	}

	void destroy() {
		assert(owner);
		owner = false;
		removeDirTree(path);
	}

	static string generateInstanceId() {
		RandomGenerator randomGenerator;
		return integerToHexatri((unsigned long long) time(NULL))
			+ "-" + randomGenerator.generateAsciiString(6)
			+ "-" + randomGenerator.generateAsciiString(6);
	}
};

typedef boost::shared_ptr<InstanceDirectory> InstanceDirectoryPtr;


} // namespace Passenger

#endif /* _PASSENGER_INSTANCE_DIRECTORY_H_ */
