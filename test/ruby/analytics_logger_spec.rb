require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'stringio'
require 'phusion_passenger/analytics_logger'

describe AnalyticsLogger do
	TODAY = 1263385422000000  # January 13, 2009, 12:23:42 UTC
	FOOBAR_MD5 = Digest::MD5.hexdigest("foobar")
	LOCALHOST_MD5 = Digest::MD5.hexdigest("localhost")
	
	before :each do
		@logger = AnalyticsLogger.new("/socket", "logging", "1234", "localhost")
		@io = StringIO.new
		@time = Time.utc(2010, 4, 11, 12, 56, 02)
	end
	
	after :each do
		@logger.close
		@io.close if !@io.closed?
	end
	
	def mock_connection
		connection = mock(:name => "MessageClient")
		@logger.should_receive(:connect).and_return do
			@logger.instance_variable_get(:"@shared_data").client = connection
		end
		return connection
	end
	
	if false
	specify "#new_transaction returns a Log object, suitable for logging" do
		AnalyticsLogger.should_receive(:current_time).any_number_of_times.and_return(@time)
		connection = mock_connection
		connection.should_receive(:write).
			with("openTransaction",
				an_instance_of(String), "foobar", :requests,
				an_instance_of(String))
		log = @logger.new_transaction("foobar")
		connection.should_receive(:write).
			with("log", log.txn_id, @time.to_i * 1_000_000 + @time.usec)
		connection.should_receive(:write_scalar).
			with("hello world")
		log.message("hello world")
	end
	
	specify "#continue_transaction opens the log file and returns a Log object, suitable for logging" do
		@logger.should_receive(:open_log_file).with("foobar", 5678, :requests).and_return(@io)
		log = @logger.continue_transaction("foobar", "abcdef-5678")
		log.group_name.should == "foobar"
		log.txn_id.should == "abcdef-5678"
		log.message("hello world")
		@io.string.should =~ /hello world/
	end
	
	specify "#continue_transaction opens the log file through the logging agent and caches this file handle" do
		mock_message_client(5678)
		log = @logger.continue_transaction("foobar", "abcdef-5678")
		log.message("hello world")
		@io.string.should =~ /hello world/
		
		log = @logger.continue_transaction("foobar", "abcdef-5678")
		log.message("hi")
		@io.string.should =~ /hi/
	end
	
	it "calculates the log file path in the same way the C++ implementation does" do
		mock_message_client(TODAY)
		@logger.continue_transaction("foobar", "abcdef-#{TODAY}")
		@logger.instance_variable_get(:'@file_handle_cache').keys.should ==
			["1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/requests/2010/01/13/12/log.txt"]
	end
	
	it "writes short messages in the same way the C++ implementation does" do
		mock_message_client(5678)
		AnalyticsLogger::Log.should_receive(:timestamp).and_return(10)
		log = @logger.continue_transaction("foobar", "abcdef-5678")
		AnalyticsLogger::Log.should_receive(:timestamp).and_return(20)
		log.message("hello world")
		AnalyticsLogger::Log.should_receive(:timestamp).and_return(30)
		log.close
		@io.string.should ==
			"abcdef-5678 10 ATTACH\n" +
			"abcdef-5678 20 hello world\n" +
			"abcdef-5678 30 DETACH\n"
	end
	
	it "writes long messages in the expected format and locks the file while doing so" do
		mock_message_client(5678)
		@io.should_receive(:flock).with(File::LOCK_EX).exactly(3).times
		@io.should_receive(:flock).with(File::LOCK_UN).exactly(3).times
		
		AnalyticsLogger::Log.should_receive(:timestamp).and_return(10)
		log = @logger.continue_transaction("foobar", "abcdef-5678", :requests, true)
		AnalyticsLogger::Log.should_receive(:timestamp).and_return(20)
		log.message("hello world")
		AnalyticsLogger::Log.should_receive(:timestamp).and_return(30)
		log.close
		
		@io.string.should ==
			"  16 abcdef-5678 10 ATTACH\n" +
			"  1b abcdef-5678 20 hello world\n" +
			"  16 abcdef-5678 30 DETACH\n"
	end
	end
end