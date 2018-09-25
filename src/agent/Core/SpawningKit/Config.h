/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SPAWNING_KIT_CONFIG_H_
#define _PASSENGER_SPAWNING_KIT_CONFIG_H_

#include <oxt/macros.hpp>
#include <boost/shared_array.hpp>
#include <vector>
#include <cstddef>

#include <jsoncpp/json.h>

#include <Constants.h>
#include <StaticString.h>
#include <DataStructures/StringKeyTable.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;


// The following hints are available:
//
// @require_non_empty
// @pass_during_handshake
// @non_confidential
// @only_meaningful_if
// @only_pass_during_handshake_if
//
// - begin hinted parseable class -
class Config {
private:
	boost::shared_array<char> storage;

	Json::Value tableToJson(const StringKeyTable<StaticString> &table) const {
		Json::Value doc(Json::objectValue);
		StringKeyTable<StaticString>::ConstIterator it(table);

		while (*it != NULL) {
			doc[it.getKey().toString()] = it.getValue().toString();
			it.next();
		}

		return doc;
	}

public:
	/**
	 * The app group name that the spawned process shall belong to. SpawningKit does
	 * not use this information directly: it is passed to LoggingKit when logging
	 * app output.
	 *
	 * @hinted_parseable
	 * @require_non_empty
	 * @pass_during_handshake
	 * @non_confidential
	 */
	StaticString appGroupName;

	/**
	 * The root directory of the application to spawn. For example, for Ruby apps, this
	 * is the directory containing config.ru. The startCommand will be invoked from
	 * this directory.
	 *
	 * @hinted_parseable
	 * @require_non_empty
	 * @pass_during_handshake
	 * @non_confidential
	 */
	StaticString appRoot;

	/**
	 * The log level to use.
	 *
	 * @hinted_parseable
	 * @pass_during_handshake
	 * @non_confidential
	 */
	int logLevel;

	/**
	 * Whether the app to be spawned is generic or not. Generic
	 * apps do not have special support for Passenger built in,
	 * nor do we have a wrapper for loading the app.
	 *
	 * For example, Rack and Node.js apps are not considered
	 * generic because we have wrappers for them. Go apps without
	 * special Passenger support built in are considered generic.
	 *
	 * @hinted_parseable
	 * @pass_during_handshake
	 * @non_confidential
	 */
	bool genericApp: 1;

	/**
	 * If the app is not generic (`!genericApp`), then this specifies
	 * whether the app is loaded through a wrapper (true), or whether
	 * the app has special support for Passenger built in and is
	 * started directly (false). The only use for this in SpawningKit
	 * is to better format error messages.
	 *
	 * @hinted_parseable
	 * @only_meaningful_if !config.genericApp
	 * @pass_during_handshake
	 * @non_confidential
	 */
	bool startsUsingWrapper: 1;

	/**
	 * When a wrapper is used to load the application, this field
	 * specifies whether the wrapper is supplied by Phusion or by
	 * a third party. The only use for this in SpawningKit is to better
	 * format error messages.
	 *
	 * @hinted_parseable
	 * @only_meaningful_if !config.genericApp && config.startsUsingWrapper
	 * @pass_during_handshake
	 * @non_confidential
	 */
	bool wrapperSuppliedByThirdParty: 1;

	/**
	 * If the app is not generic (`!genericApp`), then this specifies
	 * whether SpawningKit should find a free port to pass to the app
	 * so that it can listen on that port.
	 * This is always done if the app is generic, but *can* be done
	 * for non-generic apps as well.
	 *
	 * @hinted_parseable
	 * @only_meaningful_if !config.genericApp
	 */
	bool findFreePort: 1;

	/**
	 * Whether to load environment variables set in shell startup
	 * files (e.g. ~/.bashrc) during spawning.
	 *
	 * @hinted_parseable
	 * @pass_during_handshake
	 * @non_confidential
	 */
	bool loadShellEnvvars: 1;

	/**
	 * Set to true if you do not want SpawningKit to remove the
	 * work directory after a spawning operation, which is useful
	 * for debugging. Defaults to false.
	 *
	 * @hinted_parseable
	 */
	bool debugWorkDir: 1;

	/**
	 * The command to run in order to start the app.
	 *
	 * If `genericApp` is true, then the command string must contain '$PORT'.
	 * The command string is expected to start the app on the given port.
	 * SpawningKit will take care of passing an appropriate $PORT value to
	 * the app.
	 *
	 * If `genericApp` is false, then the command string is expected do
	 * either one of these things:
	 * - If there is a wrapper available for the app, then the command string
	 *   is to invoke the wrapper (and `startsUsingWrapper` should be true).
	 * - Otherwise, the command string is to start the app directly, in
	 *   Passenger mode (and `startsUsingWrapper` should be false).
	 *
	 * @hinted_parseable
	 * @require_non_empty
	 * @pass_during_handshake
	 * @non_confidential
	 */
	StaticString startCommand;

	/**
	 * The application's entry point file. If a relative path is given, then it
	 * is relative to the app root. Only meaningful if app is to be loaded through
	 * a wrapper.
	 *
	 * @hinted_parseable
	 * @only_meaningful_if !config.genericApp && config.startsUsingWrapper
	 * @require_non_empty
	 * @pass_during_handshake
	 * @non_confidential
	 */
	StaticString startupFile;

	/**
	 * A process title to set when spawning the application.
	 *
	 * @hinted_parseable
	 * @pass_during_handshake
	 * @non_confidential
	 * @only_pass_during_handshake_if !config.processTitle.empty()
	 */
	StaticString processTitle;

	/**
	 * An application type name, e.g. "ruby" or "nodejs". The only use for this
	 * in SpawningKit is to better format error messages.
	 *
	 * @hinted_parseable
	 * @require_non_empty
	 * @pass_during_handshake
	 * @non_confidential
	 */
	StaticString appType;

	/**
	 * The value to set PASSENGER_APP_ENV/RAILS_ENV/etc to.
	 *
	 * @hinted_parseable
	 * @require_non_empty
	 * @pass_during_handshake
	 * @non_confidential
	 */
	StaticString appEnv;

	/**
	 * The spawn method used for spawning the app, i.e. "smart" or "direct".
	 *
	 * @hinted_parseable
	 * @require_non_empty
	 * @pass_during_handshake
	 * @non_confidential
	 */
	StaticString spawnMethod;

	/**
	 * The base URI on which the app runs. If the app is running on the
	 * root URI, then this value must be "/".
	 *
	 * @hinted_parseable
	 * @require_non_empty
	 * @pass_during_handshake base_uri
	 * @non_confidential
	 */
	StaticString baseURI;

	/**
	 * The user to start run the app as. Only has effect if the current process
	 * is running with root privileges.
	 *
	 * @hinted_parseable
	 * @require_non_empty
	 * @pass_during_handshake
	 * @non_confidential
	 */
	StaticString user;

	/**
	 * The group to start run the app as. Only has effect if the current process
	 * is running with root privileges.
	 *
	 * @hinted_parseable
	 * @require_non_empty
	 * @pass_during_handshake
	 * @non_confidential
	 */
	StaticString group;

	/**
	 * Any environment variables to pass to the application. These will be set
	 * after the OS shell has already done its work, but before the application
	 * is started.
	 *
	 * @hinted_parseable
	 * @pass_during_handshake
	 */
	StringKeyTable<StaticString> environmentVariables;

	/**
	 * Specifies that the app's stdout/stderr output should be written
	 * to the given log file.
	 *
	 * @hinted_parseable
	 * @non_confidential
	 * @pass_during_handshake
	 */
	StaticString logFile;

	/**
	 * The API key of the pool group that the spawned process is to belong to.
	 *
	 * @hinted_parseable
	 * @pass_during_handshake
	 * @only_pass_during_handshake_if !config.apiKey.empty()
	 */
	StaticString apiKey;

	/**
	 * A UUID that's generated on Group initialization, and changes every time
	 * the Group receives a restart command. Allows Union Station to track app
	 * restarts.
	 *
	 * @hinted_parseable
	 * @pass_during_handshake
	 * @only_pass_during_handshake_if !config.groupUuid.empty()
	 */
	StaticString groupUuid;

	/**
	 * Minimum user ID starting from which entering LVE and CageFS is allowed.
	 *
	 * @hinted_parseable
	 */
	unsigned int lveMinUid;

	/**
	 * The file descriptor ulimit that the app should have.
	 * A value of 0 means that the ulimit should not be changed.
	 *
	 * @hinted_parseable
	 * @pass_during_handshake
	 * @non_confidential
	 * @only_pass_during_handshake_if config.fileDescriptorUlimit > 0
	 */
	unsigned int fileDescriptorUlimit;

	/**
	 * The maximum amount of time, in milliseconds, that may be spent
	 * on spawning the process or the preloader.
	 *
	 * @hinted_parseable
	 * @require config.startTimeoutMsec > 0
	 */
	unsigned int startTimeoutMsec;

	/*********************/
	/*********************/

	Config()
		: logLevel(DEFAULT_LOG_LEVEL),
		  genericApp(false),
		  startsUsingWrapper(false),
		  wrapperSuppliedByThirdParty(false),
		  findFreePort(false),
		  loadShellEnvvars(false),
		  debugWorkDir(false),
		  appEnv(P_STATIC_STRING(DEFAULT_APP_ENV)),
		  baseURI(P_STATIC_STRING("/")),
		  lveMinUid(DEFAULT_LVE_MIN_UID),
		  fileDescriptorUlimit(0),
		  startTimeoutMsec(DEFAULT_START_TIMEOUT)
		  /*********************/
		{ }

	void internStrings();
	bool validate(vector<StaticString> &errors) const;
	Json::Value getConfidentialFieldsToPassToApp() const;
	Json::Value getNonConfidentialFieldsToPassToApp() const;
};
// - end hinted parseable class -


#include <Core/SpawningKit/Config/AutoGeneratedCode.h>


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_CONFIG_H_ */
