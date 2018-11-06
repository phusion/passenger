/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_ADMIN_PANEL_CONNECTOR_H_
#define _PASSENGER_ADMIN_PANEL_CONNECTOR_H_

#include <sys/wait.h>
#include <sstream>
#include <unistd.h>

#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include <limits>
#include <string>
#include <vector>

#include <Constants.h>
#include <WebSocketCommandReverseServer.h>
#include <InstanceDirectory.h>
#include <ConfigKit/SchemaUtils.h>
#include <Core/ApplicationPool/Pool.h>
#include <Core/Controller.h>
#include <ProcessManagement/Ruby.h>
#include <FileTools/FileManip.h>
#include <SystemTools/UserDatabase.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>
#include <IOTools/IOUtils.h>
#include <Utils/AsyncSignalSafeUtils.h>
#include <LoggingKit/Context.h>

#include <jsoncpp/json.h>

namespace Passenger {
namespace Core {

using namespace std;
using namespace oxt;
namespace ASSU = AsyncSignalSafeUtils;

class AdminPanelConnector {
public:
	/**
	 * BEGIN ConfigKit schema: Passenger::Core::AdminPanelConnector::Schema
	 * (do not edit: following text is automatically generated
	 * by 'rake configkit_schemas_inline_comments')
	 *
	 *   auth_type                   string    -          default("basic")
	 *   close_timeout               float     -          default(10.0)
	 *   connect_timeout             float     -          default(30.0)
	 *   data_debug                  boolean   -          default(false)
	 *   instance_dir                string    -          read_only
	 *   integration_mode            string    -          default("standalone")
	 *   log_prefix                  string    -          -
	 *   password                    string    -          secret
	 *   password_file               string    -          -
	 *   ping_interval               float     -          default(30.0)
	 *   ping_timeout                float     -          default(30.0)
	 *   proxy_password              string    -          secret
	 *   proxy_timeout               float     -          default(30.0)
	 *   proxy_url                   string    -          -
	 *   proxy_username              string    -          -
	 *   reconnect_timeout           float     -          default(5.0)
	 *   ruby                        string    -          default("ruby")
	 *   standalone_engine           string    -          default
	 *   url                         string    required   -
	 *   username                    string    -          -
	 *   web_server_module_version   string    -          read_only
	 *   web_server_version          string    -          read_only
	 *   websocketpp_debug_access    boolean   -          default(false)
	 *   websocketpp_debug_error     boolean   -          default(false)
	 *
	 * END
	 */
	struct Schema: public WebSocketCommandReverseServer::Schema {
		Schema()
			: WebSocketCommandReverseServer::Schema(false)
		{
			using namespace ConfigKit;

			add("integration_mode", STRING_TYPE, OPTIONAL, DEFAULT_INTEGRATION_MODE);
			addWithDynamicDefault("standalone_engine", STRING_TYPE, OPTIONAL,
				ConfigKit::getDefaultStandaloneEngine);
			add("instance_dir", STRING_TYPE, OPTIONAL | READ_ONLY);
			add("web_server_version", STRING_TYPE, OPTIONAL | READ_ONLY);
			add("web_server_module_version", STRING_TYPE, OPTIONAL | READ_ONLY);
			add("ruby", STRING_TYPE, OPTIONAL, "ruby");

			addValidator(ConfigKit::validateIntegrationMode);
			addValidator(ConfigKit::validateStandaloneEngine);

			finalize();
		}
	};

	typedef WebSocketCommandReverseServer::ConfigChangeRequest ConfigChangeRequest;

	typedef WebSocketCommandReverseServer::ConnectionPtr ConnectionPtr;
	typedef WebSocketCommandReverseServer::MessagePtr MessagePtr;
	typedef boost::function<Json::Value (void)> ConfigGetter;
	typedef vector<Controller*> Controllers;

private:
	WebSocketCommandReverseServer server;
	dynamic_thread_group threads;
	Json::Value globalPropertiesFromInstanceDir;

	bool onMessage(WebSocketCommandReverseServer *server,
		const ConnectionPtr &conn, const MessagePtr &msg)
	{
		Json::Value doc;

		try {
			doc = parseAndBasicValidateMessageAsJSON(msg->get_payload());
		} catch (const RuntimeException &e) {
			Json::Value reply;
			reply["result"] = "error";
			reply["request_id"] = doc["request_id"];
			reply["data"]["message"] = e.what();
			sendJsonReply(conn, reply);
			return true;
		}

		if (doc["action"] == "get") {
			return onGetMessage(conn, doc);
		} else {
			return onUnknownMessageAction(conn, doc);
		}
	}


	bool onGetMessage(const ConnectionPtr &conn, const Json::Value &doc) {
		const string resource = doc["resource"].asString();

		if (resource == "server_properties") {
			return onGetServerProperties(conn, doc);
		} else if (resource == "global_properties") {
			return onGetGlobalProperties(conn, doc);
		} else if (resource == "global_configuration") {
			return onGetGlobalConfiguration(conn, doc);
		} else if (resource == "global_statistics") {
			return onGetGlobalStatistics(conn, doc);
		} else if (resource == "application_properties") {
			return onGetApplicationProperties(conn, doc);
		} else if (resource == "application_configuration") {
			return onGetApplicationConfig(conn, doc);
		} else if (resource == "application_logs") {
			return onGetApplicationLogs(conn, doc);
		} else {
			return onUnknownResource(conn, doc);
		}
	}

	bool onGetServerProperties(const ConnectionPtr &conn, const Json::Value &doc) {
		threads.create_thread(
			boost::bind(&AdminPanelConnector::onGetServerPropertiesBgJob, this,
				conn, doc, server.getConfig()["ruby"].asString()),
			"AdminPanelCommandServer: get_server_properties background job",
			128 * 1024);
		return false;
	}

	void onGetServerPropertiesBgJob(const ConnectionPtr &conn, const Json::Value &doc,
		const string &ruby)
	{
		vector<string> args;
		args.push_back("passenger-config");
		args.push_back("system-properties");

		int status = 0;
		SubprocessOutput output;
		try {
			runInternalRubyTool(*resourceLocator, ruby, args, &status, &output);
		} catch (const std::exception &e) {
			server.getIoService().post(boost::bind(
				&AdminPanelConnector::onGetServerPropertiesDone, this,
				conn, doc, string(), -1, e.what()
			));
			return;
		}

		server.getIoService().post(boost::bind(
			&AdminPanelConnector::onGetServerPropertiesDone, this,
			conn, doc, output.data, status, string()
		));
	}

	void onGetServerPropertiesDone(const ConnectionPtr &conn, const Json::Value &doc,
		const string output, int status, const string &error)
	{
		Json::Value reply;
		reply["request_id"] = doc["request_id"];
		if (error.empty()) {
			if (status == 0 || status == -1) {
				Json::Reader reader;
				Json::Value dataDoc;

				if (output.empty()) {
					reply["result"] = "error";
					reply["data"]["message"] = "Error parsing internal helper tool output";
					P_ERROR(getLogPrefix() << "Error parsing internal helper tool output.\n" <<
						"Raw data: \"\"");
				} else if (reader.parse(output, dataDoc)) {
					reply["result"] = "ok";
					reply["data"] = dataDoc;
				} else {
					reply["result"] = "error";
					reply["data"]["message"] = "Error parsing internal helper tool output";
					P_ERROR(getLogPrefix() << "Error parsing internal helper tool output.\n" <<
						"Error: " << reader.getFormattedErrorMessages() << "\n"
						"Raw data: \"" << cEscapeString(output) << "\"");
				}
			} else {
				int exitStatus = WEXITSTATUS(status);
				reply["result"] = "error";
				reply["data"]["message"] = "Internal helper tool exited with status "
					+ toString(exitStatus);
				P_ERROR(getLogPrefix() << "Internal helper tool exited with status "
					<< exitStatus << ". Raw output: \"" << cEscapeString(output) << "\"");
			}
		} else {
			reply["result"] = "error";
			reply["data"]["message"] = error;
		}
		sendJsonReply(conn, reply);
		server.doneReplying(conn);
	}

	bool onGetGlobalProperties(const ConnectionPtr &conn, const Json::Value &doc) {
		const ConfigKit::Store &config = server.getConfig();
		Json::Value reply, data;
		reply["result"] = "ok";
		reply["request_id"] = doc["request_id"];

		data = globalPropertiesFromInstanceDir;
		data["version"] = PASSENGER_VERSION;
		data["core_pid"] = Json::UInt(getpid());

		string integrationMode = config["integration_mode"].asString();
		data["integration_mode"]["name"] = integrationMode;
		if (!config["web_server_module_version"].isNull()) {
			data["integration_mode"]["web_server_module_version"] = config["web_server_module_version"];
		}
		if (integrationMode == "standalone") {
			data["integration_mode"]["standalone_engine"] = config["standalone_engine"];
		}
		if (!config["web_server_version"].isNull()) {
			data["integration_mode"]["web_server_version"] = config["web_server_version"];
		}

		data["originally_packaged"] = resourceLocator->isOriginallyPackaged();
		if (!resourceLocator->isOriginallyPackaged()) {
			data["packaging_method"] = resourceLocator->getPackagingMethod();
		}

		reply["data"] = data;
		sendJsonReply(conn, reply);
		return true;
	}

	bool onGetGlobalConfiguration(const ConnectionPtr &conn, const Json::Value &doc) {
		threads.create_thread(
			boost::bind(&AdminPanelConnector::onGetGlobalConfigurationBgJob, this,
				conn, doc),
			"AdminPanelCommandServer: get_global_config background job",
			128 * 1024);
		return false;
	}

	void onGetGlobalConfigurationBgJob(const ConnectionPtr &conn, const Json::Value &input) {
		Json::Value globalConfig = configGetter()["config_manifest"]["effective_value"]["global_configuration"];
		server.getIoService().post(boost::bind(
			&AdminPanelConnector::onGetGlobalConfigDone, this,
			conn, input, globalConfig
		));
	}

	void onGetGlobalConfigDone(const ConnectionPtr &conn, const Json::Value &input,
		Json::Value config)
	{
		Json::Value reply;

		reply["result"] = "ok";
		reply["request_id"] = input["request_id"];
		reply["data"]["options"] = config;

		sendJsonReply(conn, reply);
		server.doneReplying(conn);
	}

	bool onGetGlobalStatistics(const ConnectionPtr &conn, const Json::Value &doc) {
		Json::Value reply;
		reply["result"] = "ok";
		reply["request_id"] = doc["request_id"];
		reply["data"]["message"] = Json::arrayValue;

		for (unsigned int i = 0; i < controllers.size(); i++) {
			reply["data"]["message"].append(controllers[i]->inspectStateAsJson());
		}

		sendJsonReply(conn, reply);
		return true;
	}

	bool onGetApplicationProperties(const ConnectionPtr &conn, const Json::Value &doc) {
		ConfigKit::Schema argumentsSchema =
			ApplicationPool2::Pool::ToJsonOptions::createSchema();
		Json::Value args(Json::objectValue), reply;
		ApplicationPool2::Pool::ToJsonOptions inspectOptions =
			ApplicationPool2::Pool::ToJsonOptions::makeAuthorized();

		if (doc.isMember("arguments")) {
			ConfigKit::Store store(argumentsSchema);
			vector<ConfigKit::Error> errors;

			if (store.update(doc["arguments"], errors)) {
				inspectOptions.set(store.inspectEffectiveValues());
			} else {
				reply["result"] = "error";
				reply["request_id"] = doc["request_id"];
				reply["data"]["message"] = "Invalid arguments: " +
					ConfigKit::toString(errors);
				sendJsonReply(conn, reply);
				return true;
			}
		}

		reply["result"] = "ok";
		reply["request_id"] = doc["request_id"];
		reply["data"]["applications"] = appPool->inspectPropertiesInAdminPanelFormat(
			inspectOptions);
		sendJsonReply(conn, reply);
		return true;
	}

	static void modifyEnvironmentVariables(Json::Value &option) {
		Json::Value::iterator it;
		for (it = option.begin(); it != option.end(); it++) {
			Json::Value &suboption = *it;
			suboption["value"] = suboption["value"].toStyledString();
		}
	}

	bool onGetApplicationConfig(const ConnectionPtr &conn, const Json::Value &doc) {
		Json::Value appConfigsContainer = configGetter()["config_manifest"]
			["effective_value"]["application_configurations"];
		Json::Value appConfigsContainerOutput;
		Json::Value reply;

		if (doc.isMember("arguments")) {
			ConfigKit::Schema argumentsSchema =
				ApplicationPool2::Pool::ToJsonOptions::createSchema();
			ConfigKit::Store store(argumentsSchema);
			vector<ConfigKit::Error> errors;

			if (!store.update(doc["arguments"], errors)) {
				reply["result"] = "error";
				reply["request_id"] = doc["request_id"];
				reply["data"]["message"] = "Invalid arguments: " +
					ConfigKit::toString(errors);
				sendJsonReply(conn, reply);
				return true;
			}

			Json::Value allowedApplicationIds =
				store.inspectEffectiveValues()["application_ids"];
			if (allowedApplicationIds.isNull()) {
				appConfigsContainerOutput = appConfigsContainer;
			} else {
				appConfigsContainerOutput = filterJsonObject(
					appConfigsContainer,
					allowedApplicationIds);
			}
		} else {
			appConfigsContainerOutput = appConfigsContainer;
		}

		reply["result"] = "ok";
		reply["request_id"] = doc["request_id"];
		reply["data"]["options"] = appConfigsContainerOutput;

		sendJsonReply(conn, reply);
		return true;
	}

	void addWatchedFiles() {
		Json::Value appConfigs = configGetter()["config_manifest"]["effective_value"]["application_configurations"];

		// As a hack, we look up the watched files config (passenger monitor log file) in the manifest. The manifest
		// is meant for users, which means that key names depend on the integration mode. In the future when
		// component configuration more routed through ConfigKit we can get rid of the hack.
		string integrationMode = server.getConfig()["integration_mode"].asString();
		string passengerMonitorLogFile;
		string passengerAppRoot;
		if (integrationMode == "apache") {
			passengerMonitorLogFile = "PassengerMonitorLogFile";
			passengerAppRoot = "PassengerAppRoot";
		} else {
			passengerMonitorLogFile = "passenger_monitor_log_file";
			passengerAppRoot = "passenger_app_root";
			// TODO: this probably doesn't give any results with the builtin engine (not supported in other places either)
		}

		foreach (HashedStaticString key, appConfigs.getMemberNames()) {
			Json::Value files = appConfigs[key]["options"][passengerMonitorLogFile]["value_hierarchy"][0]["value"];
			string appRoot = appConfigs[key]["options"][passengerAppRoot]["value_hierarchy"][0]["value"].asString();

			pair<uid_t, gid_t> ids;
			try {
				ids = appPool->getGroupRunUidAndGids(key);
			} catch (const RuntimeException &) {
				files = Json::nullValue;
			}
			if (!files.isNull()) {
				string usernameOrUid = lookupSystemUsernameByUid(ids.first,
					P_STATIC_STRING("%d"));

				foreach (Json::Value file, files) {
					string f = file.asString();
					string maxLines = toString(LOG_MONITORING_MAX_LINES);
					Pipe pipe = createPipe(__FILE__, __LINE__);
					string agentExe = resourceLocator->findSupportBinary(AGENT_EXE);
					vector<const char *> execArgs;

					execArgs.push_back(agentExe.c_str());
					execArgs.push_back("exec-helper");
					if (geteuid() == 0) {
						execArgs.push_back("--user");
						execArgs.push_back(usernameOrUid.c_str());
					}
					execArgs.push_back("tail");
					execArgs.push_back("-n");
					execArgs.push_back(maxLines.c_str());
					execArgs.push_back(f.c_str());
					execArgs.push_back(NULL);

					pid_t pid = syscalls::fork();

					if (pid == -1) {
						int e = errno;
						throw SystemException("Cannot fork a new process", e);
					} else if (pid == 0) {
						chdir(appRoot.c_str());

						dup2(pipe.second, STDOUT_FILENO);
						pipe.first.close();
						pipe.second.close();
						closeAllFileDescriptors(2);

						execvp(execArgs[0], (char * const *) &execArgs[0]);

						int e = errno;
						char buf[256];
						char *pos = buf;
						const char *end = pos + 256;

						pos = ASSU::appendData(pos, end, "Cannot execute \"");
						pos = ASSU::appendData(pos, end, agentExe.c_str());
						pos = ASSU::appendData(pos, end, "\": ");
						pos = ASSU::appendData(pos, end, strerror(e));
						pos = ASSU::appendData(pos, end, " (errno=");
						pos = ASSU::appendInteger<int, 10>(pos, end, e);
						pos = ASSU::appendData(pos, end, ")\n");
						ASSU::writeNoWarn(STDERR_FILENO, buf, pos - buf);
						_exit(1);
					} else {
						pipe.second.close();
						string out = readAll(pipe.first,
							std::numeric_limits<size_t>::max()).first;
						LoggingKit::context->saveMonitoredFileLog(key, f.c_str(), f.size(),
							out.data(), out.size());
						pipe.first.close();
						syscalls::waitpid(pid, NULL, 0);
					}
				}
			}
		}
	}

	bool onGetApplicationLogs(const ConnectionPtr &conn, const Json::Value &doc) {
		Json::Value reply;
		reply["result"] = "ok";
		reply["request_id"] = doc["request_id"];

		addWatchedFiles();

		reply["data"]["logs"] = LoggingKit::context->convertLog();
		sendJsonReply(conn, reply);
		return true;
	}

	bool onUnknownResource(const ConnectionPtr &conn, const Json::Value &doc) {
		Json::Value reply;
		reply["result"] = "error";
		reply["request_id"] = doc["request_id"];
		reply["data"]["message"] = "Unknown resource '" + doc["resource"].asString() + "'";
		sendJsonReply(conn, reply);
		return true;
	}

	bool onUnknownMessageAction(const ConnectionPtr &conn, const Json::Value &doc) {
		Json::Value reply;
		reply["result"] = "error";
		reply["request_id"] = doc["request_id"];
		reply["data"]["message"] = "Unknown action '" + doc["action"].asString() + "'";
		sendJsonReply(conn, reply);
		return true;
	}


	Json::Value parseAndBasicValidateMessageAsJSON(const string &msg) const {
		Json::Value doc;
		Json::Reader reader;
		if (!reader.parse(msg, doc)) {
			throw RuntimeException("Error parsing command JSON document: "
				+ reader.getFormattedErrorMessages());
		}

		if (!doc.isObject()) {
			throw RuntimeException("Invalid command JSON document: must be an object");
		}
		if (!doc.isMember("action")) {
			throw RuntimeException("Invalid command JSON document: missing 'action' key");
		}
		if (!doc["action"].isString()) {
			throw RuntimeException("Invalid command JSON document: the 'action' key must be a string");
		}
		if (!doc.isMember("request_id")) {
			throw RuntimeException("Invalid command JSON document: missing 'request_id' key");
		}
		if (!doc.isMember("resource")) {
			throw RuntimeException("Invalid command JSON document: missing 'resource' key");
		}
		if (!doc["resource"].isString()) {
			throw RuntimeException("Invalid command JSON document: the 'resource' key must be a string");
		}
		if (doc.isMember("arguments") && !doc["arguments"].isObject()) {
			throw RuntimeException("Invalid command JSON document: the 'arguments' key, when present, must be an object");
		}

		return doc;
	}

	void sendJsonReply(const ConnectionPtr &conn, const Json::Value &doc) {
		Json::FastWriter writer;
		string str = writer.write(doc);
		WCRS_DEBUG_FRAME(&server, "Replying with:", str);
		conn->send(str);
	}

	void readInstanceDirProperties(const string &instanceDir) {
		Json::Value doc;
		Json::Reader reader;

		if (!reader.parse(unsafeReadFile(instanceDir + "/properties.json"), doc)) {
			throw RuntimeException("Cannot parse " + instanceDir + "/properties.json: "
				+ reader.getFormattedErrorMessages());
		}

		globalPropertiesFromInstanceDir["instance_id"] = doc["instance_id"];
		globalPropertiesFromInstanceDir["watchdog_pid"] = doc["watchdog_pid"];
	}

	Json::Value filterJsonObject(const Json::Value &object,
		const Json::Value &allowedKeys) const
	{
		Json::Value::const_iterator it, end = allowedKeys.end();
		Json::Value result(Json::objectValue);

		for (it = allowedKeys.begin(); it != end; it++) {
			if (object.isMember(it->asString())) {
				result[it->asString()] = object[it->asString()];
			}
		}

		return result;
	}

	void initializePropertiesWithoutInstanceDir() {
		globalPropertiesFromInstanceDir["instance_id"] =
			InstanceDirectory::generateInstanceId();
	}

	string getLogPrefix() const {
		return server.getConfig()["log_prefix"].asString();
	}

	WebSocketCommandReverseServer::MessageHandler createMessageFunctor() {
		return boost::bind(&AdminPanelConnector::onMessage, this,
			boost::placeholders::_1, boost::placeholders::_2,
			boost::placeholders::_3);
	}

public:
	/******* Dependencies *******/

	ResourceLocator *resourceLocator;
	ApplicationPool2::PoolPtr appPool;
	ConfigGetter configGetter;
	Controllers controllers;


	AdminPanelConnector(const Schema &schema, const Json::Value &config,
		const ConfigKit::Translator &translator = ConfigKit::DummyTranslator())
		: server(schema, createMessageFunctor(), config, translator),
		  resourceLocator(NULL)
	{
		if (!config["instance_dir"].isNull()) {
			readInstanceDirProperties(config["instance_dir"].asString());
		} else {
			initializePropertiesWithoutInstanceDir();
		}
	}

	void initialize() {
		if (resourceLocator == NULL) {
			throw RuntimeException("resourceLocator must be non-NULL");
		}
		if (appPool == NULL) {
			throw RuntimeException("appPool must be non-NULL");
		}
		if (configGetter.empty()) {
			throw RuntimeException("configGetter must be non-NULL");
		}
		server.initialize();
	}

	void run() {
		server.run();
	}

	void asyncPrepareConfigChange(const Json::Value &updates,
		ConfigChangeRequest &req,
		const ConfigKit::CallbackTypes<WebSocketCommandReverseServer>::PrepareConfigChange &callback)
	{
		server.asyncPrepareConfigChange(updates, req, callback);
	}

	void asyncCommitConfigChange(ConfigChangeRequest &req,
		const ConfigKit::CallbackTypes<WebSocketCommandReverseServer>::CommitConfigChange &callback)
		BOOST_NOEXCEPT_OR_NOTHROW
	{
		server.asyncCommitConfigChange(req, callback);
	}

	void asyncShutdown(const WebSocketCommandReverseServer::Callback &callback
		= WebSocketCommandReverseServer::Callback())
	{
		server.asyncShutdown(callback);
	}
};

} // namespace Core
} // namespace Passenger

#endif /* _PASSENGER_ADMIN_PANEL_CONNECTOR_H_ */
