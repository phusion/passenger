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
#ifndef _PASSENGER_WEB_SOCKET_COMMAND_REVERSE_SERVER_H_
#define _PASSENGER_WEB_SOCKET_COMMAND_REVERSE_SERVER_H_

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include <boost/make_shared.hpp>
#include <boost/function.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/macros.hpp>

#include <string>
#include <vector>
#include <deque>
#include <cstring>

#include <jsoncpp/json.h>
#include <modp_b64.h>

#include <LoggingKit/Logging.h>
#include <ConfigKit/ConfigKit.h>
#include <ConfigKit/AsyncUtils.h>
#include <Exceptions.h>
#include <FileTools/PathManip.h>
#include <FileTools/FileManip.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {

using namespace std;


#define WCRS_DEBUG_FRAME(self, expr1, expr2) \
	P_LOG_UNLIKELY(Passenger::LoggingKit::context, \
		(self)->_getDataDebugLevel(), \
		__FILE__, __LINE__, \
		(self)->_getLogPrefix() << expr1 << " \"" \
			<< cEscapeString(expr2) << "\"")


/**
 * A generic WebSocket command "server" that implements a request/response
 * model.
 *
 * The reason why the name contains the word "reverse" is because it doesn't
 * actually listens on a port. Instead, it connects to a port and receives
 * commands from there.
 *
 * This class is generic in the sense that it handles all sorts of connection
 * management logic such as reconnecting on failure, handling pings, timeouts,
 * configuration, basic flow control, etc. It doesn't contain any logic for
 * actually handling incoming commands: you are supposed to supply a function
 * for handling incoming commands (the message handler). This allows seperating
 * all the connection management logic from the actual message handling
 * business logic.
 *
 * ## Usage
 *
 *     static bool
 *     onMessage(WebSocketCommandReverseServer *server,
 *         const WebSocketCommandReverseServer::ConnectionPtr &conn,
 *         const WebSocketCommandReverseServer::MessagePtr &msg)
 *     {
 *         P_INFO("Message received: " << msg->get_payload());
 *         conn->send("Echo: " + msg->get_payload());
 *         return true;
 *     }
 *
 *
 *     // Set configuration
 *     Json::Value config;
 *     config["url"] = "ws://127.0.0.1:8001/";
 *
 *     // Create and initialize the server
 *     WebSocketCommandReverseServer::Schema schema;
 *     WebSocketCommandReverseServer server(schema, onMessage, config);
 *     server.initialize();
 *
 *     // Enter the server's main loop. This blocks until something
 *     // calls `server.shutdown()`.
 *     server.run();
 *
 * ## About the concurrency and I/O model
 *
 * WebSocketCommandReverseServer uses the WebSocket++ library and the
 * Boost Asio I/O library. WebSocketCommandReverseServer manages its own
 * event loop.
 *
 * The message handler will be called from the event loop's thread, so
 * be careful.
 *
 * ## About flow control and backpressure
 *
 * We purposefully do not implement any flow control/backpressure on the
 * WebSocket's _writing_ side. That is, if we send a large amount of data to
 * the remote, then we do not wait until all that data has actually been
 * sent out before proceeding to read the next message. Unfortunately the
 * WebSocket++ API does not allow us to efficiently implement that:
 * https://github.com/zaphoyd/websocketpp/issues/477
 *
 * As of 4 September 2017 (Boost 1.64.0, WebSocket++ 0.7.0),
 * we also do not implement any flow control/backpressure
 * on the WebSocket's _reading_ side. If the server floods us with requests
 * then all of them will be buffered. We are unable to implement flow control
 * on the reading side because of a bug in either WebSocket++ or ASIO: if you
 * pause/resume the WebSocket then it results in data loss or data corruption.
 *
 * So the server is responsible for ensuring that it does not overload the
 * WebSocketCommandReverseServer.
 */
class WebSocketCommandReverseServer {
public:
	/*
	 * BEGIN ConfigKit schema: Passenger::WebSocketCommandReverseServer::Schema
	 * (do not edit: following text is automatically generated
	 * by 'rake configkit_schemas_inline_comments')
	 *
	 *   auth_type                  string    -          default("basic")
	 *   close_timeout              float     -          default(10.0)
	 *   connect_timeout            float     -          default(30.0)
	 *   data_debug                 boolean   -          default(false)
	 *   log_prefix                 string    -          -
	 *   password                   string    -          secret
	 *   password_file              string    -          -
	 *   ping_interval              float     -          default(30.0)
	 *   ping_timeout               float     -          default(30.0)
	 *   proxy_password             string    -          secret
	 *   proxy_timeout              float     -          default(30.0)
	 *   proxy_url                  string    -          -
	 *   proxy_username             string    -          -
	 *   reconnect_timeout          float     -          default(5.0)
	 *   url                        string    required   -
	 *   username                   string    -          -
	 *   websocketpp_debug_access   boolean   -          default(false)
	 *   websocketpp_debug_error    boolean   -          default(false)
	 *
	 * END
	 */
	class Schema: public ConfigKit::Schema {
	private:
		void initialize() {
			using namespace ConfigKit;

			add("url", STRING_TYPE, REQUIRED);
			add("log_prefix", STRING_TYPE, OPTIONAL);
			add("websocketpp_debug_access", BOOL_TYPE, OPTIONAL, false);
			add("websocketpp_debug_error", BOOL_TYPE, OPTIONAL, false);
			add("data_debug", BOOL_TYPE, OPTIONAL, false);
			add("auth_type", STRING_TYPE, OPTIONAL, "basic");
			add("username", STRING_TYPE, OPTIONAL);
			add("password", STRING_TYPE, OPTIONAL | SECRET);
			add("password_file", STRING_TYPE, OPTIONAL);
			add("proxy_url", STRING_TYPE, OPTIONAL);
			add("proxy_username", STRING_TYPE, OPTIONAL);
			add("proxy_password", STRING_TYPE, OPTIONAL | SECRET);
			add("proxy_timeout", FLOAT_TYPE, OPTIONAL, 30.0);
			add("connect_timeout", FLOAT_TYPE, OPTIONAL, 30.0);
			add("ping_interval", FLOAT_TYPE, OPTIONAL, 30.0);
			add("ping_timeout", FLOAT_TYPE, OPTIONAL, 30.0);
			add("close_timeout", FLOAT_TYPE, OPTIONAL, 10.0);
			add("reconnect_timeout", FLOAT_TYPE, OPTIONAL, 5.0);

			addValidator(validateAuthentication);
			addNormalizer(normalizeAuthentication);
		}

		static void validateAuthentication(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
			typedef ConfigKit::Error Error;

			// url is required, but Core::Schema overrides it to be optional.
			if (config["url"].isNull() || config["auth_type"].asString() == "none") {
				return;
			}

			if (config["auth_type"].asString() != "basic") {
				errors.push_back(Error("Unsupported '{{auth_type}}' value"
					" (only 'none' and 'basic' are supported)"));
			}

			if (config["auth_type"].asString() == "basic") {
				if (config["username"].isNull()) {
					errors.push_back(Error(
						"When '{{auth_type}}' is set to 'basic', '{{username}}' must also be set"));
				}

				if (config["password"].isNull() && config["password_file"].isNull()) {
					errors.push_back(Error(
						"When '{{auth_type}}' is set to 'basic',"
						" then either '{{password}}' or '{{password_file}}' must also be set"));
				} else if (!config["password"].isNull() && !config["password_file"].isNull()) {
					errors.push_back(Error(
						"Only one of '{{password}}' or '{{password_file}}' may be set, but not both"));
				}
			}
		}

		static Json::Value normalizeAuthentication(const Json::Value &effectiveValues) {
			Json::Value updates;
			if (!effectiveValues["password_file"].isNull()) {
				updates["password_file"] = absolutizePath(
					effectiveValues["password_file"].asString());
			}
			return updates;
		}

	public:
		Schema() {
			initialize();
			finalize();
		}

		Schema(bool _subclassing) {
			initialize();
		}
	};

	struct ConfigRealization {
		string logPrefix;
		bool dataDebug;

		ConfigRealization(const ConfigKit::Store &config)
			: logPrefix(config["log_prefix"].asString()),
			  dataDebug(config["data_debug"].asBool())
			{ }

		void swap(ConfigRealization &other) BOOST_NOEXCEPT_OR_NOTHROW {
			logPrefix.swap(other.logPrefix);
			std::swap(dataDebug, other.dataDebug);
		}
	};

	struct ConfigChangeRequest {
		boost::scoped_ptr<ConfigKit::Store> config;
		boost::scoped_ptr<ConfigRealization> configRlz;
	};

	typedef websocketpp::client<websocketpp::config::asio_client> Endpoint;
	typedef Endpoint::connection_ptr ConnectionPtr;
	typedef Endpoint::message_ptr MessagePtr;
	typedef websocketpp::connection_hdl ConnectionWeakPtr;

	typedef boost::function<void ()> Callback;
	typedef boost::function<void (const Json::Value &doc)> InspectCallback;
	typedef boost::function<bool (WebSocketCommandReverseServer *server,
		const ConnectionPtr &conn, const MessagePtr &msg)> MessageHandler;

	enum State {
		UNINITIALIZED,
		NOT_CONNECTED,
		CONNECTING,
		WAITING_FOR_REQUEST,
		REPLYING,
		CLOSING,
		SHUT_DOWN
	};

private:
	ConfigKit::Store config;
	ConfigRealization configRlz;

	Endpoint endpoint;
	ConnectionPtr conn;
	boost::shared_ptr<boost::asio::deadline_timer> timer;
	MessageHandler messageHandler;
	Callback shutdownCallback;
	mutable boost::mutex stateSyncher;
	State state;
	deque<MessagePtr> buffer;
	bool reconnectAfterReply;
	bool shuttingDown;

	/**
	 * It could happen that a certain method or handler is invoked
	 * for a connection that has already been closed. For example,
	 * after the message handler was invoked and before the message
	 * handler called doneReplying(), it could happen that the connection
	 * was reset. This method allows detecting those cases so that
	 * the code can decide not to do anything.
	 */
	bool isCurrentConnection(const ConnectionPtr &c) {
		return conn && c.get() == conn.get();
	}

	bool isCurrentConnection(const ConnectionWeakPtr &wconn) {
		return conn && endpoint.get_con_from_hdl(wconn).get() == conn.get();
	}

	static bool connectionIsConnected(const ConnectionPtr &c) {
		websocketpp::session::state::value state = c->get_state();
		return state == websocketpp::session::state::connecting
			|| state == websocketpp::session::state::open;
	}

	bool isConnected(const ConnectionPtr &c) {
		return isCurrentConnection(c) && connectionIsConnected(c);
	}

	bool isConnected(const ConnectionWeakPtr &wconn) {
		if (OXT_UNLIKELY(!conn)) {
			return false;
		}

		ConnectionPtr c = endpoint.get_con_from_hdl(wconn);
		if (OXT_UNLIKELY(c.get() != conn.get())) {
			return false;
		}

		return connectionIsConnected(c);
	}

	const string &getLogPrefix() const {
		return configRlz.logPrefix;
	}

	void activateConfigUpdates(const ConfigKit::Store *oldConfig) {
		if (config["websocketpp_debug_access"].asBool()) {
			endpoint.set_access_channels(websocketpp::log::alevel::all);
		} else {
			endpoint.clear_access_channels(websocketpp::log::alevel::all);
		}
		if (config["websocketpp_debug_error"].asBool()) {
			endpoint.set_error_channels(websocketpp::log::elevel::all);
		} else {
			endpoint.clear_error_channels(websocketpp::log::elevel::all);
		}

		if (oldConfig == NULL) {
			return;
		}
		bool shouldReconnect =
			oldConfig->get("url").asString() != config["url"].asString() ||
			oldConfig->get("proxy_url").asString() != config["proxy_url"].asString() ||
			oldConfig->get("data_debug").asBool() != config["data_debug"].asBool() ||
			oldConfig->get("websocketpp_debug_access").asBool() != config["websocketpp_debug_access"].asBool() ||
			oldConfig->get("websocketpp_debug_error").asBool() != config["websocketpp_debug_error"].asBool();
		if (shouldReconnect) {
			internalReconnect();
		}
	}

	void internalInspectState(const InspectCallback callback) {
		Json::Value doc(Json::objectValue);
		doc["state"] = getStateString();
		doc["buffer"]["message_count"] = (Json::UInt) buffer.size();
		if (reconnectAfterReply) {
			doc["reconnect_planned"] = true;
		}
		if (shuttingDown) {
			doc["shutting_down"] = true;
		}
		callback(doc);
	}

	string getStateString() const {
		boost::lock_guard<boost::mutex> l(stateSyncher);
		switch (state) {
		case UNINITIALIZED:
			return "UNINITIALIZED";
		case NOT_CONNECTED:
			return "NOT_CONNECTED";
		case CONNECTING:
			return "CONNECTING";
		case WAITING_FOR_REQUEST:
			return "WAITING_FOR_REQUEST";
		case REPLYING:
			return "REPLYING";
		case CLOSING:
			return "CLOSING";
		case SHUT_DOWN:
			return "SHUT_DOWN";
		default:
			return "UNKNOWN";
		};
	}

	void internalShutdown(const Callback callback) {
		shuttingDown = true;
		shutdownCallback = callback;
		closeConnection(websocketpp::close::status::going_away,
			"shutting down");
	}

	void startConnect() {
		websocketpp::lib::error_code ec;

		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = CONNECTING;
		}

		P_NOTICE(getLogPrefix() << "Connecting to " << config["url"].asString());
		conn = endpoint.get_connection(config["url"].asString(), ec);
		if (ec) {
			P_ERROR(getLogPrefix() << "Error setting up a socket to "
				<< config["url"].asString() << ": " << ec.message());
			{
				boost::lock_guard<boost::mutex> l(stateSyncher);
				state = NOT_CONNECTED;
			}
			scheduleReconnect();
			return;
		}

		if (!applyConnectionConfig(conn)) {
			// applyConnectionConfig() already logs an error.
			{
				boost::lock_guard<boost::mutex> l(stateSyncher);
				state = NOT_CONNECTED;
			}
			scheduleReconnect();
			return;
		}

		using websocketpp::lib::placeholders::_1;
		using websocketpp::lib::placeholders::_2;

		if (config["auth_type"].asString() == "basic") {
			try {
				addBasicAuthHeader(conn);
			} catch (const std::exception &e) {
				P_ERROR(getLogPrefix() << "Error setting up basic authentication: "
					<< e.what());
				{
					boost::lock_guard<boost::mutex> l(stateSyncher);
					state = NOT_CONNECTED;
				}
				scheduleReconnect();
				return;
			}
		}

		conn->set_socket_init_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onSocketInit,
			this,
			_1,
			_2));
		conn->set_open_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onConnected,
			this,
			_1));
		conn->set_fail_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onConnectFailed,
			this,
			websocketpp::lib::placeholders::_1));
		conn->set_close_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onConnectionClosed,
			this,
			websocketpp::lib::placeholders::_1));
		conn->set_pong_timeout_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onPongTimeout,
			this,
			_1,
			_2));
		conn->set_pong_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onPong,
			this,
			_1,
			_2));
		conn->set_message_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onMessage,
			this,
			_1,
			_2));

		endpoint.connect(conn);
	}

	void addBasicAuthHeader(ConnectionPtr &conn) {
		string username = config["username"].asString();
		string password;
		if (config["password_file"].isNull()) {
			password = config["password"].asString();
		} else {
			password = strip(unsafeReadFile(config["password_file"].asString()));
		}
		string data = modp::b64_encode(username + ":" + password);
		conn->append_header("Authorization", "Basic " + data);
	}

	bool applyConnectionConfig(ConnectionPtr &conn) {
		websocketpp::lib::error_code ec;

		if (!config["proxy_url"].isNull()) {
			conn->set_proxy(config["proxy_url"].asString(), ec);
			if (ec) {
				P_ERROR(getLogPrefix()
					<< "Error setting proxy URL to "
					<< config["proxy_url"].asString() << ": "
					<< ec.message());
				return false;
			}

			if (!config["proxy_username"].isNull() || !config["proxy_password"].isNull()) {
				conn->set_proxy_basic_auth(config["proxy_username"].asString(),
					config["proxy_password"].asString(), ec);
				if (ec) {
					P_ERROR(getLogPrefix()
						<< "Error setting proxy authentication credentials to "
						<< config["proxy_username"].asString() << ":<password omitted>:"
						<< ec.message());
					return false;
				}
			}

			conn->set_proxy_timeout(config["proxy_timeout"].asDouble() * 1000, ec);
			if (ec) {
				P_ERROR(getLogPrefix()
					<< "Error setting proxy timeout to "
					<< config["proxy_timeout"].asDouble() << " seconds: "
					<< ec.message());
				return false;
			}
		}

		conn->set_open_handshake_timeout(config["connect_timeout"].asDouble() * 1000);
		conn->set_pong_timeout(config["ping_timeout"].asDouble() * 1000);
		conn->set_close_handshake_timeout(config["close_timeout"].asDouble() * 1000);

		return true;
	}

	void internalReconnect() {
		switch (state) {
		case NOT_CONNECTED:
			// Do nothing.
			break;
		case CONNECTING:
		case WAITING_FOR_REQUEST:
			closeConnection(websocketpp::close::status::service_restart,
				"reestablishing connection in order to apply configuration updates");
			break;
		case REPLYING:
			reconnectAfterReply = true;
			return;
		default:
			P_BUG("Unsupported state " + toString(state));
		}
	}

	void scheduleReconnect() {
		P_NOTICE(getLogPrefix() << "Reestablishing connection in " <<
			config["reconnect_timeout"].asDouble() << " seconds");
		restartTimer(config["reconnect_timeout"].asDouble() * 1000);
	}

	void closeConnection(websocketpp::close::status::value code,
		const string &reason)
	{
		websocketpp::lib::error_code ec;

		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = CLOSING;
		}

		P_NOTICE(getLogPrefix() << "Closing connection: " << reason);
		reconnectAfterReply = false;
		timer->cancel();
		if (conn != NULL) {
			conn->close(code, reason, ec);
			conn.reset();
			if (ec) {
				P_WARN(getLogPrefix() << "Error closing connection: " << ec.message());
			}
		}

		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = NOT_CONNECTED;
		}
		if (!shuttingDown) {
			scheduleReconnect();
		}
	}

	void restartTimer(unsigned int ms) {
		timer->expires_from_now(boost::posix_time::milliseconds(ms));
		timer->async_wait(boost::bind(
			&WebSocketCommandReverseServer::onTimeout,
			this,
			boost::placeholders::_1));
	}

	void onSocketInit(ConnectionWeakPtr wconn, boost::asio::ip::tcp::socket &s) {
		boost::asio::ip::tcp::no_delay option(true);
		s.set_option(option);
	}

	void onConnected(ConnectionWeakPtr wconn) {
		if (!isConnected(wconn)) {
			P_DEBUG(getLogPrefix() << "onConnected: stale connection");
			return;
		}

		P_NOTICE(getLogPrefix() << "Connection established");
		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = WAITING_FOR_REQUEST;
		}
		buffer.clear();
		P_DEBUG(getLogPrefix() << "Scheduling next ping in " <<
			config["ping_interval"].asDouble() << " seconds");
		restartTimer(config["ping_interval"].asDouble() * 1000);
	}

	void onConnectFailed(ConnectionWeakPtr wconn) {
		if (!isCurrentConnection(wconn)) {
			P_DEBUG(getLogPrefix() << "onConnectFailed: not current connection");
			return;
		}

		if (LoggingKit::getLevel() >= LoggingKit::ERROR) {
			string message;
			if (strcmp(conn->get_ec().category().name(), "websocketpp.processor") == 0
				&& conn->get_ec().value() == websocketpp::processor::error::invalid_http_status)
			{
				if (conn->get_response_code() == websocketpp::http::status_code::unauthorized) {
					message = "server authentication error";
				} else {
					message = conn->get_ec().message();
				}
			} else {
				message = conn->get_ec().message();
			}
			P_ERROR(getLogPrefix() << "Unable to establish connection: " << message);
		}
		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = NOT_CONNECTED;
		}
		scheduleReconnect();
	}

	void onConnectionClosed(ConnectionWeakPtr wconn) {
		if (!isCurrentConnection(wconn)) {
			P_DEBUG(getLogPrefix() << "onConnectionClosed: not current connection");
			return;
		}

		P_NOTICE(getLogPrefix() << "Connection closed (server close reason: " <<
			conn->get_remote_close_code() << ": " <<
			conn->get_remote_close_reason() << ")");
		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = NOT_CONNECTED;
		}
		reconnectAfterReply = false;

		if (shuttingDown) {
			timer->cancel();
		} else {
			scheduleReconnect();
		}
	}

	void onTimeout(const boost::system::error_code &e) {
		if (e.value() == boost::system::errc::operation_canceled) {
			P_DEBUG(getLogPrefix() << "onTimeout: operation cancelled");
			return;
		}
		if (e) {
			P_ERROR(getLogPrefix() << "Error in timer: " << e.message());
			return;
		}

		websocketpp::lib::error_code ec;

		switch (state) {
		case NOT_CONNECTED:
			startConnect();
			break;
		case WAITING_FOR_REQUEST:
		case REPLYING:
			P_DEBUG(getLogPrefix() << "Sending ping");
			conn->ping("ping", ec);
			if (ec) {
				closeConnection(websocketpp::close::status::normal,
					"error sending ping");
			}
			// After sending the ping, we wait until either
			// onPong() or onPongTimeout() is called before
			// scheduling the next ping.
			break;
		default:
			P_BUG("Unsupported state " + toString(state));
			break;
		}
	}

	void onPongTimeout(ConnectionWeakPtr wconn, const string &payload) {
		if (!isCurrentConnection(wconn)) {
			P_DEBUG(getLogPrefix() << "onPongTimeout: not current connection");
			return;
		}

		switch (state) {
		case REPLYING:
			// Ignore pong timeouts while replying because
			// reading is paused while replying.
			P_DEBUG(getLogPrefix() << "onPongTimeout: ignoring REPLYING state");
			break;
		default:
			P_DEBUG(getLogPrefix() << "onPongTimeout: closing connection");
			closeConnection(websocketpp::close::status::normal,
				"reconnecting because of pong timeout");
			break;
		}
	}

	void onPong(ConnectionWeakPtr wconn, const string &payload) {
		if (!isConnected(wconn)) {
			P_DEBUG(getLogPrefix() << "onPong: stale connection");
			return;
		}

		P_DEBUG(getLogPrefix() << "Pong received. Scheduling next ping in " <<
			config["ping_interval"].asDouble() << " seconds");
		restartTimer(config["ping_interval"].asDouble() * 1000);
	}

	void onMessage(ConnectionWeakPtr wconn, MessagePtr msg) {
		if (!isConnected(wconn)) {
			P_DEBUG(getLogPrefix() << "onMessage: stale connection");
			return;
		}

		switch (state) {
		case WAITING_FOR_REQUEST:
			P_DEBUG(getLogPrefix() << "onMessage: got frame of " <<
				msg->get_payload().size() << " bytes");
			WCRS_DEBUG_FRAME(this, "Received message's frame data:", msg->get_payload());
			{
				boost::lock_guard<boost::mutex> l(stateSyncher);
				state = REPLYING;
			}
			if (messageHandler(this, conn, msg)) {
				doneReplying(conn);
			} else {
				// We do not pause the connection here because of what appears
				// to be an ASIO bug.
				// See class header comments, "About flow control and backpressure".
				//conn->pause_reading();
			}
			break;
		case CLOSING:
			// Ignore any incoming messages while closing.
			P_DEBUG(getLogPrefix() << "onMessage: ignoring CLOSING state");
			break;
		case REPLYING:
			// Even if we call conn->pause_reading(), WebSocket++
			// may already have received further messages in its buffer,
			// which it will still pass to us. Don't process these
			// and just buffer them.
			P_DEBUG(getLogPrefix() << "onMessage: got frame of " <<
				msg->get_payload().size() << " bytes (pushed to buffer -> "
				<< (buffer.size() + 1) << " entries)");
			WCRS_DEBUG_FRAME(this, "Received message's frame data:", msg->get_payload());
			buffer.push_back(msg);
			break;
		default:
			P_BUG("Unsupported state " + toString(state));
		}
	}

public:
	WebSocketCommandReverseServer(const Schema &schema, const MessageHandler &_messageHandler,
		const Json::Value &initialConfig,
		const ConfigKit::Translator &translator = ConfigKit::DummyTranslator())
		: config(schema, initialConfig, translator),
		  configRlz(config),
		  messageHandler(_messageHandler),
		  state(UNINITIALIZED),
		  reconnectAfterReply(false),
		  shuttingDown(false)
	{
		activateConfigUpdates(NULL);
	}

	void initialize() {
		endpoint.init_asio();
		state = NOT_CONNECTED;
		timer = boost::make_shared<boost::asio::deadline_timer, boost::asio::io_service &>(
			endpoint.get_io_service());
		startConnect();
	}

	/**
	 * Enter the server's event loop. This method blocks until
	 * the server is shut down.
	 *
	 * May only be called once, and only after `initialize()` is called.
	 */
	void run() {
		endpoint.run();
		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = SHUT_DOWN;
		}
		if (shutdownCallback) {
			shutdownCallback();
		}
	}


	const ConfigKit::Store &getConfig() const {
		return config;
	}

	boost::asio::io_service &getIoService() {
		return endpoint.get_io_service();
	}


	bool prepareConfigChange(const Json::Value &updates,
		vector<ConfigKit::Error> &errors, ConfigChangeRequest &req)
	{
		req.config.reset(new ConfigKit::Store(config, updates, errors));
		if (errors.empty()) {
			req.configRlz.reset(new ConfigRealization(*req.config));
		}
		return errors.empty();
	}

	void commitConfigChange(ConfigChangeRequest &req) BOOST_NOEXCEPT_OR_NOTHROW {
		config.swap(*req.config);
		configRlz.swap(*req.configRlz);
		activateConfigUpdates(req.config.get());
	}

	Json::Value inspectConfig() const {
		return config.inspect();
	}


	void asyncPrepareConfigChange(const Json::Value &updates,
		ConfigChangeRequest &req,
		const ConfigKit::CallbackTypes<WebSocketCommandReverseServer>::PrepareConfigChange &callback)
	{
		endpoint.get_io_service().post(boost::bind(
			ConfigKit::callPrepareConfigChangeAndCallback<WebSocketCommandReverseServer>,
			this, updates, &req, callback));
	}

	void asyncCommitConfigChange(ConfigChangeRequest &req,
		const ConfigKit::CallbackTypes<WebSocketCommandReverseServer>::CommitConfigChange &callback)
		BOOST_NOEXCEPT_OR_NOTHROW
	{
		endpoint.get_io_service().post(boost::bind(
			ConfigKit::callCommitConfigChangeAndCallback<WebSocketCommandReverseServer>,
			this, &req, callback));
	}

	void asyncInspectConfig(const ConfigKit::CallbackTypes<WebSocketCommandReverseServer>::InspectConfig &callback) {
		endpoint.get_io_service().post(boost::bind(
			ConfigKit::callInspectConfigAndCallback<WebSocketCommandReverseServer>,
			this, callback));
	}

	void asyncInspectState(const InspectCallback &callback) {
		endpoint.get_io_service().post(boost::bind(
			&WebSocketCommandReverseServer::internalInspectState,
			this, callback));
	}

	/**
	 * Prepares this server for shut down. It will finish any replies that
	 * are in-flight and will close the connection. When finished, it will
	 * call the given callback (if any) from the thread that invoked
	 * `run()`.
	 *
	 * May only be called when the event loop is running.
	 * This method is thread-safe and may be called from any thread.
	 */
	void asyncShutdown(const Callback &callback = Callback()) {
		endpoint.get_io_service().post(boost::bind(
			&WebSocketCommandReverseServer::internalShutdown,
			this, callback));
	}


	/**
	 * When the message handler is done sending a reply, it must
	 * call this method to tell the server that the reply is done.
	 *
	 * May only be called when the server is in the REPLYING state.
	 * May only be called from the event loop's thread.
	 */
	void doneReplying(const ConnectionPtr &conn) {
		begin:

		if (!isConnected(conn)) {
			P_DEBUG(getLogPrefix() << "doneReplying: stale connection");
			return;
		}

		P_DEBUG(getLogPrefix() << "Done replying");
		P_ASSERT_EQ(state, REPLYING);

		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = WAITING_FOR_REQUEST;
		}
		if (reconnectAfterReply) {
			reconnectAfterReply = false;
			internalReconnect();
			return;
		}

		if (buffer.empty()) {
			// We do not resume the connection here because of what appears
			// to be an ASIO bug.
			// See class header comments, "About flow control and backpressure".
			//conn->resume_reading();
		} else {
			MessagePtr msg = buffer.front();
			P_DEBUG(getLogPrefix() << "Process next message in buffer ("
				<< buffer.size() << " entries): " <<
				msg->get_payload().size() << " bytes");
			WCRS_DEBUG_FRAME(this, "Buffered message's frame data:", msg->get_payload());
			buffer.pop_front();
			{
				boost::lock_guard<boost::mutex> l(stateSyncher);
				state = REPLYING;
			}
			if (messageHandler(this, conn, msg)) {
				goto begin;
			}
		}
	}


	const string &_getLogPrefix() const {
		return getLogPrefix();
	}

	LoggingKit::Level _getDataDebugLevel() const {
		if (OXT_UNLIKELY(configRlz.dataDebug)) {
			return LoggingKit::NOTICE;
		} else {
			return LoggingKit::DEBUG2;
		}
	}
};


} // namespace Passenger

#endif /* _PASSENGER_WEB_SOCKET_COMMAND_REVERSE_SERVER_H_ */
