require 'tmpdir'
require 'fileutils'
require 'thread'
PhusionPassenger.require_passenger_lib 'utils'
PhusionPassenger.require_passenger_lib 'utils/json'
PhusionPassenger.require_passenger_lib 'message_channel'

module PhusionPassenger

class AppProcess
  attr_reader :pid, :input, :work_dir, :properties

  def initialize(pid, input, work_dir, properties)
    @pid = pid
    @input = input
    @work_dir = work_dir
    @properties = properties
  end

  def close
    if @input && !@input.closed?
      @input.close
    end

    if @pid
      # Give process some time to exit
      sleep 0.1

      begin
        Process.kill('TERM', @pid)
      rescue Errno::ESRCH
      end
      begin
        Process.waitpid(@pid)
      rescue Errno::ECHILD
      end
    end

    FileUtils.rm_rf(@work_dir)
  end

  def connect_and_send_request(headers)
    socket = Utils.connect_to_server(find_socket_accepting_http_requests['address'])
    channel = MessageChannel.new(socket)
    data = ""
    headers["REQUEST_METHOD"] ||= "GET"
    headers["REQUEST_URI"] ||= headers["PATH_INFO"]
    headers["QUERY_STRING"] ||= ""
    headers["SCRIPT_NAME"] ||= ""
    headers["CONTENT_LENGTH"] ||= "0"
    headers.each_pair do |key, value|
      data << "#{key}\0#{value}\0"
    end
    channel.write_scalar(data)
    socket
  end

  def sockets
    @sockets ||= @properties['sockets']
  end

  def find_socket_accepting_http_requests
    sockets.find do |socket|
      socket['accept_http_requests']
    end
  end

  def find_socket_with_protocol(protocol)
    sockets.find do |socket|
      socket['protocol'] == protocol
    end
  end
end

class SpawnError < StandardError
  attr_accessor :status, :summary

  def initialize(status, details)
    super(details[:summary])
    @status = status
    @summary = details[:summary]
  end
end

class Loader
  attr_reader :command, :app_root

  def initialize(command, app_root)
    @command = command
    @app_root = app_root
  end

  def spawn(options = {})
    work_dir = create_work_dir
    begin
      write_startup_arguments(work_dir, options)
      pid, input = create_process(@command, work_dir, options)
      status = wait_for_finish(pid, work_dir)
      if status == :success
        properties = read_properties(work_dir)
        AppProcess.new(pid, input, work_dir, properties)
      else
        SpawnError.new(status, read_errors(work_dir))
      end
    rescue Exception => e
      FileUtils.rm_rf(work_dir)
      raise e
    end
  end

private
  def create_work_dir
    work_dir = Dir.mktmpdir
    begin
      create_work_dir_contents(work_dir)
      work_dir
    rescue Exception => e
      FileUtils.rm_rf(work_dir)
      raise e
    end
  end

  def create_work_dir_contents(work_dir)
    Dir.mkdir("#{work_dir}/args")
    Dir.mkdir("#{work_dir}/envdump")
    Dir.mkdir("#{work_dir}/response")
    Dir.mkdir("#{work_dir}/response/error")
    Dir.mkdir("#{work_dir}/response/steps")
    Dir.mkdir("#{work_dir}/response/steps/subprocess_exec_wrapper")
    Dir.mkdir("#{work_dir}/response/steps/subprocess_wrapper_preparation")
    Dir.mkdir("#{work_dir}/response/steps/subprocess_app_load_or_exec")
    Dir.mkdir("#{work_dir}/response/steps/subprocess_listen")
    if !system("mkfifo", "#{work_dir}/response/finish")
      raise "'mkfifo #{work_dir}/response/finish' failed"
    end
  end

  def create_process(command, work_dir, options = {})
    a, b = IO.pipe
    pid = fork do
      STDIN.reopen(a)
      if options[:quiet]
        STDOUT.reopen('/dev/null', 'w')
        STDERR.reopen(STDOUT)
        STDOUT.sync = STDERR.sync = true
      end
      b.close
      Dir.chdir(@app_root)
      ENV['RAILS_ENV'] = ENV['RACK_ENV'] = ENV['PASSENGER_ENV'] = 'production'
      ENV['PASSENGER_SPAWN_WORK_DIR'] = work_dir
      exec(*command)
    end
    a.close
    [pid, b]
  end

  def write_startup_arguments(work_dir, options)
    real_options = {
      :passenger_root => PhusionPassenger.install_spec,
      :ruby_libdir => PhusionPassenger.ruby_libdir,
      :app_root => File.expand_path(@app_root),
      :app_group_name => @app_root
    }
    real_options[:log_level] = 7 if DEBUG
    real_options.merge!(options)

    File.open("#{work_dir}/args.json", 'w') do |f|
      f.write(PhusionPassenger::Utils::JSON.generate(real_options))
    end

    real_options.each_pair do |key, value|
      File.open("#{work_dir}/args/#{key}", 'w') do |f|
        f.write(value)
      end
    end
  end

  def wait_for_finish(pid, work_dir)
    mutex  = Mutex.new
    cond   = ConditionVariable.new
    result = nil

    finish_signal_thr = Thread.new do
      content = File.open("#{work_dir}/response/finish", 'r') do |f|
        f.read(1)
      end
      mutex.synchronize do
        if result.nil?
          result = { :finish_signal => content }
          cond.signal
        end
      end
    end

    process_exit_thr = Thread.new do
      while true
        if Utils.process_is_alive?(pid)
          begin
            wait_result = Process.waitpid(pid, Process::WNOHANG)
          rescue Errno::ECHILD
            # The process is not a direct child of ours.
            # We don't know whether it is a zombie,
            # but let's assume that it isn't and
            # let's assume that its actual parent
            # does a proper job of reaping zombies.
            wait_result = false
          end
          if wait_result
            break
          else
            sleep 0.1
          end
        else
          break
        end
      end
      mutex.synchronize do
        if result.nil?
          result = { :process_exited => true }
          cond.signal
        end
      end
    end

    begin
      mutex.synchronize do
        while result.nil?
          cond.wait(mutex)
        end
      end

      if result[:finish_signal]
        if result[:finish_signal] == '1'
          :success
        else
          :error
        end
      else
        :premature_exit
      end
    ensure
      finish_signal_thr.kill
      process_exit_thr.kill
      finish_signal_thr.join
      process_exit_thr.join
    end
  end

  def read_properties(work_dir)
    PhusionPassenger::Utils::JSON.parse(
      File.read("#{work_dir}/response/properties.json"))
  end

  def read_errors(work_dir)
    {
      :summary => File.read("#{work_dir}/response/error/summary")
    }
  end
end

class Preloader < Loader
  attr_reader :command, :app_root, :preloader_process

  def initialize(command, app_root)
    @command = command
    @app_root = app_root
  end

  def close
    @preloader_process.close if @preloader_process
  end

  def start(options = {})
    work_dir = create_work_dir
    begin
      write_startup_arguments(work_dir, options)
      pid, input = create_process(@command, work_dir, options)
      status = wait_for_finish(pid, work_dir)
      if status == :success
        properties = read_properties(work_dir)
        @preloader_process = AppProcess.new(pid, input, work_dir, properties)
      else
        raise SpawnError.new(status, read_errors(work_dir))
      end
    rescue Exception => e
      FileUtils.rm_rf(work_dir)
      raise e
    end
  end

  def spawn(options = {})
    socket = Utils.connect_to_server(
      @preloader_process.find_socket_with_protocol('preloader')['address'])
    work_dir = create_work_dir
    begin
      if !system("mkfifo", "#{work_dir}/stdin")
        raise "'mkfifo #{work_dir}/stdin' failed"
      end

      write_startup_arguments(work_dir, options)
      write_spawn_request(socket, work_dir)
      pid = read_spawn_response(socket)

      begin
        input = File.open("#{work_dir}/stdin", 'w')
        status = wait_for_finish(pid, work_dir)
        if status == :success
          AppProcess.new(pid, input, work_dir, read_properties(work_dir))
        else
          raise SpawnError.new(status, read_errors(work_dir))
        end
      rescue Exception => e
        begin
          Process.kill('TERM', pid)
        rescue Errno::ESRCH
        end
        raise e
      end
    rescue Exception => e
      FileUtils.rm_rf(work_dir)
      raise e
    end
  end

private
  def write_spawn_request(socket, work_dir)
    socket.puts(PhusionPassenger::Utils::JSON.generate(
      :command => 'spawn',
      :work_dir => work_dir
    ))
  end

  def read_spawn_response(socket)
    doc = PhusionPassenger::Utils::JSON.parse(socket.readline)
    if doc['result'] != 'ok'
      raise "Spawn failed: #{doc.inspect}"
    end
    doc['pid']
  end

  def open_std_channels_async(work_dir)
    state = {
      :mutex => Mutex.new,
      :cond  => ConditionVariable.new,
      :stdin => nil,
      :stdout_and_err => nil,
      :stdin_open_thread => nil,
      :stdout_and_err_open_thread => nil
    }

    state[:stdin_open_thread] = Thread.new do
      begin
        f = File.open("#{work_dir}/stdin", 'w')
      rescue Exception => e
        state[:mutex].synchronize do
          state[:stdin] = e
          state[:cond].signal
        end
      else
        state[:mutex].synchronize do
          state[:stdin] = f
          state[:cond].signal
        end
      end
    end

    state[:stdout_and_err_open_thread] = Thread.new do
      begin
        f = File.open("#{work_dir}/stdout_and_err", 'w')
      rescue Exception => e
        state[:mutex].synchronize do
          state[:stdout_and_err] = e
          state[:cond].signal
        end
      else
        state[:mutex].synchronize do
          state[:stdout_and_err] = f
          state[:cond].signal
        end
      end
    end

    state
  end
end

module LoaderSpecHelper
  def self.included(klass)
    klass.before(:each) do
      @stubs = []
    end

    klass.after(:each) do
      begin
        @process.close if @process && @process.is_a?(AppProcess)
        @preloader.close if @preloader
      ensure
        @stubs.each do |stub|
          stub.destroy
        end
      end
    end
  end

  def before_start(code)
    @before_start = code
  end

  def after_start(code)
    @after_start = code
  end

  def register_stub(stub)
    @stubs << stub
    File.prepend(stub.startup_file, "#{@before_start}\n")
    File.append(stub.startup_file, "\n#{@after_start}")
    return stub
  end

  def register_app(app)
    @apps << app
    return app
  end

  def start!(options = {})
    result = start(options)
    if !result.is_a?(AppProcess)
      raise "Loader failed to start; error:\n#{result.summary}"
    end
  end

  def perform_request(headers)
    socket = @process.connect_and_send_request(headers)
    headers = {}
    line = socket.readline
    headers["Status"] = line.split(" ")[1]
    while line != "\r\n"
      key, value = line.strip.split(/ *: */, 2)
      headers[key] = value
      line = socket.readline
    end
    body = socket.read
    socket.close
    return [headers, body]
  end
end

shared_examples_for 'a loader' do
  it 'works' do
    start
    expect(@process).to be_an_instance_of(AppProcess)
    headers, body = perform_request(
      "REQUEST_METHOD" => "GET",
      "PATH_INFO" => "/"
    )
    expect(headers['Status']).to eq('200')
    expect(body).to eq('front page')
  end
end

end # module PhusionPassenger
