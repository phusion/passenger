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
end
