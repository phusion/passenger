require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')
require 'yaml'
require 'etc'

module PhusionPassenger

shared_examples_for "a spawner" do
	def ping_app(app, connect_password)
		if app.server_sockets[:main][1] == "unix"
			client = UNIXSocket.new(app.server_sockets[:main][0])
		else
			addr, port = app.server_sockets[:main][0].split(/:/)
			client = TCPSocket.new(addr, port.to_i)
		end
		begin
			channel = MessageChannel.new(client)
			channel.write_scalar("REQUEST_METHOD\0PING\0PASSENGER_CONNECT_PASSWORD\0#{connect_password}\0")
			return client.read
		ensure
			client.close
		end
	end
	
	it "returns a valid AppProcess object" do
		app = spawn_some_application
		lambda { Process.kill(0, app.pid) }.should_not raise_error
	end
	
	it "sets the working directory of the app to its app root" do
		before_start %q{
			File.touch("cwd.txt")
		}
		app = spawn_some_application
		File.exist?("#{app.app_root}/cwd.txt").should be_true
	end
	
	it "sets ENV['RAILS_ENV'] and ENV['RACK_ENV']" do
		before_start %q{
			File.write("rails_env.txt", ENV['RAILS_ENV'])
			File.write("rack_env.txt", ENV['RACK_ENV'])
		}
		app = spawn_some_application("environment" => "staging")
		File.read("#{app.app_root}/rails_env.txt").should == "staging"
		File.read("#{app.app_root}/rack_env.txt").should == "staging"
	end
	
	it "sets ENV['RAILS_RELATIVE_URL_ROOT'] and ENV['RACK_BASE_URI'] if the 'base_uri' option is set to a valid value" do
		before_start %q{
			File.write("rails_relative_url_root.txt", ENV['RAILS_RELATIVE_URL_ROOT'])
			File.write("rack_base_uri.txt", ENV['RACK_BASE_URI'])
		}
		app = spawn_some_application("base_uri" => "/foo")
		File.read("#{app.app_root}/rails_relative_url_root.txt").should == "/foo"
		File.read("#{app.app_root}/rack_base_uri.txt").should == "/foo"
	end
	
	it "doesn't set ENV['RAILS_RELATIVE_URL_ROOT'] and ENV['RACK_BASE_URI'] if 'base_uri' is not given" do
		before_start %q{
			if ENV['RAILS_RELATIVE_URL_ROOT']
				File.touch("rails_relative_url_root.txt")
			end
			if ENV['RACK_BASE_URI']
				File.touch("rack_base_uri.txt")
			end
		}
		app = spawn_some_application
		File.exist?("#{app.app_root}/rails_relative_url_root.txt").should be_false
		File.exist?("#{app.app_root}/rack_base_uri.txt").should be_false
	end
	
	it "doesn't set ENV['RAILS_RELATIVE_URL_ROOT'] and ENV['RACK_BASE_URI'] if 'base_uri' is empty" do
		before_start %q{
			if ENV['RAILS_RELATIVE_URL_ROOT']
				File.touch("rails_relative_url_root.txt")
			end
			if ENV['RACK_BASE_URI']
				File.touch("rack_base_uri.txt")
			end
		}
		app = spawn_some_application("base_uri" => "")
		File.exist?("#{app.app_root}/rails_relative_url_root.txt").should be_false
		File.exist?("#{app.app_root}/rack_base_uri.txt").should be_false
	end
	
	it "doesn't set ENV['RAILS_RELATIVE_URL_ROOT'] and ENV['RACK_BASE_URI'] if 'base_uri' is '/'" do
		before_start %q{
			if ENV['RAILS_RELATIVE_URL_ROOT']
				File.touch("rails_relative_url_root.txt")
			end
			if ENV['RACK_BASE_URI']
				File.touch("rack_base_uri.txt")
			end
		}
		app = spawn_some_application("base_uri" => "/")
		File.exist?("#{app.app_root}/rails_relative_url_root.txt").should be_false
		File.exist?("#{app.app_root}/rack_base_uri.txt").should be_false
	end
	
	it "sets the environment variables in the 'environment_variables' option" do
		before_start %q{
			File.open("env.txt", "w") do |f|
				f.puts
				ENV.each_pair do |key, value|
					f.puts("#{key} = #{value}")
				end
			end
		}
		
		env_vars_string = "PATH\0/usr/bin:/opt/sw/bin\0FOO\0foo bar!\0"
		options = { "environment_variables" => [env_vars_string].pack("m") }
		app = spawn_some_application(options)
		
		contents = File.read("#{app.app_root}/env.txt")
		contents.should =~ %r(\nPATH = /usr/bin:/opt/sw/bin\n)
		contents.should =~ %r(\nFOO = foo bar\!\n)
	end
	
	it "does not cache things like the connect password" do
		app1 = spawn_some_application("connect_password" => "1234")
		app2 = spawn_some_application("connect_password" => "5678")
		ping_app(app1, "1234").should == "pong"
		ping_app(app2, "5678").should == "pong"
	end
	
	it "calls the starting_worker_process event after the startup file has been loaded" do
		after_start %q{
			history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
			PhusionPassenger.on_event(:starting_worker_process) do
				::File.append(history_file, "worker_process_started\n")
			end
			::File.append(history_file, "end of startup file\n");
		}
		spawn_some_application.close
		app = spawn_some_application
		app.close
		
		history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
		eventually do
			contents = File.read(history_file)
			lines = contents.split("\n")
			lines[0] == "end of startup file" &&
				lines.count("worker_process_started") == 2
		end
	end
	
	it "calls the stopping_worker_process event" do
		after_start %q{
			history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
			PhusionPassenger.on_event(:stopping_worker_process) do
				::File.append(history_file, "worker_process_stopped\n")
			end
			::File.append(history_file, "end of startup file\n");
		}
		spawn_some_application.close
		app = spawn_some_application
		app.close
		
		history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
		eventually do
			contents = File.read(history_file)
			lines = contents.split("\n")
			lines[0] == "end of startup file" &&
				lines.count("worker_process_stopped") == 2
		end
	end
	
	it "calls #at_exit blocks upon exiting" do
		before_start %q{
			history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
			at_exit do
				File.open(history_file, "a") do |f|
					f.puts "at_exit 1"
				end
			end
			at_exit do
				File.open(history_file, "a") do |f|
					f.puts "at_exit 2"
				end
			end
		}
		
		spawn_some_application.close
		history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
		eventually do
			File.exist?(history_file) &&
			File.read(history_file) ==
				"at_exit 2\n" +
				"at_exit 1\n"
		end
	end
	
	it "lowers privilege using Utils#lower_privilege" do
		filename = "#{PhusionPassenger::Utils.passenger_tmpdir}/called.txt"
		PhusionPassenger::Utils.stub!(:lower_privilege_called).and_return do
			File.touch(filename)
		end
		spawn_some_application.close
		eventually do
			File.exist?(filename).should be_true
		end
	end
	
	describe "error handling" do
		it "raises an AppInitError if the spawned app raises a standard exception during startup" do
			before_start %q{
				raise 'This is a dummy exception.'
			}
			begin
				spawn_some_application("print_exceptions" => false)
				violated "Spawning the application should have raised an AppInitError."
			rescue AppInitError => e
				e.child_exception.message.should == "This is a dummy exception."
			end
		end
		
		it "raises an AppInitError if the spawned app raises a custom-defined exception during startup" do
			before_start %q{
				class MyError < StandardError
				end
				
				raise MyError, "This is a custom exception."
			}
			begin
				spawn_some_application("print_exceptions" => false)
				violated "Spawning the application should have raised an AppInitError."
			rescue AppInitError => e
				e.child_exception.message.should == "This is a custom exception. (MyError)"
			end
		end
		
		it "raises an AppInitError if the spawned app calls exit() during startup" do
			before_start %q{
				exit
			}
			begin
				spawn_some_application("print_exceptions" => false).close
				violated "Spawning the application should have raised an AppInitError."
			rescue AppInitError => e
				e.child_exception.class.should == SystemExit
			end
		end
		
		it "prints the exception to STDERR if the spawned app raised an error" do
			old_stderr = STDERR
			file = File.new('output.tmp', 'w+')
			begin
				Object.send(:remove_const, "STDERR") rescue nil
				Object.const_set("STDERR", file)
				
				before_start %q{
					def dummy_function
						raise 'This is a dummy exception.'
					end
					dummy_function
				}
				block = lambda { spawn_some_application }
				block.should raise_error(AppInitError)
				
				file.rewind
				data = file.read
				data.should =~ /This is a dummy exception/
				data.should =~ /dummy_function/
			ensure
				Object.send(:remove_const, "STDERR") rescue nil
				Object.const_set("STDERR", old_stderr)
				file.close rescue nil
				File.unlink('output.tmp') rescue nil
			end
		end
	end
end

end # module PhusionPassenger
