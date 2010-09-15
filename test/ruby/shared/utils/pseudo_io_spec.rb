require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')
require 'stringio'
require 'phusion_passenger/message_channel'

module PhusionPassenger

shared_examples_for "a pseudo stderr created by #report_app_init_status" do
	before :each do
		@sink = StringIO.new
		@temp_channel = MessageChannel.new(StringIO.new)
	end
	
	after :each do
		File.unlink("output.tmp") rescue nil
	end
	
	it "redirects everything written to the pseudo STDERR/$stderr to the sink" do
		report_app_init_status(@temp_channel, @sink) do
			STDERR.puts "Something went wrong!"
			$stderr.puts "Something went wrong again!"
			raise StandardError, ":-(" if @raise_error
		end
		@sink.string.should =~ /Something went wrong!/
		@sink.string.should =~ /Something went wrong again!/
	end
	
	it "redirects reopen operations on the pseudo stderr to the sink" do
		@sink.should_receive(:reopen).with("output.tmp", "w")
		report_app_init_status(@temp_channel, @sink) do
			STDERR.reopen("output.tmp", "w")
			raise StandardError, ":-(" if @raise_error
		end
	end
	
	specify "after the function has finished, every operation on the old pseudo stderr object will still be redirected to the sink" do
		pseudo_stderr = nil
		report_app_init_status(@temp_channel, @sink) do
			pseudo_stderr = STDERR
			raise StandardError, ":-(" if @raise_error
		end
		
		pseudo_stderr.puts "hello world"
		@sink.string.should =~ /hello world/
		
		@sink.should_receive(:reopen).with("output.tmp", "w")
		pseudo_stderr.reopen("output.tmp", "w")
	end
	
	specify "after the function has finished, every output operation on the old pseudo stderr object will not be buffered" do
		pseudo_stderr = nil
		report_app_init_status(@temp_channel, @sink) do
			pseudo_stderr = STDERR
			pseudo_stderr.instance_variable_get(:@buffer).should_not be_nil
			raise StandardError, ":-(" if @raise_error
		end
		pseudo_stderr.instance_variable_get(:@buffer).should be_nil
	end
end

end # module PhusionPassenger
