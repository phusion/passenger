shared_examples_for "a minimal spawner" do
	it "can spawn our stub application" do
		use_rails_stub('foobar') do |stub|
			app = spawn_stub_application(stub)
			app.pid.should_not == 0
			app.app_root.should_not be_nil
			app.close
		end
	end
	
	it "can spawn an arbitary number of applications" do
		use_rails_stub('foobar') do |stub|
			last_pid = 0
			4.times do
				app = spawn_stub_application(stub)
				app.pid.should_not == last_pid
				app.app_root.should_not be_nil
				last_pid = app.pid
				app.close
			end
		end
	end
	
	it "respects ENV['RAILS_ENV']= in environment.rb" do
		use_rails_stub('foobar') do |stub|
			File.prepend(stub.environment_rb, "ENV['RAILS_ENV'] = 'development'\n")
			File.append(stub.environment_rb, %q{
				File.open('environment.txt', 'w') do |f|
					f.write(RAILS_ENV)
				end
			})
			spawn_stub_application(stub).close
			environment = File.read("#{stub.app_root}/environment.txt")
			environment.should == "development"
		end
	end
	
	it "does not conflict with models in the application that are named 'Passenger'" do
		use_rails_stub('foobar') do |stub|
			if !File.directory?("#{stub.app_root}/app/models")
				Dir.mkdir("#{stub.app_root}/app/models")
			end
			File.open("#{stub.app_root}/app/models/passenger.rb", 'w') do |f|
				f.write(%q{
					class Passenger
						def name
							return "Gourry Gabriev"
						end
					end
				})
			end
			File.append(stub.environment_rb, %q{
				# We explicitly call 'require' here because we might be
				# using a stub Rails framework (that doesn't support automatic
				# loading of model source files).
				require 'app/models/passenger'
				File.open('passenger.txt', 'w') do |f|
					f.write(Passenger.new.name)
				end
			})
			spawn_stub_application(stub).close
			passenger_name = File.read("#{stub.app_root}/passenger.txt")
			passenger_name.should == 'Gourry Gabriev'
		end
	end
	
	it "loads application_controller.rb instead of application.rb, if the former exists" do
		use_rails_stub('foobar') do |stub|
			File.rename("#{stub.app_root}/app/controllers/application.rb",
				"#{stub.app_root}/app/controllers/application_controller.rb")
			lambda { spawn_stub_application(stub).close }.should_not raise_error
		end
	end
	
	it "sets the environment variables passed in the environment_variables option" do
		use_rails_stub('foobar') do |stub|
			File.append(stub.environment_rb, %q{
				File.open("env.txt", "w") do |f|
					ENV.each_pair do |key, value|
						f.puts("#{key} = #{value}")
					end
				end
			})
			env_vars_string = "PATH\0/usr/bin:/opt/sw/bin\0FOO\0foo bar!\0"
			options = { "environment_variables" => [env_vars_string].pack("m") }
			spawn_stub_application(stub, options).close
			
			contents = File.read("#{stub.app_root}/env.txt")
			contents.should =~ %r(PATH = /usr/bin:/opt/sw/bin\n)
			contents.should =~ %r(FOO = foo bar\!\n)
		end
	end
end
