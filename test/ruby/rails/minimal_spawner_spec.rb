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
	
	it "loads application_controller.rb instead of application.rb, if the former exists" do
		use_rails_stub('foobar') do |stub|
			File.rename("#{stub.app_root}/app/controllers/application.rb",
				"#{stub.app_root}/app/controllers/application_controller.rb")
			lambda { spawn_stub_application(stub).close }.should_not raise_error
		end
	end
end
