require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'phusion_passenger/abstract_server'

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
