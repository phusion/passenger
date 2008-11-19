require 'support/config'

require 'tempfile'
require 'passenger/utils'

include Passenger

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
			ENV.delete('PHUSION_PASSENGER_TMP')
		end
		
		after :each do
			ENV.delete('PHUSION_PASSENGER_TMP')
		end
		
		it "returns Dir.tmpdir if ENV['PHUSION_PASSENGER_TMP'] is nil" do
			passenger_tmpdir(false).should == Dir.tmpdir
		end
		
		it "returns Dir.tmpdir if ENV['PHUSION_PASSENGER_TMP'] is an empty string" do
			ENV['PHUSION_PASSENGER_TMP'] = ''
			passenger_tmpdir(false).should == Dir.tmpdir
		end
		
		it "returns ENV['PHUSION_PASSENGER_TMP'] if it's set" do
			ENV['PHUSION_PASSENGER_TMP'] = '/foo'
			passenger_tmpdir(false).should == '/foo'
		end
		
		it "creates the directory if it doesn't exist, if the 'create' argument is true" do
			ENV['PHUSION_PASSENGER_TMP'] = 'utils_spec.tmp'
			passenger_tmpdir
			begin
				File.directory?('utils_spec.tmp').should be_true
			ensure
				Dir.rmdir('utils_spec.tmp') rescue nil
			end
		end
	end
end
