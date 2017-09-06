/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
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

#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>

#include <Constants.h>
#include <WebSocketCommandReverseServer.h>
#include <InstanceDirectory.h>
#include <ConfigKit/ValidationUtils.h>
#include <Core/ApplicationPool/Pool.h>
#include <Utils/StrIntUtils.h>
#include <Utils/IOUtils.h>

#include <jsoncpp/json.h>

namespace Passenger {
namespace Core {

using namespace std;
using namespace oxt;

class AdminPanelConnector {
public:
	/**
	 * BEGIN ConfigKit schema: Passenger::Core::AdminPanelConnector::Schema
	 * (do not edit: following text is automatically generated
	 * by 'rake configkit_schemas_inline_comments')
	 *
	 *   authentication              object    -          secret
	 *   close_timeout               float     -          default(10.0)
	 *   connect_timeout             float     -          default(30.0)
	 *   data_debug                  boolean   -          default(false)
	 *   instance_dir                string    -          read_only
	 *   integration_mode            string    -          default("standalone")
	 *   log_prefix                  string    -          -
	 *   ping_interval               float     -          default(30.0)
	 *   ping_timeout                float     -          default(30.0)
	 *   proxy_password              string    -          secret
	 *   proxy_timeout               float     -          default(30.0)
	 *   proxy_url                   string    -          -
	 *   proxy_username              string    -          -
	 *   reconnect_timeout           float     -          default(5.0)
	 *   ruby                        string    -          default("ruby")
	 *   standalone_engine           string    -          -
	 *   url                         string    required   -
	 *   web_server_module_version   string    -          read_only
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
			add("standalone_engine", STRING_TYPE, OPTIONAL);
			add("instance_dir", STRING_TYPE, OPTIONAL | READ_ONLY);
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
		} else if (resource == "application_config") {
			return onGetApplicationConfig(conn, doc);
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
		string output;
		try {
			output = runInternalRubyTool(*resourceLocator, ruby, args, &status);
		} catch (const std::exception &e) {
			server.getIoService().post(boost::bind(
				&AdminPanelConnector::onGetServerPropertiesDone, this,
				conn, doc, string(), -1, e.what()
			));
			return;
		}

		server.getIoService().post(boost::bind(
			&AdminPanelConnector::onGetServerPropertiesDone, this,
			conn, doc, output, status, string()
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
		Json::Value reply, data;
		reply["result"] = "ok";
		reply["request_id"] = doc["request_id"];

		data = globalPropertiesFromInstanceDir;
		data["version"] = PASSENGER_VERSION;
		data["core_pid"] = Json::UInt(getpid());

		const ConfigKit::Store &config = server.getConfig();
		string integrationMode = config["integration_mode"].asString();
		data["integration_mode"]["name"] = integrationMode;
		if (!config["web_server_module_version"].isNull()) {
			data["integration_mode"]["web_server_module_version"] = config["web_server_module_version"];
		}
		if (integrationMode == "standalone") {
			data["integration_mode"]["standalone_engine"] = config["standalone_engine"];
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
		server.getIoService().post(boost::bind(
			&AdminPanelConnector::onGetGlobalConfigDone, this,
			conn, input, configGetter()
		));
	}

	void onGetGlobalConfigDone(const ConnectionPtr &conn, const Json::Value &input,
		const Json::Value config)
	{
		Json::Value reply, options(Json::objectValue);
		Json::Value::const_iterator it, end = config.end();

		for (it = config.begin(); it != end; it++) {
			const Json::Value &subconfig = *it;
			Json::Value valueHierarchy(Json::arrayValue);

			if (!subconfig["user_value"].isNull()) {
				Json::Value valueEntry;
				valueEntry["value"] = subconfig["user_value"];
				valueEntry["source"]["type"] = "ephemeral";
				valueHierarchy.append(valueEntry);
			}
			if (!subconfig["default_value"].isNull()) {
				Json::Value valueEntry;
				valueEntry["value"] = subconfig["default_value"];
				valueEntry["source"]["type"] = "default";
				valueHierarchy.append(valueEntry);
			}

			options[it.name()] = valueHierarchy;
		}

		reply["status"] = "ok";
		reply["request_id"] = input["request_id"];
		reply["data"]["options"] = options;

		sendJsonReply(conn, reply);
		server.doneReplying(conn);
	}

	bool onGetGlobalStatistics(const ConnectionPtr &conn, const Json::Value &doc) {
		Json::Value reply;
		reply["result"] = "error";
		reply["request_id"] = doc["request_id"];
		reply["data"]["message"] = "Action not implemented";
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

	bool onGetApplicationConfig(const ConnectionPtr &conn, const Json::Value &doc) {
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
		reply["data"]["options"] = appPool->inspectConfigInAdminPanelFormat(
			inspectOptions);
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

		if (!reader.parse(readAll(instanceDir + "/properties.json"), doc)) {
			throw RuntimeException("Cannot parse " + instanceDir + "/properties.json: "
				+ reader.getFormattedErrorMessages());
		}

		globalPropertiesFromInstanceDir["instance_id"] = doc["instance_id"];
		globalPropertiesFromInstanceDir["watchdog_pid"] = doc["watchdog_pid"];
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


	template<typename Translator = ConfigKit::DummyTranslator>
	AdminPanelConnector(const Schema &schema, const Json::Value &config,
		const Translator &translator = ConfigKit::DummyTranslator())
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
		if (configGetter == NULL) {
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
