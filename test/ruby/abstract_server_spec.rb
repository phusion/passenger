require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'phusion_passenger/abstract_server'

module PhusionPassenger

describe AbstractServer do
	before :each do
		@server = AbstractServer.new
	end
	
	after :each do
		@server.stop if @server.started?
	end
	
	it "reseeds the pseudo-random number generator after forking off a process" do
		@server.send(:define_message_handler, :random_number, :handle_random_number)
		@server.stub!(:handle_random_number).and_return do |channel|
			channel.write(rand.to_s)
		end
		
		first_num = second_num = nil
		
		@server.start
		@server.connect do |channel|
			channel.write("random_number")
			first_num = channel.read
		end
		
		@server.stop
		@server.start
		@server.connect do |channel|
			channel.write("random_number")
			second_num = channel.read
		end
		
		first_num.should_not == second_num
	end
	
	specify "its socket is password protected" do
		@server.ignore_password_errors = true
		@server.send(:define_message_handler, :number, :handle_number)
		@server.stub!(:handle_number).and_return do |channel|
			channel.write(1)
		end
		
		@server.start
		@server.instance_variable_set(:"@password", "1234")
		
		begin
			@server.connect do |channel|
				channel.write("number")
				result = channel.read
				result.should be_nil
			end
		rescue SystemCallError, IOError
			# Success.
		end
	end
end

end # module PhusionPassenger
