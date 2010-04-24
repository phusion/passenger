require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')
require 'ruby/shared/abstract_server_spec'

module PhusionPassenger

shared_examples_for "a spawn server" do
	it "raises an AbstractServer::ServerError if the server was killed" do
		spawner   # Start the spawn server.
		Process.kill('SIGKILL', spawner.server_pid)
		spawning = lambda { spawn_some_application }
		spawning.should raise_error(AbstractServer::ServerError)
	end
	
	it "works correctly after a restart, if something went wrong" do
		filename = "#{Utils.passenger_tmpdir}/works.txt"
		before_start %Q{
			File.touch(#{filename.inspect})
		}
		spawner   # Start the spawn server.
		Process.kill('SIGKILL', spawner.server_pid)
		spawner.stop
		spawner.start
		spawn_some_application
		File.exist?(filename).should be_true
	end
end

end # module PhusionPassenger
