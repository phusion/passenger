require 'support/config'
require 'support/test_helper'

require 'fileutils'
require 'phusion_passenger/utils'
require 'phusion_passenger/admin_tools'
require 'phusion_passenger/admin_tools/server_instance'

include PhusionPassenger

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
	
	def create_instance_dir(dir)
		Dir.mkdir(dir)
		File.write("#{dir}/structure_version.txt",
			AdminTools::ServerInstance::DIRECTORY_STRUCTURE_MAJOR_VERSION.to_s + "," +
			AdminTools::ServerInstance::DIRECTORY_STRUCTURE_MINOR_VERSION.to_s)
	end
	
	describe ".list" do
		before :each do
			AdminTools.should_receive(:tmpdir).and_return(passenger_tmpdir)
			AdminTools::ServerInstance.stub!(:current_time).
				and_return(Time.now + AdminTools::ServerInstance::STALE_TIME_THRESHOLD + 1)
		end
		
		it "returns a list of ServerInstances representing the running Phusion Passenger instances" do
			@process1 = spawn_process
			@process2 = spawn_process
			processes = [@process1, @process2].sort { |a, b| a.pid <=> b.pid }
			
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process1.pid}")
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process2.pid}")
			instances = AdminTools::ServerInstance.list.sort { |a, b| a.pid <=> b.pid }
			instances.should have(2).items
			instances[0].pid.should == @process1.pid
			instances[1].pid.should == @process2.pid
		end
		
		it "ignores directories that don't look like Phusion Passenger instance directories" do
			@process1 = spawn_process
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process1.pid}")
			Dir.mkdir("#{passenger_tmpdir}/foo.123")
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
		end
		
		it "ignores instance directories that have a different major structure version" do
			@process1 = spawn_process
			@process2 = spawn_process
			
			dir = "#{passenger_tmpdir}/passenger.#{@process1.pid}"
			create_instance_dir(dir)
			File.write("#{dir}/structure_version.txt", "0,0")
			
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process2.pid}")
			
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process2.pid
		end
		
		it "ignores instance directories that have the same major structure version but a larger minor structure version" do
			@process1 = spawn_process
			@process2 = spawn_process
			
			dir = "#{passenger_tmpdir}/passenger.#{@process1.pid}"
			create_instance_dir(dir)
			File.write("#{dir}/structure_version.txt",
				AdminTools::ServerInstance::DIRECTORY_STRUCTURE_MAJOR_VERSION.to_s + "," +
				"9" + AdminTools::ServerInstance::DIRECTORY_STRUCTURE_MINOR_VERSION.to_s)
			
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process2.pid}")
			
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process2.pid
		end
		
		it "cleans up server instance directories for which its PID doesn't exist" do
			@process1 = spawn_process
			process2 = spawn_process
			process2_pid = process2.pid
			
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process1.pid}")
			create_instance_dir("#{passenger_tmpdir}/passenger.#{process2_pid}")
			
			Process.kill('KILL', process2_pid) rescue nil
			process2.close
			
			AdminTools::ServerInstance.should_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?("#{passenger_tmpdir}/passenger.#{process2_pid}").should be_false
		end
		
		it "cleans up server instance directories that don't contain structure_version.txt" do
			@process1 = spawn_process
			@process2 = spawn_process
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process1.pid}")
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process2.pid}")
			File.unlink("#{passenger_tmpdir}/passenger.#{@process2.pid}/structure_version.txt")
			
			AdminTools::ServerInstance.should_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?("#{passenger_tmpdir}/passenger.#{@process2.pid}").should be_false
		end
		
		it "cleans up server instance directories that contain a corrupted instance.pid" do
			@process1 = spawn_process
			@process2 = spawn_process
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process1.pid}")
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process2.pid}")
			File.write("#{passenger_tmpdir}/passenger.#{@process2.pid}/instance.pid", "")
			
			AdminTools::ServerInstance.should_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?("#{passenger_tmpdir}/passenger.#{@process2.pid}").should be_false
		end
		
		it "cleans up server instance directories that contain a corrupted structure_version.txt" do
			@process1 = spawn_process
			@process2 = spawn_process
			@process3 = spawn_process
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process1.pid}")
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process2.pid}")
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process3.pid}")
			File.write("#{passenger_tmpdir}/passenger.#{@process2.pid}/structure_version.txt", "")
			File.write("#{passenger_tmpdir}/passenger.#{@process3.pid}/structure_version.txt", "1,x")
			
			AdminTools::ServerInstance.should_receive(:log_cleaning_action).twice
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?("#{passenger_tmpdir}/passenger.#{@process2.pid}").should be_false
			File.exist?("#{passenger_tmpdir}/passenger.#{@process3.pid}").should be_false
		end
		
		it "only cleans up an instance directory if it was created more than STALE_TIME_THRESHOLD seconds ago" do
			@process1 = spawn_process
			@process2 = spawn_process
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process1.pid}")
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process2.pid}")
			File.unlink("#{passenger_tmpdir}/passenger.#{@process2.pid}/structure_version.txt")
			
			AdminTools::ServerInstance.should_not_receive(:log_cleaning_action)
			AdminTools::ServerInstance.should_receive(:current_time).and_return(Time.now)
			instances = AdminTools::ServerInstance.list
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?("#{passenger_tmpdir}/passenger.#{@process2.pid}").should be_true
		end
		
		it "does not clean up instance directories if clean_stale_or_corrupted is false" do
			@process1 = spawn_process
			@process2 = spawn_process
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process1.pid}")
			create_instance_dir("#{passenger_tmpdir}/passenger.#{@process2.pid}")
			File.write("#{passenger_tmpdir}/passenger.#{@process2.pid}/instance.pid", "")
			
			AdminTools::ServerInstance.should_not_receive(:log_cleaning_action)
			instances = AdminTools::ServerInstance.list(false)
			instances.should have(1).item
			instances[0].pid.should == @process1.pid
			File.exist?("#{passenger_tmpdir}/passenger.#{@process2.pid}").should be_true
		end
	end
	
	describe "#pid" do
		before :each do
			@process1 = spawn_process
		end
		
		it "returns the PID in the directory filename if instance.pid doesn't exist" do
			dir = "#{passenger_tmpdir}/passenger.#{@process1.pid}"
			create_instance_dir(dir)
			AdminTools::ServerInstance.new(dir).pid.should == @process1.pid
		end
		
		it "returns the PID in instance.pid if it exists" do
			dir = "#{passenger_tmpdir}/passenger.#{@process1.pid + 1}"
			create_instance_dir(dir)
			File.write("#{dir}/instance.pid", @process1.pid)
			AdminTools::ServerInstance.new(dir).pid.should == @process1.pid
		end
	end
end