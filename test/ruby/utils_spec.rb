require 'support/config'

require 'tmpdir'
require 'fileutils'
require 'stringio'
require 'phusion_passenger/message_channel'
require 'phusion_passenger/utils'

include PhusionPassenger

describe Utils do
	include Utils
	
	specify "#close_all_io_objects_for_fds closes all IO objects that are associated with the given file descriptors" do
		filename = "#{Dir.tmpdir}/passenger_test.#{Process.pid}.txt"
		begin
			pid = safe_fork('utils_spec') do
				a, b = IO.pipe
				close_all_io_objects_for_fds([0, 1, 2])
				File.open(filename, "w") do |f|
					f.write("#{a.closed?}, #{b.closed?}")
				end
			end
			Process.waitpid(pid) rescue nil
			File.read(filename).should == "true, true"
		ensure
			File.unlink(filename) rescue nil
		end
	end
	
	describe "#report_app_init_status" do
		it "reports normal errors, which #unmarshal_and_raise_errors raises" do
			a, b = IO.pipe
			begin
				pid = safe_fork('utils_spec') do
					a.close
					report_app_init_status(MessageChannel.new(b)) do
						raise RuntimeError, "hello world"
					end
				end
				b.close
				lambda { unmarshal_and_raise_errors(MessageChannel.new(a)) }.should raise_error(/hello world/)
			ensure
				a.close rescue nil
				b.close rescue nil
			end
		end
		
		it "reports SystemExit errors, which #unmarshal_and_raise_errors raises" do
			a, b = IO.pipe
			begin
				pid = safe_fork('utils_spec') do
					a.close
					report_app_init_status(MessageChannel.new(b)) do
						exit
					end
				end
				b.close
				lambda { unmarshal_and_raise_errors(MessageChannel.new(a)) }.should raise_error(/exited during startup/)
			ensure
				a.close rescue nil
				b.close rescue nil
			end
		end
		
		it "returns whether the block succeeded" do
			channel = MessageChannel.new(StringIO.new)
			success = report_app_init_status(channel) do
				false
			end
			success.should be_true
			
			success = report_app_init_status(channel) do
				raise StandardError, "hi"
			end
			success.should be_false
		end
		
		it "reports all data written to stderr" do
			a, b = IO.pipe
			begin
				pid = safe_fork('utils_spec') do
					a.close
					report_app_init_status(MessageChannel.new(b)) do
						STDERR.puts "Something went wrong!"
						exit
					end
				end
				b.close
				
				begin
					unmarshal_and_raise_errors(MessageChannel.new(a))
					violated "No exception raised"
				rescue AppInitError => e
					e.stderr.should =~ /Something went wrong!/
				end
			ensure
				a.close rescue nil
				b.close rescue nil
			end
		end
		
		it "writes all buffered stderr data to the 'write_stderr_contents_to' argument if the block failed" do
			stderr_buffer = StringIO.new
			report_app_init_status(MessageChannel.new(StringIO.new), stderr_buffer) do
				STDERR.puts "Something went wrong!"
				raise StandardError, ":-("
			end
			
			stderr_buffer.string.should =~ /Something went wrong!/
		end
		
		it "writes all buffered stderr data to the 'write_stderr_contents_to' argument if the block succeeded" do
			stderr_buffer = StringIO.new
			report_app_init_status(MessageChannel.new(StringIO.new), stderr_buffer) do
				STDERR.puts "Something went wrong!"
			end
			
			stderr_buffer.string.should =~ /Something went wrong!/
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
				FileUtils.chmod_R(0777, 'utils_spec.tmp')
				FileUtils.rm_rf('utils_spec.tmp')
			end
		end
	end
	
	######################
end
