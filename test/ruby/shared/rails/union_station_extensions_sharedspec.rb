require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')
require 'socket'
require 'fileutils'
PhusionPassenger.require_passenger_lib 'union_station/core'
PhusionPassenger.require_passenger_lib 'utils/tmpdir'

module PhusionPassenger

shared_examples_for "Union Station extensions for Rails" do
	before :each do
		@logging_agent_password = "1234"
		@dump_file = "#{Utils.passenger_tmpdir}/log.txt"
		@agent_pid, @socket_filename, @socket_address = spawn_logging_agent(@dump_file,
			@logging_agent_password)
		@options = {
			"analytics" => true,
			"logging_agent_address" => @socket_address,
			"logging_agent_username" => "logging",
			"logging_agent_password" => "1234",
			"node_name" => "localhost",
			"app_group_name" => "foobar"
		}
	end
	
	after :each do
		@connection.close if @connection && @connection.closed?
		Process.kill('KILL', @agent_pid)
		Process.waitpid(@agent_pid)
	end
	
	def send_request_to_app(headers)
		headers = {
			"PASSENGER_TXN_ID"     => "1234-abcd"
		}.merge(headers)
		return perform_request(headers)
	end
	
	def read_log
		return File.read(@dump_file)
	end
	
	def base64(data)
		return [data].pack('m').gsub("\n", "")
	end
	
	it "doesn't install Union Station extensions if analytics logging is turned off" do
		@options.delete("analytics")
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
				def index
					File.open("out.txt", "w") do |f|
						f.write(request.env["UNION_STATION_REQUEST_TRANSACTION"].class.to_s)
					end
					render :nothing => true
				end
			end
		})
		start!(@options)
		send_request_to_app("PATH_INFO" => "/foo")
		eventually(5) do
			filename = "#{@stub.app_root}/out.txt"
			File.exist?(filename) && File.read(filename) == "NilClass"
		end
	end
	
	it "logs the controller and action name" do
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
				def index
					render :nothing => true
				end
			end
		})
		start!(@options)
		send_request_to_app("PATH_INFO" => "/foo")
		eventually(5) do
			flush_logging_agent(@logging_agent_password, @socket_address)
			log = read_log
			log.include?("Controller action: FooController#index\n")
		end
	end
	
	it "logs uncaught exceptions in controller actions" do
		File.write("#{@stub.app_root}/app/controllers/crash_controller.rb", %Q{
			class CrashController < ActionController::Base
				def index
					raise "something went wrong"
				end
			end
		})
		start!(@options)
		send_request_to_app("PATH_INFO" => "/crash")
		eventually(5) do
			flush_logging_agent(@logging_agent_password, @socket_address)
			log = read_log
			log.include?("Request transaction ID: 1234-abcd\n") &&
				log.include?("Message: " + base64("something went wrong")) &&
				log.include?("Class: RuntimeError") &&
				log.include?("Backtrace: ") &&
				log.include?("Controller action: CrashController#index")
		end
	end
	
	it "logs ActionController benchmarks" do
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
				def index
					if respond_to?(:benchmark, true)
						benchmark("hello") do
						end
					else
						ActionController::Base.benchmark("hello") do
						end
					end
					render :nothing => true
				end
			end
		})
		start!(@options)
		send_request_to_app("PATH_INFO" => "/foo")
		eventually(5) do
			flush_logging_agent(@logging_agent_password, @socket_address)
			log = read_log
			log.include?('BEGIN: BENCHMARK: hello') &&
				log.include?('END: BENCHMARK: hello')
		end
	end
	
	it "logs ActionView benchmarks" do
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
			end
		})
		FileUtils.mkdir_p("#{@stub.app_root}/app/views/foo")
		File.write("#{@stub.app_root}/app/views/foo/index.html.erb", %Q{
			<% benchmark("hello") do %>
			<% end %>
		})
		start!(@options)
		send_request_to_app("PATH_INFO" => "/foo")
		eventually(5) do
			flush_logging_agent(@logging_agent_password, @socket_address)
			log = read_log
			log.include?('BEGIN: BENCHMARK: hello') &&
				log.include?('END: BENCHMARK: hello')
		end
	end
	
	it "logs successful SQL queries" do
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
				def index
					db = ActiveRecord::Base.connection
					db.execute("CREATE TABLE foobar (id INT)")
					db.execute("INSERT INTO foobar VALUES (1)")
					render :nothing => true
				end
			end
		})
		start!(@options.merge("active_record" => true))
		send_request_to_app("PATH_INFO" => "/foo")
		extra_info_regex = Regexp.escape(base64("SQL\nCREATE TABLE foobar (id INT)"))
		eventually(5) do
			flush_logging_agent(@logging_agent_password, @socket_address)
			log = read_log
			log =~ /BEGIN: DB BENCHMARK: .* \(.*\) #{extra_info_regex}$/ &&
				log =~ /END: DB BENCHMARK: .* \(.*\)$/
		end
	end

	it "applies event preprocessor to log events" do
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
				def index
					db = ActiveRecord::Base.connection
					db.execute("CREATE TABLE foobar (id INT)--secret")
					db.execute("INSERT INTO foobar VALUES (1)")
					render :nothing => true
				end
			end
		})
		start!(@options.merge("active_record" => true))
		send_request_to_app("PATH_INFO" => "/foo")
		extra_info_regex = Regexp.escape(base64("SQL\nCREATE TABLE foobar (id INT)--PASSWORD"))
		eventually(5) do
			flush_logging_agent(@logging_agent_password, @socket_address)
			log = read_log
			log =~ /BEGIN: DB BENCHMARK: .* \(.*\) #{extra_info_regex}$/ &&
				log =~ /END: DB BENCHMARK: .* \(.*\)$/
		end
	end
	
	it "logs failed SQL queries" do
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
				def index
					db = ActiveRecord::Base.connection
					db.execute("INVALID QUERY")
					render :nothing => true
				end
			end
		})
		start!(@options.merge("active_record" => true))
		send_request_to_app("PATH_INFO" => "/foo")
		extra_info_regex = Regexp.escape(base64("SQL\nINVALID QUERY"))
		if rails_version >= '3.0'
			pending do
				eventually(5) do
					log = read_log
					log =~ /BEGIN: DB BENCHMARK: .* \(.*\) #{extra_info_regex}$/ &&
						log =~ /FAIL: DB BENCHMARK: .* \(.*\)$/
				end
			end
		else
			eventually(5) do
				flush_logging_agent(@logging_agent_password, @socket_address)
				log = read_log
				log =~ /BEGIN: DB BENCHMARK: .* \(.*\) #{extra_info_regex}$/ &&
					log =~ /FAIL: DB BENCHMARK: .* \(.*\)$/
			end
		end
	end
	
	it "logs controller processing time of successful actions" do
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
				def index
					render :nothing => true
				end
			end
		})
		start!(@options)
		send_request_to_app("PATH_INFO" => "/foo")
		eventually(5) do
			flush_logging_agent(@logging_agent_password, @socket_address)
			log = read_log
			log.include?("BEGIN: framework request processing") &&
				log.include?("END: framework request processing")
		end
	end
	
	it "logs controller processing time of failed actions" do
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
				def index
					raise "crash"
				end
			end
		})
		start!(@options)
		send_request_to_app("PATH_INFO" => "/foo")
		eventually(5) do
			flush_logging_agent(@logging_agent_password, @socket_address)
			log = read_log
			log.include?("BEGIN: framework request processing") &&
				log.include?("FAIL: framework request processing")
		end
	end
	
	it "logs view rendering time of successful actions" do
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
				def index
				end
			end
		})
		FileUtils.mkdir_p("#{@stub.app_root}/app/views/foo")
		File.write("#{@stub.app_root}/app/views/foo/index.html.erb", %Q{
			hello world
		})
		start!(@options)
		send_request_to_app("PATH_INFO" => "/foo")
		eventually(5) do
			flush_logging_agent(@logging_agent_password, @socket_address)
			log = read_log
			log.include?("BEGIN: view rendering") &&
				log.include?("END: view rendering") &&
				log =~ /View rendering time: \d+$/
		end
	end
	
	it "logs view rendering time of failed actions" do
		File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
			class FooController < ActionController::Base
				def index
				end
			end
		})
		FileUtils.mkdir_p("#{@stub.app_root}/app/views/foo")
		File.write("#{@stub.app_root}/app/views/foo/index.html.erb", %Q{
			<% raise "crash!" %>
		})
		start!(@options)
		send_request_to_app("PATH_INFO" => "/foo")
		eventually(5) do
			flush_logging_agent(@logging_agent_password, @socket_address)
			log = read_log
			log.include?("BEGIN: view rendering") &&
				log.include?("FAIL: view rendering")
		end
	end
	
	it "logs cache hits" do
		if rails_version >= '2.1'
			File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
				class FooController < ActionController::Base
					def index
						STDERR.puts 'hi'
						Rails.cache.write("key1", "foo")
						Rails.cache.write("key2", "foo")
						Rails.cache.write("key3", "foo")
						Rails.cache.read("key1")
						Rails.cache.fetch("key2")
						Rails.cache.fetch("key3") { "bar" }
						render :text => 'ok'
					end
				end
			})
			start!(@options)
			send_request_to_app("PATH_INFO" => "/foo")
			eventually(5) do
				flush_logging_agent(@logging_agent_password, @socket_address)
				log = read_log
				log.include?("Cache hit: key1") &&
					log.include?("Cache hit: key2") &&
					log.include?("Cache hit: key3")
			end
		end
	end
	
	it "logs cache misses" do
		if rails_version >= '2.1'
			File.write("#{@stub.app_root}/app/controllers/foo_controller.rb", %Q{
				class FooController < ActionController::Base
					def index
						Rails.cache.read("key1")
						Rails.cache.fetch("key2")
						Rails.cache.fetch("key3") { "bar" }
						render :text => 'ok'
					end
				end
			})
			start!(@options)
			send_request_to_app("PATH_INFO" => "/foo")
			eventually(5) do
				flush_logging_agent(@logging_agent_password, @socket_address)
				log = read_log
				log.include?("Cache miss: key1") &&
					log.include?("Cache miss: key2") &&
					log =~ /Cache miss \(\d+\): key3/
			end
		end
	end
end

end # module PhusionPassenger
