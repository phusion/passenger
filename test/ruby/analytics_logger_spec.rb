require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'stringio'
require 'phusion_passenger/analytics_logger'

describe AnalyticsLogger do
	YESTERDAY = Time.utc(2010, 4, 11, 11, 56, 02)
	TODAY     = Time.utc(2010, 4, 11, 12, 56, 02)
	TOMORROW  = Time.utc(2010, 4, 11, 13, 56, 02)
	FOOBAR_MD5 = Digest::MD5.hexdigest("foobar")
	LOCALHOST_MD5 = Digest::MD5.hexdigest("localhost")
	
	before :each do
		@username = "logging"
		@password = "1234"
		@log_dir  = Utils.passenger_tmpdir
		@socket_filename = "#{Utils.passenger_tmpdir}/generation-0/logging.socket"
		@agent_pid = spawn_logging_agent
		eventually do
			File.exist?(@socket_filename)
		end
		@logger = AnalyticsLogger.new(@socket_filename, "logging", "1234", "localhost")
		@logger2 = AnalyticsLogger.new(@socket_filename, "logging", "1234", "localhost")
	end
	
	after :each do
		@logger.close
		@logger2.close
		if @agent_pid
			Process.kill('KILL', @agent_pid)
			Process.waitpid(@agent_pid)
		end
	end
	
	def spawn_logging_agent
		spawn_process("#{AGENTS_DIR}/PassengerLoggingAgent",
			"server_instance_dir", Utils.passenger_tmpdir,
			"generation_number",   "0",
			"analytics_log_dir",   @log_dir,
			"analytics_log_user",  CONFIG['normal_user_1'],
			"analytics_log_group", CONFIG['normal_group_1'],
			"analytics_log_permissions", "u=rwx,g=rwx,o=rwx",
			"logging_agent_password", [@password].pack("m"))
	end
	
	def mock_time(time)
		AnalyticsLogger.should_receive(:current_time).any_number_of_times.and_return(time)
	end
	
	specify "logging with #new_transaction works" do
		mock_time(TODAY)
		
		log = @logger.new_transaction("foobar")
		log.should_not be_null
		begin
			log.message("hello")
		ensure
			log.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/requests/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /hello/
		
		log = @logger.new_transaction("foobar", :processes)
		log.should_not be_null
		begin
			log.message("world")
		ensure
			log.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/processes/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /world/
	end
	
	specify "#new_transaction reestablishes the connection if disconnected" do
		mock_time(TODAY)
		
		@logger.new_transaction("foobar").close(true)
		shared_data = @logger.instance_variable_get(:"@shared_data")
		shared_data.synchronize do
			shared_data.client.close
		end
		
		log = @logger.new_transaction("foobar")
		begin
			log.message("hello")
		ensure
			log.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/requests/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /hello/
	end
	
	specify "logging with #continue_transaction works" do
		mock_time(TODAY)
		
		log = @logger.new_transaction("foobar", :processes)
		begin
			log.message("hello")
			log2 = @logger2.continue_transaction(log.txn_id, "foobar", :processes)
			log2.should_not be_null
			log2.txn_id.should == log.txn_id
			begin
				log2.message("world")
			ensure
				log2.close(true)
			end
		ensure
			log.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/processes/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /#{Regexp.escape log.txn_id} .* hello$/
		File.read(log_file).should =~ /#{Regexp.escape log.txn_id} .* world$/
	end
	
	specify "#continue_transaction reestablishes the connection if disconnected" do
		mock_time(TODAY)
		
		log = @logger.new_transaction("foobar")
		log.close(true)
		log2 = @logger2.continue_transaction(log.txn_id, "foobar")
		log2.close(true)
		
		shared_data = @logger2.instance_variable_get(:"@shared_data")
		shared_data.synchronize do
			shared_data.client.close
		end
		
		log2 = @logger2.continue_transaction(log.txn_id, "foobar")
		begin
			log2.message("hello")
		ensure
			log2.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/requests/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /hello/
	end
	
	specify "AnalyticsLogger only creates null Log objects if no server address is given" do
		logger = AnalyticsLogger.new(nil, nil, nil, nil)
		begin
			logger.new_transaction("foobar").should be_null
		ensure
			logger.close
		end
	end
	
	specify "once a Log object is closed, be becomes null" do
		log = @logger.new_transaction("foobar")
		log.close
		log.should be_null
	end
	
	specify "null Log objects don't do anything" do
		logger = AnalyticsLogger.new(nil, nil, nil, nil)
		begin
			log = logger.new_transaction("foobar")
			log.message("hello")
			log.close(true)
		ensure
			logger.close
		end
		
		File.exist?("#{@log_dir}/1").should be_false
	end
	
	specify "#clear_connection closes the connection" do
		@logger.new_transaction("foobar").close
		@logger.clear_connection
		shared_data = @logger.instance_variable_get(:"@shared_data")
		shared_data.synchronize do
			shared_data.client.should be_nil
		end
	end
	
	it "reestablishes the connection to the logging server if the logging server crashed and was restarted" do
		mock_time(TODAY)
		
		@logger.new_transaction("foobar").close
		Process.kill('KILL', @agent_pid)
		Process.waitpid(@agent_pid)
		@agent_pid = spawn_logging_agent
		
		log = @logger.new_transaction("foobar")
		begin
			log.message("hello")
		ensure
			log.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/requests/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /hello/
	end
end