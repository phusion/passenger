require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'fileutils'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'utils'
PhusionPassenger.require_passenger_lib 'admin_tools'
PhusionPassenger.require_passenger_lib 'admin_tools/server_instance'

module PhusionPassenger

describe AdminTools do
	include Utils
	
	before :each do
		Dir.mkdir("#{passenger_tmpdir}/master")
	end
end

describe AdminTools::ServerInstance do
	include Utils
	
	before :each do
		File.chmod(0700, passenger_tmpdir)
	end
	
	after :each do
		if @process1
			Process.kill('KILL', @process1.pid)
			@process1.close
		end
		if @process2
			Process.kill('KILL', @process2.pid)
			@process2.close
		end
		if @process3
			Process.kill('KILL', @process3.pid)
			@process3.close
		end
	end
	
	def spawn_process
		IO.popen("sleep 999")
	end
	
	def create_instance_dir(pid, major = PhusionPassenger::SERVER_INSTANCE_DIR_STRUCTURE_MAJOR_VERSION, minor = PhusionPassenger::SERVER_INSTANCE_DIR_STRUCTURE_MINOR_VERSION)
		dir = "#{passenger_tmpdir}/passenger.#{major}.#{minor}.#{pid}"
		Dir.mkdir(dir)
		return dir
	end
	
	def create_generation(dir, number = 0, major = PhusionPassenger::SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MAJOR_VERSION, minor = PhusionPassenger::SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MINOR_VERSION)
		dir = "#{dir}/generation-#{number}"
		Dir.mkdir(dir)
		File.write("#{dir}/structure_version.txt", "#{major}.#{minor}")
		return dir
	end
	
	describe ".list" do
		before :each do
			AdminTools.should_receive(:tmpdir).and_return(passenger_tmpdir)
			AdminTools::ServerInstance.stub(:current_time).
				and_return(Time.now + AdminTools::ServerInstance::STALE_TIME_THRESHOLD + 1)
		end
		
		it "returns a list of ServerInstances representing the running Phusion Passenger instances" do
			@process1 = spawn_process
			@process2 = spawn_process
			processes = [@process1, @process2].sort { |a, b| a.pid <=> b.pid }
			
			dir1 = create_instance_dir(@process1.pid)
			create_generation(dir1)
			dir2 = create_instance_dir(@process2.pid)
			create_generation(dir2)
			
			instances = AdminTools::ServerInstance.list.sort { |a, b| a.pid <=> b.pid }
			instances.should have(2).items
			instances[0].pid.should == processes[0].pid
			instances[1].pid.should == processes[1].pid
		end
		
		it "doesn't list directories that don't look like Phusion Passenger server instance directories" do
			@process1 = spawn_process
			
			dir = create_instance_dir(@process1.pid)
			create_generation(dir)
			
			Dir.mkdir("#{passenger_tmpdir}/foo.123")
			create_generation("#{passenger_tmpdir}/foo.123")
			
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
		end
		
		it "doesn't list server instance directories that have a different major structure version" do
			@process1 = spawn_process
			@process2 = spawn_process
			
			dir1 = create_instance_dir(@process1.pid, 0)
			create_generation(dir1)
			dir2 = create_instance_dir(@process2.pid)
			create_generation(dir2)
			
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process2.pid
		end
		
		it "doesn't list server instance directories that have the same major structure version but a larger minor structure version" do
			@process1 = spawn_process
			@process2 = spawn_process
			
			dir1 = create_instance_dir(@process1.pid, SERVER_INSTANCE_DIR_STRUCTURE_MAJOR_VERSION,
				SERVER_INSTANCE_DIR_STRUCTURE_MINOR_VERSION + 1)
			create_generation(dir1)
			dir2 = create_instance_dir(@process2.pid)
			create_generation(dir2)
			
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process2.pid
		end
		
		it "doesn't list server instance directories with no generations" do
			@process1 = spawn_process
			create_instance_dir(@process1.pid)
			AdminTools::ServerInstance.list.should be_empty
		end
		
		it "doesn't list server instance directories for which the newest generation has a different major version" do
			@process1 = spawn_process
			@process2 = spawn_process
			dir1 = create_instance_dir(@process1.pid)
			create_generation(dir1, 0, SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MAJOR_VERSION)
			create_generation(dir1, 1, SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MINOR_VERSION + 1)
			dir2 = create_instance_dir(@process2.pid)
			create_generation(dir2)
			
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process2.pid
		end
		
		it "doesn't list server instance directories for which the newest generation has the same major version but a larger minor version" do
			@process1 = spawn_process
			@process2 = spawn_process
			dir1 = create_instance_dir(@process1.pid)
			create_generation(dir1, 0, SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MAJOR_VERSION)
			create_generation(dir1, 1, SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MAJOR_VERSION + 1,
				SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MINOR_VERSION + 1)
			dir2 = create_instance_dir(@process2.pid)
			create_generation(dir2)
			
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process2.pid
		end
		
		it "cleans up server instance directories for which its PID doesn't exist" do
			@process1 = spawn_process
			process2 = spawn_process
			process2_pid = process2.pid
			process3 = spawn_process
			process3_pid = process3.pid
			
			dir1 = create_instance_dir(@process1.pid)
			create_generation(dir1)
			dir2 = create_instance_dir(process2_pid)
			create_generation(dir2)
			dir3 = create_instance_dir(process3_pid + 1)
			create_generation(dir3)
			File.write("#{dir3}/control_process.pid", process3_pid)
			
			Process.kill('KILL', process2_pid) rescue nil
			process2.close
			Process.kill('KILL', process3_pid) rescue nil
			process3.close
			
			AdminTools::ServerInstance.should_receive(:log_cleaning_action).twice
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?(dir2).should be_false
			File.exist?(dir3).should be_false
		end
		
		it "doesn't clean up server instance directories for which the major structure version is different" do
			process1 = spawn_process
			dir1 = create_instance_dir(process1.pid, SERVER_INSTANCE_DIR_STRUCTURE_MAJOR_VERSION + 1)
			create_generation(dir1)
			Process.kill('KILL', process1.pid) rescue nil
			process1.close
			
			AdminTools::ServerInstance.should_not_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list
			instances.should be_empty
			File.exist?(dir1).should be_true
		end
		
		it "doesn't clean up server instance directories for which the major structure version is the same but the minor structure version is larger" do
			process1 = spawn_process
			dir1 = create_instance_dir(process1.pid, SERVER_INSTANCE_DIR_STRUCTURE_MAJOR_VERSION, SERVER_INSTANCE_DIR_STRUCTURE_MINOR_VERSION + 1)
			create_generation(dir1)
			Process.kill('KILL', process1.pid) rescue nil
			process1.close
			
			AdminTools::ServerInstance.should_not_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list
			instances.should be_empty
			File.exist?(dir1).should be_true
		end
		
		it "doesn't clean up server instance directories for which the latest generation has a different major version" do
			process1 = spawn_process
			dir1 = create_instance_dir(process1.pid)
			create_generation(dir1, 0, SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MAJOR_VERSION + 1)
			Process.kill('KILL', process1.pid) rescue nil
			process1.close
			
			AdminTools::ServerInstance.should_not_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list
			instances.should be_empty
			File.exist?(dir1).should be_true
		end
		
		it "doesn't clean up server instance directories for which the latest generation has the same major version but a larger minor version" do
			process1 = spawn_process
			dir1 = create_instance_dir(process1.pid)
			create_generation(dir1, 0, SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MAJOR_VERSION, SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MINOR_VERSION + 1)
			Process.kill('KILL', process1.pid) rescue nil
			process1.close
			
			AdminTools::ServerInstance.should_not_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list
			instances.should be_empty
			File.exist?(dir1).should be_true
		end
		
		it "cleans up server instance directories that contain a corrupted control_process.pid" do
			@process1 = spawn_process
			@process2 = spawn_process
			
			dir1 = create_instance_dir(@process1.pid)
			create_generation(dir1)
			dir2 = create_instance_dir(@process2.pid)
			create_generation(dir2)
			File.write("#{dir2}/control_process.pid", "")
			
			AdminTools::ServerInstance.should_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?(dir2).should be_false
		end
		
		it "cleans up server instance directories for which the latest generation has a corrupted structure_version.txt" do
			@process1 = spawn_process
			@process2 = spawn_process
			@process3 = spawn_process
			
			dir1 = create_instance_dir(@process1.pid)
			create_generation(dir1)
			dir2 = create_instance_dir(@process2.pid)
			generation2 = create_generation(dir2)
			File.write("#{generation2}/structure_version.txt", "")
			dir3 = create_instance_dir(@process3.pid)
			generation3 = create_generation(dir3)
			File.write("#{generation3}/structure_version.txt", "1.x")
			
			AdminTools::ServerInstance.should_receive(:log_cleaning_action).twice
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?(dir2).should be_false
			File.exist?(dir3).should be_false
		end
		
		it "cleans up server instance directories for which the latest generation doesn't have a structure_version.txt" do
			@process1 = spawn_process
			@process2 = spawn_process
			
			dir1 = create_instance_dir(@process1.pid)
			create_generation(dir1)
			dir2 = create_instance_dir(@process2.pid)
			generation2 = create_generation(dir2)
			File.unlink("#{generation2}/structure_version.txt")
			
			AdminTools::ServerInstance.should_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?(dir2).should be_false
		end
		
		it "only cleans up an instance directory if it was modified more than STALE_TIME_THRESHOLD seconds ago" do
			@process1 = spawn_process
			@process2 = spawn_process
			creation_time = Time.now
			list_time = Time.now + AdminTools::ServerInstance::STALE_TIME_THRESHOLD + 1
			
			# This directory was created more than STALE_TIME_THRESHOLD secs ago, but
			# modified just now.
			dir1 = create_instance_dir(@process1.pid)
			generation1 = create_generation(dir1)
			File.unlink("#{generation1}/structure_version.txt")
			File.utime(creation_time, list_time, dir1)
			
			# This directory was created and modified more than STALE_TIME_THRESHOLD
			# secs ago.
			dir2 = create_instance_dir(@process2.pid)
			generation2 = create_generation(dir2)
			File.unlink("#{generation2}/structure_version.txt")
			File.utime(creation_time, creation_time, dir2)
			
			AdminTools::ServerInstance.should_receive(:log_cleaning_action)
			AdminTools::ServerInstance.should_receive(:current_time).and_return(list_time)
			instances = AdminTools::ServerInstance.list
			instances.should have(0).items
			File.exist?(dir1).should be_true
			File.exist?(dir2).should be_false
		end
		
		it "does not clean up instance directories if clean_stale_or_corrupted is false" do
			@process1 = spawn_process
			@process2 = spawn_process
			
			dir1 = create_instance_dir(@process1.pid)
			create_generation(dir1)
			dir2 = create_instance_dir(@process2.pid)
			create_generation(dir2)
			File.write("#{dir2}/control_process.pid", "")
			
			AdminTools::ServerInstance.should_not_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list(:clean_stale_or_corrupted => false)
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?(dir2).should be_true
		end
	end
	
	describe "#pid" do
		before :each do
			@process1 = spawn_process
		end
		
		it "returns the PID in the directory filename if instance.pid doesn't exist" do
			dir = create_instance_dir(@process1.pid)
			create_generation(dir)
			AdminTools::ServerInstance.new(dir).pid.should == @process1.pid
		end
		
		it "returns the PID in control_process.pid if it exists" do
			dir = create_instance_dir(@process1.pid + 1)
			create_generation(dir)
			File.write("#{dir}/control_process.pid", @process1.pid)
			AdminTools::ServerInstance.new(dir).pid.should == @process1.pid
		end
	end
end

end # module PhusionPassenger
