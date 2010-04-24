require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')

module PhusionPassenger

shared_examples_for "an AbstractServer" do
	it "doesn't crash if it's started and stopped multiple times" do
		3.times do
			# Give the server some time to install the
			# signal handlers. If we don't give it enough
			# time, it will raise an ugly exception when
			# we send it a signal.
			sleep 0.1
			server.stop
			server.start
		end
	end
	
	it "raises a ServerAlreadyStarted if the server is already started" do
		lambda { server.start }.should raise_error(AbstractServer::ServerAlreadyStarted)
	end
end

end # module PhusionPassenger
