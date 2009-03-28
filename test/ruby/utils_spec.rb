require 'support/config'

require 'tempfile'
require 'phusion_passenger/utils'

include PhusionPassenger

describe Utils do
	include Utils
	
	specify "#close_all_io_objects_for_fds closes all IO objects that are associated with the given file descriptors" do
		filename = "#{Dir.tmpdir}/passenger_test.#{Process.pid}.txt"
		begin
			pid = fork do
				begin
					a, b = IO.pipe
					close_all_io_objects_for_fds([0, 1, 2])
					File.open(filename, "w") do |f|
						f.write("#{a.closed?}, #{b.closed?}")
					end
				rescue Exception => e
					print_exception("utils_spec", e)
				ensure
					exit!
				end
			end
			Process.waitpid(pid) rescue nil
			File.read(filename).should == "true, true"
		ensure
			File.unlink(filename) rescue nil
		end
	end
	
	describe "#passenger_tmpdir" do
		before :each do
			ENV.delete('PASSENGER_INSTANCE_TEMP_DIR')
		end
		
		after :each do
			ENV.delete('PASSENGER_INSTANCE_TEMP_DIR')
		end
		
		it "returns a directory under Dir.tmpdir if ENV['PASSENGER_INSTANCE_TEMP_DIR'] is nil" do
			File.dirname(passenger_tmpdir(false)).should == Dir.tmpdir
		end
		
		it "returns a directory under Dir.tmpdir if ENV['PASSENGER_INSTANCE_TEMP_DIR'] is an empty string" do
			ENV['PASSENGER_INSTANCE_TEMP_DIR'] = ''
			File.dirname(passenger_tmpdir(false)).should == Dir.tmpdir
		end
		
		it "returns ENV['PASSENGER_INSTANCE_TEMP_DIR'] if it's set" do
			ENV['PASSENGER_INSTANCE_TEMP_DIR'] = '/foo'
			passenger_tmpdir(false).should == '/foo'
		end
		
		it "creates the directory if it doesn't exist, if the 'create' argument is true" do
			ENV['PASSENGER_INSTANCE_TEMP_DIR'] = 'utils_spec.tmp'
			passenger_tmpdir
			begin
				File.directory?('utils_spec.tmp').should be_true
			ensure
				Dir.rmdir('utils_spec.tmp') rescue nil
			end
		end
	end
end
