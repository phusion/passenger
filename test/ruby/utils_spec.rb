require 'support/config'

require 'tmpdir'
require 'fileutils'
require 'phusion_passenger/utils'

include PhusionPassenger

describe Utils do
	include Utils
	
	specify "#close_all_io_objects_for_fds closes all IO objects that are associated with the given file descriptors" do
		filename = "#{Dir.tmpdir}/passenger_test.#{Process.pid}.txt"
		puts "#{$$}: 1"
		begin
			pid = fork do
				begin
					puts "#{$$}: 2"
					a, b = IO.pipe
					puts "#{$$}: 3"
					close_all_io_objects_for_fds([0, 1, 2])
					puts "#{$$}: 4"
					File.open(filename, "w") do |f|
						f.write("#{a.closed?}, #{b.closed?}")
					end
					puts "#{$$}: 5"
				rescue Exception => e
					print_exception("utils_spec", e)
				ensure
					puts "#{$$}: 6"
					exit!
				end
			end
			puts "#{$$}: 6"
			Process.waitpid(pid) rescue nil
			puts "#{$$}: 7"
			File.read(filename).should == "true, true"
		ensure
			puts "#{$$}: 8"
			File.unlink(filename) rescue nil
		end
	end
	
	describe "#passenger_tmpdir" do
		before :each do
			@old_passenger_tmpdir = Utils.passenger_tmpdir
			Utils.passenger_tmpdir = nil
		end
		
		after :each do
			Utils.passenger_tmpdir = @old_passenger_tmpdir
		end
		
		it "returns a directory under Dir.tmpdir if Utils.passenger_tmpdir is nil" do
			File.dirname(passenger_tmpdir(false)).should == Dir.tmpdir
		end
		
		it "returns a directory under Dir.tmpdir if Utils.passenger_tmpdir is an empty string" do
			Utils.passenger_tmpdir = ''
			File.dirname(passenger_tmpdir(false)).should == Dir.tmpdir
		end
		
		it "returns Utils.passenger_tmpdir if it's set" do
			Utils.passenger_tmpdir = '/foo'
			passenger_tmpdir(false).should == '/foo'
		end
		
		it "creates the directory if it doesn't exist, if the 'create' argument is true" do
			Utils.passenger_tmpdir = 'utils_spec.tmp'
			passenger_tmpdir
			begin
				File.directory?('utils_spec.tmp').should be_true
			ensure
				FileUtils.rm_rf('utils_spec.tmp')
			end
		end
	end
end
