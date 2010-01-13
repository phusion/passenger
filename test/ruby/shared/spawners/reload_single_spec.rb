require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')

shared_examples_for "a Rails spawner that supports #reload(app_root)" do
	it "#reload(app_root) reloads a specific application" do
		use_some_stub do |stub1|
		use_some_stub do |stub2|
			File.append(stub1.startup_file, %q{
				File.write("output.txt", "stub 1")
			})
			spawn_stub_application(stub1).close
			File.append(stub2.startup_file, %q{
				File.write("output.txt", "stub 2")
			})
			spawn_stub_application(stub2).close
			
			spawner.reload(stub1.app_root)
			
			File.append(stub1.startup_file, %q{
				File.write("output.txt", "stub 1 modified")
			})
			spawn_stub_application(stub1).close
			File.append(stub2.startup_file, %q{
				File.write("output.txt", "stub 2 modified")
			})
			spawn_stub_application(stub2).close
			
			File.read("#{stub1.app_root}/output.txt").should == "stub 1 modified"
			File.read("#{stub2.app_root}/output.txt").should == "stub 2"
		end
		end
	end
end
