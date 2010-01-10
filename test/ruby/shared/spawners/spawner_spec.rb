require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')
require 'yaml'
require 'etc'

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
	
	specify "spawning multiple times works" do
		sleep 1 # Give previous processes some time to free their memory.
		last_pid = nil
		4.times do
			app = spawn_some_application
			app.pid.should_not == last_pid
			app.app_root.should_not be_nil
			last_pid = app.pid
			app.close
			sleep 0.1  # Give process some time to free memory.
		end
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
	
	when_user_switching_possible do
		before :each do
			before_start %q{
				require 'yaml'
				info = {
					:username => `whoami`.strip,
					:user_id  => `id -u`.strip.to_i,
					:group_id => `id -g`.strip.to_i,
					:groups   => `groups "#{`whoami`.strip}"`.strip,
					:home     => ENV['HOME']
				}
				File.open("dump.yml", 'w') do |f|
					YAML::dump(info, f)
				end
			}
		end
		
		def spawn_some_application_as(options)
			if options[:role]
				user = CONFIG[options.delete(:role)]
			elsif options[:username]
				user = options.delete(:username)
			else
				user = options.delete(:uid).to_s
			end
			@app = spawn_some_application(options) do |stub|
				system("chown", "-R", user, stub.app_root)
			end
		end
		
		def username_for(role)
			return CONFIG[role]
		end
		
		def uid_for(role)
			return Etc.getpwnam(CONFIG[role]).uid
		end
		
		def gid_for(role)
			return Etc.getpwnam(CONFIG[role]).gid
		end
		
		def read_dump
			@dump ||= YAML.load_file("#{@app.app_root}/dump.yml")
		end
		
		def my_username
			return `whoami`.strip
		end
		
		it "lowers the application's privilege to the owner of the startup file" do
			spawn_some_application_as(:role => 'normal_user_1')
			dump_yml_uid = File.stat("#{@app.app_root}/dump.yml").uid
			dump_yml_uid.should == uid_for('normal_user_1')
			read_dump[:username].should == username_for('normal_user_1')
			read_dump[:user_id].should  == uid_for('normal_user_1')
		end
		
		it "switches the group to the owner's primary group" do
			spawn_some_application_as(:role => 'normal_user_1')
			dump_yml_gid = File.stat("#{@app.app_root}/dump.yml").gid
			dump_yml_gid.should == gid_for('normal_user_1')
			read_dump[:group_id].should == gid_for('normal_user_1')
		end
		
		it "switches supplementary groups to the owner's default supplementary groups" do
			spawn_some_application_as(:role => 'normal_user_1')
			default_groups = `groups "#{CONFIG['normal_user_1']}"`.strip
			read_dump[:groups].should == default_groups
		end
		
		it "lowers its privileges to 'lowest_user' if the startup file is owned by root" do
			spawn_some_application_as(:username => 'root')
			read_dump[:username].should == CONFIG['lowest_user']
		end
		
		it "lowers its privileges to 'lowest_user' if the startup file is owned by a nonexistant user" do
			spawn_some_application_as(:uid => CONFIG['nonexistant_uid'])
			read_dump[:username].should == CONFIG['lowest_user']
		end
		
		it "doesn't switch user if the startup file is owned by a nonexistant user, and 'lowest_user' doesn't exist either" do
			spawn_some_application_as(:uid => CONFIG['nonexistant_uid'],
				"lowest_user" => CONFIG['nonexistant_user'])
			read_dump[:username].should == my_username
		end
		
		it "doesn't switch user if 'lower_privilege' is set to false" do
			@app = spawn_some_application("lower_privilege" => false)
			read_dump[:username].should == my_username
		end
		
		it "sets $HOME to the user's home directory, after privilege lowering" do
			spawn_some_application_as(:role => 'normal_user_1')
			read_dump[:home].should == Etc.getpwnam(CONFIG['normal_user_1']).dir
		end
	end
end

#shared_examples_for "a Rack spawner"
#shared_examples_for "a WSGI spawner"
