require 'support/config'

require 'phusion_passenger/abstract_server'

include PhusionPassenger

shared_examples_for "an AbstractServer" do
	it "doesn't crash if it's started and stopped multiple times" do
		3.times do
			# Give the server some time to install the
			# signal handlers. If we don't give it enough
			# time, it will raise an ugly exception when
			# we send it a signal.
			sleep 0.1
			@server.stop
			@server.start
		end
	end
	
	it "raises a ServerAlreadyStarted if the server is already started" do
		lambda { @server.start }.should raise_error(AbstractServer::ServerAlreadyStarted)
	end
end

describe AbstractServer do
	before :each do
		@server = AbstractServer.new
	end
	
	after :each do
		@server.stop if @server.started?
	end
	
	it "reseeds the pseudo-random number generator after forking off a process" do
		@server.send(:define_message_handler, :random_number, :handle_random_number)
		@server.stub!(:handle_random_number).and_return do
			@server.send(:client).write(rand.to_s)
		end
		
		@server.start
		@server.send(:server).write("random_number")
		first_num = @server.send(:server).read
		
		@server.stop
		@server.start
		@server.send(:server).write("random_number")
		second_num = @server.send(:server).read
		
		first_num.should_not == second_num
	end
end
