require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'tmpdir'
require 'json'
require 'digest/sha1'
require 'socket'
require 'fileutils'
require 'net/http'
require 'support/apache2_controller'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'admin_tools'
PhusionPassenger.require_passenger_lib 'admin_tools/instance_registry'

WEB_SERVER_DECHUNKS_REQUESTS = false

require 'integration_tests/shared/example_webapp_tests'

# TODO: test the 'PassengerUserSwitching' and 'PassengerDefaultUser' option.
# TODO: test custom page caching directory

describe 'Apache 2 module' do
  PORT = ENV.fetch('TEST_PORT_BASE', '64506').to_i

  before :all do
    check_hosts_configuration

    @passenger_temp_dir = Dir.mktmpdir('psg-test-', '/tmp')
    FileUtils.chmod_R(0777, @passenger_temp_dir)
    ENV['TMPDIR'] = @passenger_temp_dir
    ENV['PASSENGER_INSTANCE_REGISTRY_DIR'] = @passenger_temp_dir

    if File.directory?(PhusionPassenger.install_spec)
      @log_dir = "#{PhusionPassenger.install_spec}/buildout/testlogs"
    else
      @log_dir = "#{@passenger_temp_dir}/testlogs"
    end
    @log_file = "#{@log_dir}/apache2.log"
    FileUtils.mkdir_p(@log_dir)
  end

  after :all do
    begin
      @apache2.stop if @apache2
      FileUtils.cp(Dir["#{@passenger_temp_dir}/passenger-error-*.html"],
        "#{@log_dir}/")
    ensure
      FileUtils.chmod_R(0777, @passenger_temp_dir)
      FileUtils.rm_rf(@passenger_temp_dir)
    end
  end

  before :each do |example|
    File.open(@log_file, 'a') do |f|
      # Make sure that all Apache log output is prepended by the test description
      # so that we know which messages are associated with which tests.
      f.puts "\n#### #{Time.now}: #{example.full_description}"
      @test_log_pos = f.pos
    end
  end

  after :each do |example|
    log 'End of test'
    if example.exception
      puts "\t---------------- Begin logs -------------------"
      File.open(@log_file, 'rb') do |f|
        f.seek(@test_log_pos)
        puts f.read.split("\n").map { |line| "\t#{line}" }.join("\n")
      end
      puts "\t---------------- End logs -------------------"
      puts "\tThe following test failed. The web server logs are printed above."
    end
  end

  def create_apache2_controller
    @apache2 = Apache2Controller.new(port: PORT)
    @apache2.set(passenger_temp_dir: @passenger_temp_dir, log_file: @log_file)
    if CONFIG.has_key?('codesigning_identity')
      @apache2.set(codesigning_identity: CONFIG['codesigning_identity'])
    end
    if CONFIG.has_key?('codesigning_keychain')
      @apache2.set(codesigning_keychain: CONFIG['codesigning_keychain'])
    end
    if Process.uid == 0
      @apache2.set(
        www_user: CONFIG['normal_user_1'],
        www_group: Etc.getgrgid(Etc.getpwnam(CONFIG['normal_user_1']).gid).name
      )
    end
  end

  def log(message)
    File.open(@log_file, 'a') do |f|
      f.puts "[#{Time.now}] Spec: #{message}"
    end
  end

  describe 'a Ruby app running on the root URI' do
    before :all do
      create_apache2_controller
      @server = "http://1.passenger.test:#{@apache2.port}"
      @stub = RackStub.new('rack')
      @apache2 << 'PassengerMaxPoolSize 1'
      @apache2.set_vhost('1.passenger.test', "#{@stub.full_app_root}/public")
      @apache2.start
    end

    after :all do
      @stub.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @stub.reset
    end

    include_examples 'an example web app'
  end

  describe 'a Ruby app running in a sub-URI' do
    before :all do
      create_apache2_controller
      @server = "http://1.passenger.test:#{@apache2.port}/subapp"
      @stub = RackStub.new('rack')
      @apache2 << 'PassengerMaxPoolSize 1'
      @apache2.set_vhost('1.passenger.test', File.expand_path('stub')) do |vhost|
        vhost << %Q(
          Alias /subapp #{@stub.full_app_root}/public
          <Location /subapp>
            PassengerBaseURI /subapp
            PassengerAppRoot #{@stub.full_app_root}
          </Location>
        )
      end
      @apache2.start
    end

    after :all do
      @stub.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @stub.reset
    end

    include_examples 'an example web app'

    it 'does not interfere with the root website' do
      @server = "http://1.passenger.test:#{@apache2.port}"
      get('/').should == 'This is the stub directory.'
    end
  end

  describe 'a Python app running on the root URI' do
    before :all do
      create_apache2_controller
      @server = "http://1.passenger.test:#{@apache2.port}"
      @stub = PythonStub.new('wsgi')
      @apache2 << 'PassengerMaxPoolSize 1'
      @apache2.set_vhost('1.passenger.test', "#{@stub.full_app_root}/public")
      @apache2.start
    end

    after :all do
      @stub.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @stub.reset
    end

    include_examples 'an example web app'
  end

  describe 'a Python app running in a sub-URI' do
    before :all do
      create_apache2_controller
      @server = "http://1.passenger.test:#{@apache2.port}/subapp"
      @stub = PythonStub.new('wsgi')
      @apache2 << 'PassengerMaxPoolSize 1'
      @apache2.set_vhost('1.passenger.test', File.expand_path('stub')) do |vhost|
        vhost << %Q(
          Alias /subapp #{@stub.full_app_root}/public
          <Location /subapp>
            PassengerBaseURI /subapp
            PassengerAppRoot #{@stub.full_app_root}
          </Location>
        )
      end
      @apache2.start
    end

    after :all do
      @stub.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @stub.reset
    end

    include_examples 'an example web app'

    it 'does not interfere with the root website' do
      @server = "http://1.passenger.test:#{@apache2.port}"
      get('/').should == 'This is the stub directory.'
    end
  end

  describe 'protocol upgrades' do
    # Connects to Apache and sends a request that asks for a protocol upgrade.
    # Returns the socket -- positioned right after the response header block --
    # together with the status line and the response headers.
    def start_upgrade(path, headers, host: '1.passenger.test', pipelined_payload: nil)
      socket = TCPSocket.new(host, @apache2.port)
      socket.sync = true

      request = +"GET #{path} HTTP/1.1\r\nHost: #{host}\r\n"
      headers.each_pair { |name, value| request << "#{name}: #{value}\r\n" }
      request << "\r\n"
      request << pipelined_payload if pipelined_payload
      socket.write(request)

      status_line, response_headers = read_response_head(socket)
      [ socket, status_line, response_headers ]
    end

    # Reads one byte at a time, which is the only way not to consume part of
    # the tunnelled stream that follows the header block.
    def read_response_head(socket, timeout = 10)
      deadline = Time.now + timeout
      head = +''

      until head.end_with?("\r\n\r\n")
        remaining = deadline - Time.now
        byte = remaining > 0 ? read_available(socket, 1, remaining) : ''
        if byte.empty?
          raise 'The connection closed or stalled before a complete response ' \
            "header block arrived. Got: #{head.inspect}"
        end
        head << byte
      end

      lines = head.split("\r\n")
      status_line = lines.shift
      response_headers = {}
      lines.each do |line|
        name, value = line.split(':', 2)
        response_headers[name.downcase.strip] = value.to_s.strip
      end
      [ status_line, response_headers ]
    end

    # Reads up to `bytes` bytes, giving up after `timeout` seconds rather than
    # blocking forever, so that a broken tunnel fails the example instead of
    # hanging the suite.
    def read_available(socket, bytes, timeout = 10)
      result = +''.b
      deadline = Time.now + timeout

      while result.bytesize < bytes
        remaining = deadline - Time.now
        break if remaining <= 0 || !socket.wait_readable(remaining)

        chunk = socket.read_nonblock(bytes - result.bytesize, exception: false)
        break if chunk == :wait_readable || chunk.nil?
        result << chunk
      end

      result
    end

    # Whether the peer closes the connection within `timeout` seconds.
    # Anything it sends before closing is discarded.
    def closed_within?(socket, timeout)
      deadline = Time.now + timeout

      loop do
        remaining = deadline - Time.now
        return false if remaining <= 0 || !socket.wait_readable(remaining)
        return true if socket.read_nonblock(4096, exception: false).nil?
      end
    end

    before :all do
      create_apache2_controller
      @stub = RackStub.new('rack')
      # Every tunnelled connection holds an application process for as long as
      # it is open, so one process per concurrently open tunnel is needed.
      @apache2 << 'PassengerMaxPoolSize 4'
      # The default is 128 MB, which is large enough that the core absorbs
      # anything these examples send rather than applying backpressure. Lower
      # it so that the saturation example actually exercises a full pipeline.
      @apache2 << 'PassengerResponseBufferHighWatermark 1048576'
      @apache2.set_vhost('1.passenger.test', "#{@stub.full_app_root}/public")
      @apache2.set_vhost('2.passenger.test', "#{@stub.full_app_root}/public") do |vhost|
        vhost << 'PassengerUpgradeIdleTimeout 2'
      end
      @apache2.set_vhost('3.passenger.test', "#{@stub.full_app_root}/public") do |vhost|
        vhost << 'PassengerUpgradeIdleTimeout 2'
        # Bounds the writes towards the client as well, so that a stalled
        # pipeline is torn down quickly enough to assert on.
        vhost << 'Timeout 5'
      end
      @apache2.start
    end

    after :all do
      @stub.destroy
      @apache2.stop if @apache2
    end

    it 'carries data in both directions once the application has switched protocols' do
      socket, status_line, headers = start_upgrade('/switch_protocol',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' })
      begin
        status_line.should == 'HTTP/1.1 101 Switching Protocols'
        headers['upgrade'].should == 'raw'
        headers['connection'].should == 'Upgrade'
        # What follows a 101 is an opaque byte stream, so the web server must
        # not describe it as an HTTP message body.
        headers.should_not have_key('content-length')
        headers.should_not have_key('transfer-encoding')

        # Writing after having read proves that both directions are open at the
        # same time, which is the part that a WebSocket depends on.
        socket.write("hello\n")
        read_available(socket, 12).should == "Echo: hello\n"
        socket.write("again\n")
        read_available(socket, 12).should == "Echo: again\n"
      ensure
        socket.close
      end
    end

    it 'recognises the Connection header that browsers actually send' do
      # Browsers send the upgrade token alongside keep-alive rather than on
      # its own, so the token list has to be parsed rather than compared.
      socket, status_line, = start_upgrade('/switch_protocol',
        { 'Upgrade' => 'raw', 'Connection' => 'keep-alive, Upgrade' })
      begin
        status_line.should == 'HTTP/1.1 101 Switching Protocols'
        socket.write("hello\n")
        read_available(socket, 12).should == "Echo: hello\n"
      ensure
        socket.close
      end
    end

    it 'carries data that the client pipelined behind the upgrade request' do
      socket, status_line, = start_upgrade('/switch_protocol',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' },
        pipelined_payload: "pipelined\n")
      begin
        status_line.should == 'HTTP/1.1 101 Switching Protocols'
        read_available(socket, 16).should == "Echo: pipelined\n"
      ensure
        socket.close
      end
    end

    it 'carries payloads larger than the tunnel buffer' do
      payload = 'x' * (512 * 1024)
      socket, = start_upgrade('/switch_protocol',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' })
      begin
        # The echo starts arriving while we are still writing, so write from
        # another thread to keep the socket buffers from deadlocking us.
        writer = Thread.new { socket.write("#{payload}\n") }
        begin
          read_available(socket, payload.bytesize + 7, 30).should == "Echo: #{payload}\n"
        ensure
          writer.join(5) || writer.kill
        end
      ensure
        socket.close
      end
    end

    it 'keeps both directions moving when both are saturated' do
      # A synchronous echo application reads and writes in lockstep, so it
      # stops reading as soon as its own write blocks, and every buffer
      # between the two ends fills up. The pump has to keep servicing both
      # directions throughout; favouring either one stalls the pipeline.
      payload = 'x' * (4 * 1024 * 1024)
      socket, status_line, = start_upgrade('/switch_protocol_stream_echo',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' })
      begin
        status_line.should == 'HTTP/1.1 101 Switching Protocols'

        writer = Thread.new { socket.write(payload) }
        begin
          echoed = read_available(socket, payload.bytesize, 60)
          echoed.bytesize.should == payload.bytesize
          echoed.should == payload
        ensure
          writer.join(10) || writer.kill
        end
      ensure
        socket.close
      end
    end

    it 'gives up on a client that stops reading rather than holding the worker' do
      socket, status_line, = start_upgrade('/switch_protocol_stream_echo',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' }, host: '3.passenger.test')
      begin
        status_line.should == 'HTTP/1.1 101 Switching Protocols'

        # Push data in and never read the echo, so that every buffer between
        # here and the application fills up and the pipeline stalls for good.
        # A tunnel with an unbounded write in it would hold this worker, and
        # the application process behind it, until Apache was restarted.
        chunk = 'x' * (256 * 1024)
        begin
          loop { socket.write_nonblock(chunk) }
        rescue IO::WaitWritable, Errno::EAGAIN, Errno::EPIPE, Errno::ECONNRESET
        end

        # Do not read during this: reading would drain the pipeline and let
        # it recover, which is the opposite of what is being tested.
        sleep 10
        closed_within?(socket, 30).should be true
      ensure
        socket.close
      end
    end

    it 'passes a client half close on and keeps forwarding the application output' do
      socket, = start_upgrade('/switch_protocol',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' })
      begin
        socket.write("bye\n")
        socket.close_write

        read_available(socket, 10).should == "Echo: bye\n"
        # The application's read loop ends at the forwarded end of stream and
        # closes, which must reach the client rather than leaving it hanging.
        closed_within?(socket, 10).should be true
      ensure
        socket.close
      end
    end

    it 'closes the connection when the application closes its end' do
      socket, status_line, = start_upgrade('/switch_protocol_and_close',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' })
      begin
        status_line.should == 'HTTP/1.1 101 Switching Protocols'
        read_available(socket, 8).should == "goodbye\n"
        closed_within?(socket, 10).should be true
      ensure
        socket.close
      end
    end

    it 'closes an upgraded connection that stays idle longer than PassengerUpgradeIdleTimeout' do
      socket, status_line, = start_upgrade('/switch_protocol',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' }, host: '2.passenger.test')
      begin
        status_line.should == 'HTTP/1.1 101 Switching Protocols'
        started = Time.now
        closed_within?(socket, 15).should be true

        elapsed = Time.now - started
        # The lower bound matters as much as the upper one: a pump that tore
        # every connection down at once would satisfy the upper bound alone.
        elapsed.should be >= 1.5
        elapsed.should be < 6
      ensure
        socket.close
      end
    end

    it 'does not close an upgraded connection that keeps sending' do
      socket, status_line, = start_upgrade('/switch_protocol',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' }, host: '2.passenger.test')
      begin
        status_line.should == 'HTTP/1.1 101 Switching Protocols'
        # Three times the configured timeout, so traffic really has to reset
        # the deadline rather than merely delay the first expiry.
        6.times do |i|
          sleep 1
          socket.write("ping #{i}\n")
          read_available(socket, 13).should == "Echo: ping #{i}\n"
        end
      ensure
        socket.close
      end
    end

    it 'preserves the WebSocket handshake headers' do
      key = [ Array.new(16) { rand(256) }.pack('C*') ].pack('m0')
      expected_accept = [ Digest::SHA1.digest(
        "#{key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11") ].pack('m0')

      socket, status_line, headers = start_upgrade('/websocket_handshake', {
        'Upgrade' => 'websocket',
        'Connection' => 'Upgrade',
        'Sec-WebSocket-Version' => '13',
        'Sec-WebSocket-Key' => key
      })
      begin
        status_line.should == 'HTTP/1.1 101 Switching Protocols'
        headers['upgrade'].should == 'websocket'
        headers['sec-websocket-accept'].should == expected_accept

        frame = "\x81\x03abc".b
        socket.write(frame)
        read_available(socket, frame.bytesize).should == frame
      ensure
        socket.close
      end
    end

    it 'serves an ordinary response when the application declines the upgrade' do
      socket, status_line, headers = start_upgrade('/',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' })
      begin
        status_line.should == 'HTTP/1.1 200 OK'
        read_available(socket, headers['content-length'].to_i).should == 'front page'

        # The declined upgrade reaches the client through a response header
        # block that the module had to read ahead and hand back. If it handed
        # back too much or too little, the next response on this connection
        # is what would show it.
        socket.write("GET / HTTP/1.1\r\nHost: 1.passenger.test\r\n\r\n")
        _, second_headers = read_response_head(socket)
        read_available(socket, second_headers['content-length'].to_i).should == 'front page'
      ensure
        socket.close
      end
    end

    it 'refuses a 101 that switches to a protocol the client did not ask for' do
      socket, status_line, = start_upgrade('/switch_protocol_with_headers?upgrade=something-else',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' })
      begin
        status_line.should == 'HTTP/1.1 502 Bad Gateway'
      ensure
        socket.close
      end
    end

    it 'refuses a 101 that does not say which protocol it switches to' do
      socket, status_line, = start_upgrade('/switch_protocol_with_headers?upgrade=',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' })
      begin
        status_line.should == 'HTTP/1.1 502 Bad Gateway'
      ensure
        socket.close
      end
    end

    it 'strips hop-by-hop headers from the 101' do
      socket, status_line, headers = start_upgrade(
        '/switch_protocol_with_headers?upgrade=raw&Keep-Alive=timeout%3D5&X-End-To-End=yes',
        { 'Upgrade' => 'raw', 'Connection' => 'Upgrade' })
      begin
        status_line.should == 'HTTP/1.1 101 Switching Protocols'
        # Keep-Alive describes the connection between the application and
        # Passenger, not the one to the client.
        headers.should_not have_key('keep-alive')
        headers['x-end-to-end'].should == 'yes'
        headers['connection'].should == 'Upgrade'
        headers['upgrade'].should == 'raw'
        read_available(socket, 3).should == "ok\n"
      ensure
        socket.close
      end
    end
  end

  describe 'a Node.js app running on the root URI' do
    before :all do
      create_apache2_controller
      @server = "http://1.passenger.test:#{@apache2.port}"
      @stub = NodejsStub.new('node')
      @apache2 << 'PassengerMaxPoolSize 1'
      @apache2.set_vhost('1.passenger.test', "#{@stub.full_app_root}/public")
      @apache2.start
    end

    after :all do
      @stub.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @stub.reset
    end

    include_examples 'an example web app'
  end

  describe 'a Node.js app running in a sub-URI' do
    before :all do
      create_apache2_controller
      @server = "http://1.passenger.test:#{@apache2.port}/subapp"
      @stub = NodejsStub.new('node')
      @apache2 << 'PassengerMaxPoolSize 1'
      @apache2.set_vhost('1.passenger.test', File.expand_path('stub')) do |vhost|
        vhost << %Q(
          Alias /subapp #{@stub.full_app_root}/public
          <Location /subapp>
            PassengerBaseURI /subapp
            PassengerAppRoot #{@stub.full_app_root}
          </Location>
        )
      end
      @apache2.start
    end

    after :all do
      @stub.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @stub.reset
    end

    include_examples 'an example web app'

    it 'does not interfere with the root website' do
      @server = "http://1.passenger.test:#{@apache2.port}"
      get('/').should == 'This is the stub directory.'
    end
  end

  describe 'a generic app running on the root URI' do
    before :all do
      create_apache2_controller
      @server = "http://1.passenger.test:#{@apache2.port}"
      @stub = NodejsStub.new('node')
      rename_entrypoint_file
      @apache2 << 'PassengerMaxPoolSize 1'
      @apache2.set_vhost('1.passenger.test', "#{@stub.full_app_root}/public") do |vhost|
        vhost << "PassengerAppStartCommand 'node boot.js'"
      end
      @apache2.start
    end

    after :all do
      @stub.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @stub.reset
      rename_entrypoint_file
    end

    def rename_entrypoint_file
      FileUtils.mv("#{@stub.app_root}/app.js", "#{@stub.app_root}/boot.js")
    end

    include_examples 'an example web app'
  end

  describe 'compatibility with other modules' do
    before :all do
      create_apache2_controller
      @apache2 << 'PassengerMaxPoolSize 1'
      @apache2 << 'PassengerStatThrottleRate 0'

      @stub = RackStub.new('rack')
      @server = "http://1.passenger.test:#{@apache2.port}"
      @apache2.set_vhost('1.passenger.test', "#{@stub.full_app_root}/public") do |vhost|
        vhost << 'RewriteEngine on'
        vhost << 'RewriteRule ^/rewritten_frontpage$ / [PT,QSA,L]'
        vhost << 'RewriteRule ^/rewritten_env$ /env [PT,QSA,L]'
      end
      @apache2.start
    end

    after :all do
      @stub.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @stub.reset
    end

    it 'supports environment variable passing through mod_env' do
      File.write("#{@stub.app_root}/public/.htaccess", 'SetEnv FOO "Foo Bar!"')
      File.touch("#{@stub.app_root}/tmp/restart.txt", 2)  # Activate ENV changes.
      get('/system_env').should =~ /^FOO = Foo Bar\!$/
    end

    it 'supports mod_rewrite in the virtual host block' do
      get('/rewritten_frontpage').should == 'front page'
      cgi_envs = get('/rewritten_env?foo=bar+baz')
      cgi_envs.should include("REQUEST_URI = /env?foo=bar+baz\n")
      cgi_envs.should include("PATH_INFO = /env\n")
    end

    it 'supports mod_rewrite in .htaccess' do
      File.write("#{@stub.app_root}/public/.htaccess", %Q(
        RewriteEngine on
        RewriteRule ^htaccess_frontpage$ / [PT,QSA,L]
        RewriteRule ^htaccess_env$ env [PT,QSA,L]
      ))
      get('/htaccess_frontpage').should == 'front page'
      cgi_envs = get('/htaccess_env?foo=bar+baz')
      cgi_envs.should include("REQUEST_URI = /env?foo=bar+baz\n")
      cgi_envs.should include("PATH_INFO = /env\n")
    end
  end

  describe 'configuration options' do
    before :all do
      create_apache2_controller
      @apache2 << 'PassengerMaxPoolSize 3'
      @apache2 << 'PassengerStatThrottleRate 0'

      @stub = RackStub.new('rack')
      @stub_url_root = "http://5.passenger.test:#{@apache2.port}"
      @apache2.set_vhost('5.passenger.test', "#{@stub.full_app_root}/public") do |vhost|
        vhost << 'PassengerBufferUpload off'
        vhost << 'PassengerFriendlyErrorPages on'
        vhost << 'AllowEncodedSlashes on'
      end

      @stub2 = RackStub.new('rack')
      @stub2_url_root = "http://6.passenger.test:#{@apache2.port}"
      @apache2.set_vhost('6.passenger.test', "#{@stub2.full_app_root}/public") do |vhost|
        vhost << 'PassengerAppEnv development'
        vhost << 'PassengerSpawnMethod conservative'
        vhost << "PassengerRestartDir #{@stub2.full_app_root}/public"
        vhost << 'AllowEncodedSlashes off'
      end

      @apache2.start
    end

    after :all do
      @stub.destroy
      @stub2.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @stub.reset
      @stub2.reset
    end

    specify 'PassengerAppEnv is per-virtual host' do
      @server = @stub_url_root
      get('/system_env').should =~ /PASSENGER_APP_ENV = production/

      @server = @stub2_url_root
      get('/system_env').should =~ /PASSENGER_APP_ENV = development/
    end

    it 'looks for restart.txt in the directory specified by PassengerRestartDir' do
      @server = @stub2_url_root
      startup_file = "#{@stub2.app_root}/config.ru"
      restart_file = "#{@stub2.app_root}/public/restart.txt"

      File.write(startup_file, %Q{
        require File.expand_path(File.dirname(__FILE__) + "/library")

        app = lambda do |env|
          case env['PATH_INFO']
          when '/'
            text_response("hello world")
          else
            [404, { "Content-Type" => "text/plain" }, ["Unknown URI"]]
          end
        end

        run app
      })

      now = Time.now
      File.touch(restart_file, now - 5)
      get('/').should == 'hello world'

      File.write(startup_file, %Q{
        require File.expand_path(File.dirname(__FILE__) + "/library")

        app = lambda do |env|
          case env['PATH_INFO']
          when '/'
            text_response("oh hai")
          else
            [404, { "Content-Type" => "text/plain" }, ["Unknown URI"]]
          end
        end

        run app
      })

      File.touch(restart_file, now - 10)
      get('/').should == 'oh hai'
    end

    describe 'PassengerShowVersionInHeader' do
      before :each do
        @apache2 << 'PassengerShowVersionInHeader ' + option
        @apache2.stop
        @apache2.start
        @server = @stub_url_root
      end

      context 'set to on' do
        let(:option) { 'on' }

        it 'adds version to header' do
          response = get_response('/')

          response['X-Powered-By'].should include('Phusion Passenger')
          response['X-Powered-By'].should include(PhusionPassenger::VERSION_STRING)
        end
      end

      context 'set to off' do
        let(:option) { 'off' }

        it 'filters version from header' do
          response = get_response('/')

          response['X-Powered-By'].should include('Phusion Passenger')
          response['X-Powered-By'].should_not include(PhusionPassenger::VERSION_STRING)
        end
      end
    end

    describe 'PassengerAppRoot' do
      before :each do
        @server = @stub_url_root
        File.write("#{@stub.full_app_root}/public/cached.html", 'Static cached.html')
        File.write("#{@stub.full_app_root}/public/dir.html", 'Static dir.html')
        Dir.mkdir("#{@stub.full_app_root}/public/dir")
      end

      it 'supports page caching on non-index URIs' do
        get('/cached').should == 'Static cached.html'
      end

      it 'supports page caching on directory index URIs' do
        get('/dir').should == 'Static dir.html'
      end

      it 'works' do
        result = get('/parameters?first=one&second=Green+Bananas')
        result.should =~ %r{First: one\n}
        result.should =~ %r{Second: Green Bananas\n}
      end
    end

    it 'supports encoded slashes in the URL if AllowEncodedSlashes is turned on' do
      @server = @stub_url_root
      get('/env/foo%2fbar').should =~ %r{PATH_INFO = /env/foo/bar\n}

      @server = @stub2_url_root
      get('/env/foo%2fbar').should =~ %r{404 Not Found}
    end

    describe "when handling POST requests with 'chunked' transfer encoding, if PassengerBufferUpload is off" do
      it "sets Transfer-Encoding to 'chunked' and removes Content-Length" do
        @uri = URI.parse(@stub_url_root)
        socket = TCPSocket.new(@uri.host, @uri.port)
        begin
          socket.write("POST #{@stub_url_root}/env HTTP/1.1\r\n")
          socket.write("Host: #{@uri.host}:#{@uri.port}\r\n")
          socket.write("Transfer-Encoding: chunked\r\n")
          socket.write("Content-Type: text/plain\r\n")
          socket.write("Connection: close\r\n")
          socket.write("\r\n")

          chunk = 'foo=bar!'
          socket.write("%X\r\n%s\r\n" % [ chunk.size, chunk ])
          socket.write("0\r\n\r\n")
          socket.flush

          response = socket.read
          response.should_not include('CONTENT_LENGTH = ')
          response.should include("HTTP_TRANSFER_ENCODING = chunked\n")
        ensure
          socket.close
        end
      end
    end

    ####################################
  end

  describe 'error handling' do
    before :all do
      create_apache2_controller
      @webdir = Dir.mktmpdir('webdir')
      @apache2.set_vhost('1.passenger.test', @webdir) do |vhost|
        vhost << 'PassengerBaseURI /app-that-crashes-during-startup/public'
      end

      @stub = RackStub.new('rack')
      @stub_url_root = "http://2.passenger.test:#{@apache2.port}"
      @apache2.set_vhost('2.passenger.test', "#{@stub.full_app_root}/public")

      @apache2 << 'PassengerFriendlyErrorPages on'
      @apache2.start
    end

    after :all do
      FileUtils.rm_rf(@webdir)
      @stub.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @server = "http://1.passenger.test:#{@apache2.port}"
      @error_page_signature = /window\.spec = /
      @stub.reset
    end

    it 'displays an error page if the application crashes during startup' do
      RackStub.use('rack', "#{@webdir}/app-that-crashes-during-startup") do |stub|
        File.prepend(stub.startup_file, "raise 'app crash'")
        result = get('/app-that-crashes-during-startup/public')
        result.should =~ @error_page_signature
        result.should =~ /app crash/
      end
    end

    it "doesn't display a Ruby spawn error page if PassengerFriendlyErrorPages is off" do
      RackStub.use('rack', "#{@webdir}/app-that-crashes-during-startup") do |stub|
        File.write("#{stub.app_root}/public/.htaccess", 'PassengerFriendlyErrorPages off')
        File.prepend(stub.startup_file, "raise 'app crash'")
        result = get('/app-that-crashes-during-startup/public')
        result.should_not =~ @error_page_signature
        result.should_not =~ /app crash/
      end
    end
  end

  describe 'core' do
    AdminTools = PhusionPassenger::AdminTools

    before :all do
      create_apache2_controller
      @stub = RackStub.new('rack')
      @stub_url_root = "http://1.passenger.test:#{@apache2.port}"
      @apache2 << 'PassengerStatThrottleRate 0'
      @apache2.set_vhost('1.passenger.test', "#{@stub.full_app_root}/public")
      @apache2.start
      @server = "http://1.passenger.test:#{@apache2.port}"
    end

    after :all do
      @stub.destroy
      @apache2.stop if @apache2
    end

    before :each do
      @stub.reset
    end

    def get_newest_instance
      # Because Apache reloads once during startup, we want to select
      # the newest Passenger instance.
      instances = AdminTools::InstanceRegistry.new.list
      instances.sort! do |a, b|
        x = a.properties['instance_dir']['created_at_monotonic_usec']
        y = b.properties['instance_dir']['created_at_monotonic_usec']
        x <=> y
      end
      instances.last
    end

    it 'is restarted if it crashes' do
      # Make sure that all Apache worker processes have connected to
      # the Passenger core.
      10.times do
        get('/').should == 'front page'
        sleep 0.1
      end

      # Now kill the Passenger core.
      Process.kill('SIGKILL', get_newest_instance.core_pid)
      sleep 0.02 # Give the signal a small amount of time to take effect.

      # Each worker process should detect that the old
      # Passenger core has died, and should reconnect.
      10.times do
        get('/').should == 'front page'
        sleep 0.1
      end
    end

    it 'exposes the application pool for passenger-status' do
      File.touch("#{@stub.app_root}/tmp/restart.txt", 1)  # Get rid of all previous app processes.
      get('/').should == 'front page'
      instance = get_newest_instance

      # Wait until the server has processed the session close event.
      sleep 1

      request = Net::HTTP::Get.new('/pool.json')
      request.basic_auth('ro_admin', instance.read_only_admin_password)
      response = instance.http_request('agents.s/core_api', request)
      if response.code.to_i / 100 == 2
        if RUBY_VERSION >= '2.3'
          groups = JSON.parse(response.body, symbolize_names: true).to_a.map { |(key, value)| { name: key.to_s, app_root: value.dig(:app_root, 0, :value) } }
        else
          groups = JSON.parse(response.body, symbolize_names: true).to_a.map { |(key, value)| { name: key.to_s, app_root: value[:app_root][0][:value] } }
        end
      else
        raise response.body
      end

      groups.should have(1).item
      groups.each do |group|
        group[:name].should == "#{@stub.full_app_root} (production)"
        # TODO re-enable
        # processes = group.dig(:processes).map{|p|p.dig(:process)}
        # processes.should have(1).item
        # processes[0][:processed].should == "1"
      end
    end
  end

  ##### Helper methods #####

  def start_web_server_if_necessary
    if !@apache2.running?
      @apache2.start
    end
  end
end
