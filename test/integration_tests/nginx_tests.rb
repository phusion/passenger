require File.expand_path(File.dirname(__FILE__) + "/spec_helper")
require 'support/nginx_controller'
require 'fileutils'
require 'tmpdir'

WEB_SERVER_DECHUNKS_REQUESTS = true

require 'integration_tests/shared/example_webapp_tests'

describe "Phusion Passenger for Nginx" do
  before :all do
    if !CONFIG['nginx']
      STDERR.puts "*** ERROR: You must set the 'nginx' config option in test/config.json."
      exit!(1)
    end

    check_hosts_configuration

    @nginx_root = Dir.mktmpdir('psg-test-', '/tmp')
    ENV['TMPDIR'] = @nginx_root
    ENV['PASSENGER_INSTANCE_REGISTRY_DIR'] = @nginx_root

    if File.directory?(PhusionPassenger.install_spec)
      @log_dir = "#{PhusionPassenger.install_spec}/buildout/testlogs"
    else
      @log_dir = "#{@nginx_root}/testlogs"
    end
    @log_file = "#{@log_dir}/nginx.log"
    FileUtils.mkdir_p(@log_dir)
  end

  after :all do
    begin
      begin
        @nginx.stop if @nginx
      ensure
        FileUtils.cp(Dir["#{@nginx_root}/passenger-error-*.html"],
          "#{@log_dir}/")
      end
    ensure
      FileUtils.rm_rf(@nginx_root)
    end
  end

  before :each do |example|
    File.open(@log_file, 'a') do |f|
      # Make sure that all Nginx log output is prepended by the test description
      # so that we know which messages are associated with which tests.
      f.puts "\n#### #{Time.now}: #{example.full_description}"
      @test_log_pos = f.pos
    end
  end

  after :each do |example|
    log "End of test"
    if example.exception
      puts "\t---------------- Begin logs -------------------"
      File.open(@log_file, 'rb') do |f|
        f.seek(@test_log_pos)
        puts f.read.split("\n").map{ |line| "\t#{line}" }.join("\n")
      end
      puts "\t---------------- End logs -------------------"
      puts "\tThe following test failed. The web server logs are printed above."
    end
  end

  def create_nginx_controller(options = {})
    @nginx = NginxController.new(@nginx_root, @log_file)
    if Process.uid == 0
      @nginx.set(
        :www_user => CONFIG['normal_user_1'],
        :www_group => Etc.getgrgid(Etc.getpwnam(CONFIG['normal_user_1']).gid).name
      )
    end
    if CONFIG['nginx_passenger_dynamic_module_conf_file']
      @nginx.set(:passenger_dynamic_module_conf_file =>
        CONFIG['nginx_passenger_dynamic_module_conf_file'])
    end
    @nginx.set(options)
  end

  def log(message)
    File.open(@log_file, 'a') do |f|
      f.puts "[#{Time.now}] Spec: #{message}"
    end
  end

  describe "a Ruby app running on the root URI" do
    before :all do
      create_nginx_controller
      @server = "http://1.passenger.test:#{@nginx.port}"
      @stub = RackStub.new('rack')
      @nginx.add_server do |server|
        server[:server_name] = "1.passenger.test"
        server[:root]        = "#{@stub.full_app_root}/public"
      end
      @nginx.start
    end

    after :all do
      @stub.destroy
      @nginx.stop if @nginx
    end

    before :each do
      @stub.reset
    end

    include_examples "an example web app"
  end

  describe "a Ruby app running in a sub-URI" do
    before :all do
      create_nginx_controller
      @server = "http://1.passenger.test:#{@nginx.port}/subapp"
      @stub = RackStub.new('rack')
      @nginx.add_server do |server|
        server[:server_name] = "1.passenger.test"
        server[:root]        = File.expand_path("stub")
        server << %Q{
          location ~ ^/subapp(/.*|$) {
            alias #{@stub.full_app_root}/public$1;
            passenger_base_uri /subapp;
            passenger_document_root #{@stub.full_app_root}/public;
            passenger_app_root #{@stub.full_app_root};
            passenger_enabled on;
          }
        }
      end
      @nginx.start
    end

    after :all do
      @stub.destroy
      @nginx.stop if @nginx
    end

    before :each do
      @stub.reset
    end

    include_examples "an example web app"

    it "does not interfere with the root website" do
      @server = "http://1.passenger.test:#{@nginx.port}"
      get('/').should == "This is the stub directory."
    end
  end

  describe "a Python app running on the root URI" do
    before :all do
      create_nginx_controller
      @server = "http://1.passenger.test:#{@nginx.port}"
      @stub = PythonStub.new('wsgi')
      @nginx.add_server do |server|
        server[:server_name] = "1.passenger.test"
        server[:root]        = "#{@stub.full_app_root}/public"
      end
      @nginx.start
    end

    after :all do
      @stub.destroy
      @nginx.stop if @nginx
    end

    before :each do
      @stub.reset
    end

    include_examples "an example web app"
  end

  describe "a Python app running in a sub-URI" do
    before :all do
      create_nginx_controller
      @server = "http://1.passenger.test:#{@nginx.port}/subapp"
      @stub = PythonStub.new('wsgi')
      @nginx.add_server do |server|
        server[:server_name] = "1.passenger.test"
        server[:root]        = File.expand_path("stub")
        server << %Q{
          location ~ ^/subapp(/.*|$) {
            alias #{@stub.full_app_root}/public$1;
            passenger_base_uri /subapp;
            passenger_app_root #{@stub.full_app_root};
            passenger_document_root #{@stub.full_app_root}/public;
            passenger_enabled on;
          }
        }
      end
      @nginx.start
    end

    after :all do
      @stub.destroy
      @nginx.stop if @nginx
    end

    before :each do
      @stub.reset
    end

    include_examples "an example web app"

    it "does not interfere with the root website" do
      @server = "http://1.passenger.test:#{@nginx.port}"
      get('/').should == "This is the stub directory."
    end
  end

  describe "a Node.js app running on the root URI" do
    before :all do
      create_nginx_controller
      @server = "http://1.passenger.test:#{@nginx.port}"
      @stub = NodejsStub.new('node')
      @nginx.add_server do |server|
        server[:server_name] = "1.passenger.test"
        server[:root]        = "#{@stub.full_app_root}/public"
      end
      @nginx.start
    end

    after :all do
      @stub.destroy
      @nginx.stop if @nginx
    end

    before :each do
      @stub.reset
    end

    include_examples "an example web app"
  end

  describe "a Node.js app running in a sub-URI" do
    before :all do
      create_nginx_controller
      @server = "http://1.passenger.test:#{@nginx.port}/subapp"
      @stub = NodejsStub.new('node')
      @nginx.add_server do |server|
        server[:server_name] = "1.passenger.test"
        server[:root]        = File.expand_path("stub")
        server[:passenger_friendly_error_pages] = 'on'
        server << %Q{
          location ~ ^/subapp(/.*|$) {
            alias #{@stub.full_app_root}/public$1;
            passenger_base_uri /subapp;
            passenger_document_root #{@stub.full_app_root}/public;
            passenger_app_root #{@stub.full_app_root};
            passenger_enabled on;
          }
        }
      end
      @nginx.start
    end

    after :all do
      @stub.destroy
      @nginx.stop if @nginx
    end

    before :each do
      @stub.reset
    end

    include_examples "an example web app"

    it "does not interfere with the root website" do
      @server = "http://1.passenger.test:#{@nginx.port}"
      get('/').should == "This is the stub directory."
    end
  end

  describe "a generic app running on the root URI" do
    before :all do
      create_nginx_controller
      @server = "http://1.passenger.test:#{@nginx.port}"
      @stub = NodejsStub.new('node')
      rename_entrypoint_file
      @nginx.add_server do |server|
        server[:server_name] = "1.passenger.test"
        server[:root]        = "#{@stub.full_app_root}/public"
        server[:passenger_app_start_command] = "'node boot.js'"
      end
      @nginx.start
    end

    after :all do
      @stub.destroy
      @nginx.stop if @nginx
    end

    before :each do
      @stub.reset
      rename_entrypoint_file
    end

    def rename_entrypoint_file
      FileUtils.mv("#{@stub.app_root}/app.js", "#{@stub.app_root}/boot.js")
    end

    include_examples "an example web app"
  end

  describe "various features" do
    before :all do
      create_nginx_controller
      @server = "http://1.passenger.test:#{@nginx.port}"
      @stub = RackStub.new('rack')
      @nginx.set(:stat_throttle_rate => 0)
      @nginx.add_server do |server|
        server[:server_name] = "1.passenger.test"
        server[:root]        = "#{@stub.full_app_root}/public"
        server[:passenger_load_shell_envvars] = "off"
        server[:passenger_friendly_error_pages] = "on"
        server << %q{
          location /crash_without_friendly_error_page {
            passenger_enabled on;
            passenger_friendly_error_pages off;
          }
        }
      end
      @nginx.add_server do |server|
        server[:server_name] = "2.passenger.test"
        server[:root]        = "#{@stub.full_app_root}/public"
        server[:passenger_app_group_name] = "secondary"
        server[:passenger_load_shell_envvars] = "off"
        server[:passenger_read_timeout] = '3000ms'
      end
      @nginx.add_server do |server|
        server[:server_name] = "3.passenger.test"
        server[:passenger_app_group_name] = "tertiary"
        server[:root]        = "#{@stub.full_app_root}/public"
        server[:passenger_load_shell_envvars] = "off"
        server[:passenger_max_requests] = 3
      end
      if @nginx.version >= '1.15.3'
        @nginx.add_server do |server|
          server[:server_name] = "4.passenger.test"
          server[:passenger_app_group_name] = "quaternary"
          server[:root]        = "#{@stub.full_app_root}/public"
          server[:passenger_request_buffering] = "off"
        end
      end
      @nginx.start
    end

    after :all do
      @stub.destroy
      @nginx.stop if @nginx
    end

    before :each do
      @stub.reset
      @error_page_signature = /window\.spec =/
      File.touch("#{@stub.app_root}/tmp/restart.txt", 1 + rand(100000))
    end

    it "sets ENV['SERVER_SOFTWARE']" do
      File.write("#{@stub.app_root}/config.ru", %q{
        server_software = ENV['SERVER_SOFTWARE']
        app = lambda do |env|
          [200, { "Content-Type" => "text/plain" }, [server_software]]
        end
        run app
      })
      get('/').should =~ /nginx/i
    end

    it "tries index.html when path ends in /" do
      Dir.mkdir("#{@stub.app_root}/public/test")
      File.write("#{@stub.app_root}/public/test/index.html", "indexsuccess")
      data = get('/test/')
      data.should == "indexsuccess"
    end

    it "displays a friendly error page if the application fails to spawn" do
      File.write("#{@stub.app_root}/config.ru", %q{
        raise "my error"
      })
      data = get('/')
      data.should =~ /#{@error_page_signature}/
      data.should =~ /my error/
    end

    it "doesn't display a friendly error page if the application fails to spawn but passenger_friendly_error_pages is off" do
      File.write("#{@stub.app_root}/config.ru", %q{
        raise "my error"
      })
      data = get('/crash_without_friendly_error_page')
      data.should_not =~ /#{@error_page_signature}/
      data.should_not =~ /my error/
    end

    it "appends an X-Powered-By header containing the Phusion Passenger version number" do
      response = get_response('/')
      response["X-Powered-By"].should include("Phusion Passenger")
      response["X-Powered-By"].should include(PhusionPassenger::VERSION_STRING)
    end

    it "respawns the app after handling max_requests requests" do
      @server = "http://3.passenger.test:#{@nginx.port}"
      pid = get("/pid")
      get("/pid").should == pid
      get("/pid").should == pid
      get("/pid").should_not == pid
    end

    it "respects read_timeout setting" do
      @server = "http://2.passenger.test:#{@nginx.port}"

      # Start process
      get("/pid")

      response = get_response('/?sleep_seconds=1')
      response.class.should == Net::HTTPOK
      response = get_response('/?sleep_seconds=6')
      response.class.should == Net::HTTPGatewayTimeOut
    end

    it "supports disabling request buffering" do
      if @nginx.version >= '1.15.3'
        @server = "http://4.passenger.test:#{@nginx.port}"

        # Start process
        get("/pid")

        @uri = URI.parse(@server)
        socket = TCPSocket.new(@uri.host, @uri.port)
        begin
          socket.write("POST /raw_upload_to_file HTTP/1.1\r\n")
          socket.write("Host: #{@uri.host}:#{@uri.port}\r\n")
          socket.write("Transfer-Encoding: chunked\r\n")
          socket.write("Content-Type: text/plain\r\n")
          socket.write("Connection: close\r\n")
          socket.write("X-Output: output.txt\r\n")
          socket.write("\r\n")

          output_file = @stub.full_app_root + "/output.txt"

          eventually do
            File.exist?(output_file)
          end

          socket.write("5\r\n12345\r\n")
          eventually do
            File.read(output_file) == "5\r\n12345\r\n"
          end

          socket.write("5\r\n67890\r\n")
          eventually do
            File.read(output_file) == "5\r\n12345\r\n5\r\n67890\r\n"
          end

          socket.write("0\r\n\r\n")
          eventually do
            File.read(output_file) == "5\r\n12345\r\n5\r\n67890\r\n0\r\n\r\n"
          end
        ensure
          socket.close
        end
      end
    end
  end

  describe "oob work" do
    before :all do
      create_nginx_controller
      @server = "http://passenger.test:#{@nginx.port}"
      @stub = RackStub.new('rack')
      @nginx.set(:max_pool_size => 2)
      @nginx.add_server do |server|
        server[:server_name] = "passenger.test"
        server[:root]        = "#{@stub.full_app_root}/public"
      end
    end

    after :all do
      @stub.destroy
      @nginx.stop if @nginx
    end

    before :each do
      @stub.reset

      File.write("#{@stub.app_root}/config.ru", <<-RUBY)
        PhusionPassenger.on_event(:oob_work) do
          f = File.open("#{@stub.full_app_root}/oob_work.\#{$$}", 'w')
          f.close
          sleep 3
        end
        app = lambda do |env|
          if env['PATH_INFO'] == '/oobw'
            [200, { "Content-Type" => "text/html", "!~Request-OOB-Work" => 'true' }, [$$]]
          else
            [200, { "Content-Type" => "text/html" }, [$$]]
          end
        end
        run app
      RUBY

      @nginx.start
    end

    it "invokes oobw when requested by the app process" do
      pid = get("/oobw")
      eventually do
        File.exist?("#{@stub.app_root}/oob_work.#{pid}")
      end
    end

    it "does not block client while invoking oob work" do
      get("/") # ensure there are spawned app processes
      t0 = Time.now
      get("/oobw")
      secs = Time.now - t0
      secs.should <= 0.5
    end
  end

  ##### Helper methods #####

  def start_web_server_if_necessary
    if !@nginx.running?
      @nginx.start
    end
  end
end
