require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')

module PhusionPassenger

shared_examples_for "a Rails spawner that supports #reload(app_group_name)" do
	it "#reload(app_group_name) reloads a specific application" do
		use_some_stub do |stub1|
		use_some_stub do |stub2|
			File.append(stub1.startup_file, %q{
				File.write("output.txt", "stub 1, variant #{ENV['VARIANT']}")
			})
			File.append(stub2.startup_file, %q{
				File.write("output.txt", "stub 2")
			})
			
			spawn_stub_application(stub1,
				"app_group_name" => "stub 1, variant A",
				"environment_variables" => ["VARIANT\0A\0"].pack('m')
			).close
			spawn_stub_application(stub1,
				"app_group_name" => "stub 1, variant B",
				"environment_variables" => ["VARIANT\0B\0"].pack('m')
			).close
			spawn_stub_application(stub2).close
			
			spawner.reload("stub 1, variant A")
			
			File.append(stub1.startup_file, %q{
				File.write("output.txt", "stub 1 modified, variant #{ENV['VARIANT']}")
			})
			File.append(stub2.startup_file, %q{
				File.write("output.txt", "stub 2 modified")
			})
			
			spawn_stub_application(stub1,
				"app_group_name" => "stub 1, variant A",
				"environment_variables" => ["VARIANT\0A\0"].pack('m')
			).close
			spawn_stub_application(stub1,
				"app_group_name" => "stub 1, variant B",
				"environment_variables" => ["VARIANT\0B\0"].pack('m')
			).close
			spawn_stub_application(stub2).close
			
			File.read("#{stub1.app_root}/output.txt").should == "stub 1 modified, variant A"
			File.read("#{stub2.app_root}/output.txt").should == "stub 2"
		end
		end
	end
end

end # module PhusionPassenger
