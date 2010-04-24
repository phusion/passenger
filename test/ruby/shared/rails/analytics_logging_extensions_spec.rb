require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')
require 'socket'
require 'fileutils'
require 'phusion_passenger/analytics_logger'
require 'phusion_passenger/utils/tmpdir'

module PhusionPassenger

shared_examples_for "analytics logging extensions for Rails" do
	before :each do
		@agent_pid, @socket_filename = spawn_logging_agent(Utils.passenger_tmpdir, "1234")
		@options = {
			"logging_agent_address" => @socket_filename,
			"logging_agent_username" => "logging",
			"logging_agent_password_base64" => base64("1234"),
			"node_name" => "localhost"
		}
	end
	
	after :each do
		@connection.close if @connection && @connection.closed?
		Process.kill('KILL', @agent_pid)
		Process.waitpid(@agent_pid)
	end
	
	def send_request_to_app(app, env)
		if app.server_sockets[:main][1] == "unix"
			@connection = UNIXSocket.new(app.server_sockets[:main][0])
		else
			addr, port = app.server_sockets[:main][0].split(/:/)
			@connection = TCPSocket.new(addr, port.to_i)
		end
		channel = MessageChannel.new(@connection)
		data = ""
		env = {
			"REQUEST_METHOD" => "GET",
			"SCRIPT_NAME"    => "",
			"PATH_INFO"      => "/",
			"QUERY_STRING"   => "",
			"SERVER_NAME"    => "localhost",
			"SERVER_PORT"    => "80",
			"PASSENGER_TXN_ID"     => "1234-abcd",
			"PASSENGER_GROUP_NAME" => "foobar"
		}.merge(env)
		env["REQUEST_URI"] ||= env["PATH_INFO"]
		env.each_pair do |key, value|
			data << key << "\0"
			data << value << "\0"
		end
		channel.write_scalar(data)
		return @connection.read
	end
	
	def read_log(name_suffix)
		filename = Dir["#{Utils.passenger_tmpdir}/1/*/*/#{name_suffix}"].first
		if filename
			return File.read(filename)
		else
			return ""
		end
	end
	
	def base64(data)
		return [data].pack('m').gsub("\n", "")
	end
	
	it "doesn't install analytics logging extensions if analytics logging is turned off"
	
	it "logs the controller and action name" do
		app = spawn_some_application(@options) do |stub|
			File.write("#{stub.app_root}/app/controllers/foo_controller.rb", %Q{
				class FooController < ActionController::Base
					def index
						render :nothing => true
					end
				end
			})
		end
		send_request_to_app(app, "PATH_INFO" => "/foo")
		eventually(5) do
			log = read_log("requests/**/log.txt")
			log.include?("Controller action: FooController#index\n")
		end
	end
	
	it "logs uncaught exceptions in controller actions" do
		app = spawn_some_application(@options) do |stub|
			File.write("#{stub.app_root}/app/controllers/crash_controller.rb", %Q{
				class CrashController < ActionController::Base
					def index
						raise "something went wrong"
					end
				end
			})
		end
		send_request_to_app(app, "PATH_INFO" => "/crash")
		eventually(5) do
			log = read_log("exceptions/**/log.txt")
			log.include?("Request transaction ID: 1234-abcd\n") &&
				log.include?("Message: " + base64("something went wrong")) &&
				log.include?("Class: RuntimeError") &&
				log.include?("Backtrace: ") &&
				log.include?("Controller action: CrashController#index")
		end
	end
	
	it "logs ActionController benchmarks" do
		app = spawn_some_application(@options) do |stub|
			File.write("#{stub.app_root}/app/controllers/foo_controller.rb", %Q{
				class FooController < ActionController::Base
					def index
						# The '::' prefix works around a Dependencies
						# bug in Rails 1.2
						::ActionController::Base.benchmark("hello") do
						end
						render :nothing => true
					end
				end
			})
		end
		send_request_to_app(app, "PATH_INFO" => "/foo")
		eventually(5) do
			log = read_log("requests/**/log.txt")
			log.include?('BEGIN: BENCHMARK: hello') &&
				log.include?('END: BENCHMARK: hello')
		end
	end
	
	it "logs ActionView benchmarks" do
		app = spawn_some_application(@options) do |stub|
			File.write("#{stub.app_root}/app/controllers/foo_controller.rb", %Q{
				class FooController < ActionController::Base
				end
			})
			FileUtils.mkdir_p("#{stub.app_root}/app/views/foo")
			File.write("#{stub.app_root}/app/views/foo/index.rhtml", %Q{
				<% benchmark("hello") do %>
				<% end %>
			})
		end
		send_request_to_app(app, "PATH_INFO" => "/foo")
		eventually(5) do
			log = read_log("requests/**/log.txt")
			log.include?('BEGIN: BENCHMARK: hello') &&
				log.include?('END: BENCHMARK: hello')
		end
	end
	
	it "logs SQL queries" do
		app = spawn_some_application(@options) do |stub|
			File.write("#{stub.app_root}/config/database.yml",
				"production:\n" +
				"  adapter: sqlite3\n" +
				"  database: db.sqlite3\n")
			File.write("#{stub.app_root}/app/controllers/foo_controller.rb", %Q{
				class FooController < ActionController::Base
					def index
						db = ActiveRecord::Base.connection
						db.execute("CREATE TABLE foobar (id INT)")
						db.execute("INSERT INTO foobar VALUES (1)")
						render :nothing => true
					end
				end
			})
		end
		send_request_to_app(app, "PATH_INFO" => "/foo")
		eventually(5) do
			log = read_log("requests/**/log.txt")
			log.include?("DB BENCHMARK: " + base64("CREATE TABLE foobar (id INT)")) &&
				log.include?("DB BENCHMARK: " + base64("INSERT INTO foobar VALUES (1)"))
		end
	end
	
	it "logs controller processing time" do
		app = spawn_some_application(@options) do |stub|
			File.write("#{stub.app_root}/app/controllers/foo_controller.rb", %Q{
				class FooController < ActionController::Base
					def index
						render :nothing => true
					end
				end
			})
		end
		send_request_to_app(app, "PATH_INFO" => "/foo")
		eventually(5) do
			log = read_log("requests/**/log.txt")
			log.include?("BEGIN: framework request processing") &&
				log.include?("END: framework request processing")
		end
	end
	
	it "logs view rendering time" do
		app = spawn_some_application(@options) do |stub|
			File.write("#{stub.app_root}/app/controllers/foo_controller.rb", %Q{
				class FooController < ActionController::Base
					def index
						render :nothing => true
					end
				end
			})
		end
		send_request_to_app(app, "PATH_INFO" => "/foo")
		eventually(5) do
			log = read_log("requests/**/log.txt")
			( log.include?("BEGIN: view rendering") &&
				log.include?("END: view rendering") ) ||
				log.include?("MEASURED: view rendering") ||
				log.include?("INTERVAL: view rendering")
		end
	end
end

end # module PhusionPassenger
