# daemon_controller, library for robust daemon management
# Copyright (c) 2010-2017 Phusion Holding B.V.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

require 'tempfile'
require 'fcntl'
require 'socket'
require 'pathname'
require 'timeout'
if Process.respond_to?(:spawn)
  require 'rbconfig'
end

PhusionPassenger.require_passenger_lib 'vendor/daemon_controller/lock_file'

module PhusionPassenger

# Main daemon controller object. See the README for an introduction and tutorial.
class DaemonController
  ALLOWED_CONNECT_EXCEPTIONS = [Errno::ECONNREFUSED, Errno::ENETUNREACH,
    Errno::ETIMEDOUT, Errno::ECONNRESET, Errno::EINVAL,
    Errno::EADDRNOTAVAIL]

  SPAWNER_FILE = File.expand_path(File.join(File.dirname(__FILE__),
    "daemon_controller", "spawn.rb"))

  class Error < StandardError
  end
  class TimeoutError < Error
  end
  class AlreadyStarted < Error
  end
  class StartError < Error
  end
  class StartTimeout < TimeoutError
  end
  class StopError < Error
  end
  class StopTimeout < TimeoutError
  end
  class ConnectError < Error
  end
  class DaemonizationTimeout < TimeoutError
  end

  # Create a new DaemonController object.
  #
  # === Mandatory options
  #
  # [:identifier]
  #  A human-readable, unique name for this daemon, e.g. "Sphinx search server".
  #  This identifier will be used in some error messages. On some platforms, it will
  #  be used for concurrency control: on such platforms, no two DaemonController
  #  objects will operate on the same identifier on the same time.
  #
  # [:start_command]
  #  The command to start the daemon. This must be a a String, e.g.
  #  "mongrel_rails start -e production", or a Proc which returns a String.
  #
  #  If the value is a Proc, and the +before_start+ option is given too, then
  #  the +start_command+ Proc is guaranteed to be called after the +before_start+
  #  Proc is called.
  #
  # [:ping_command]
  #  The ping command is used to check whether the daemon can be connected to.
  #  It is also used to ensure that #start only returns when the daemon can be
  #  connected to.
  #
  #  The value may be a command string. This command must exit with an exit code of
  #  0 if the daemon can be successfully connected to, or exit with a non-0 exit
  #  code on failure.
  #
  #  The value may also be an Array which specifies the socket address of the daemon.
  #  It must be in one of the following forms:
  #  - [:tcp, host_name, port]
  #  - [:unix, filename]
  #
  #  The value may also be a Proc, which returns an expression that evaluates to
  #  true (indicating that the daemon can be connected to) or false (failure).
  #  If the Proc raises Errno::ECONNREFUSED, Errno::ENETUNREACH, Errno::ETIMEDOUT
  #  Errno::ECONNRESET, Errno::EINVAL or Errno::EADDRNOTAVAIL then that also
  #  means that the daemon cannot be connected to.
  #  <b>NOTE:</b> if the ping command returns an object which responds to
  #  <tt>#close</tt>, then that method will be called on it.
  #  This makes it possible to specify a ping command such as
  #  <tt>lambda { TCPSocket.new('localhost', 1234) }</tt>, without having to worry
  #  about closing it afterwards.
  #  Any exceptions raised by #close are ignored.
  #
  # [:pid_file]
  #  The PID file that the daemon will write to. Used to check whether the daemon
  #  is running.
  #
  # [:lock_file]
  #  The lock file to use for serializing concurrent daemon management operations.
  #  Defaults to "(filename of PID file).lock".
  #
  # [:log_file]
  #  The log file that the daemon will write to. It will be consulted to see
  #  whether the daemon has printed any error messages during startup.
  #
  # === Optional options
  # [:stop_command]
  #  A command to stop the daemon with, e.g. "/etc/rc.d/nginx stop". If no stop
  #  command is given (i.e. +nil+), then DaemonController will stop the daemon
  #  by killing the PID written in the PID file.
  #
  #  The default value is +nil+.
  #
  # [:restart_command]
  #  A command to restart the daemon with, e.g. "/etc/rc.d/nginx restart". If
  #  no restart command is given (i.e. +nil+), then DaemonController will
  #  restart the daemon by calling #stop and #start.
  #
  #  The default value is +nil+.
  #
  # [:before_start]
  #  This may be a Proc. It will be called just before running the start command.
  #  The before_start proc is not subject to the start timeout.
  #
  # [:start_timeout]
  #  The maximum amount of time, in seconds, that #start may take to start
  #  the daemon. Since #start also waits until the daemon can be connected to,
  #  that wait time is counted as well. If the daemon does not start in time,
  #  then #start will raise an exception.
  #
  #  The default value is 15.
  #
  # [:stop_timeout]
  #  The maximum amount of time, in seconds, that #stop may take to stop
  #  the daemon. Since #stop also waits until the daemon is no longer running,
  #  that wait time is counted as well. If the daemon does not stop in time,
  #  then #stop will raise an exception.
  #
  #  The default value is 15.
  #
  # [:log_file_activity_timeout]
  #  Once a daemon has gone into the background, it will become difficult to
  #  know for certain whether it is still initializing or whether it has
  #  failed and exited, until it has written its PID file. Suppose that it
  #  failed with an error after daemonizing but before it has written its PID file;
  #  not many system administrators want to wait 15 seconds (the default start
  #  timeout) to be notified of whether the daemon has terminated with an error.
  #
  #  An alternative way to check whether the daemon has terminated with an error,
  #  is by checking whether its log file has been recently updated. If, after the
  #  daemon has started, the log file hasn't been updated for the amount of seconds
  #  given by the :log_file_activity_timeout option, then the daemon is assumed to
  #  have terminated with an error.
  #
  #  The default value is 7.
  #
  # [:dont_stop_if_pid_file_invalid]
  #  If the :stop_command option is given, then normally daemon_controller will
  #  always execute this command upon calling #stop. But if :dont_stop_if_pid_file_invalid
  #  is given, then daemon_controller will not do that if the PID file does not contain
  #  a valid number.
  #
  #  The default is false.
  #
  # [:daemonize_for_me]
  #  Normally daemon_controller will wait until the daemon has daemonized into the
  #  background, in order to capture any errors that it may print on stdout or
  #  stderr before daemonizing. However, if the daemon doesn't support daemonization
  #  for some reason, then setting this option to true will cause daemon_controller
  #  to do the daemonization for the daemon.
  #
  #  The default is false.
  #
  # [:keep_ios]
  #  Upon spawning the daemon, daemon_controller will normally close all file
  #  descriptors except stdin, stdout and stderr. However if there are any file
  #  descriptors you want to keep open, specify the IO objects here. This must be
  #  an array of IO objects.
  #
  # [:env]
  #  This must be a Hash.  The hash will contain the environment variables available
  #  to be made available to the daemon. Hash keys must be strings, not symbols.
  def initialize(options)
    [:identifier, :start_command, :ping_command, :pid_file, :log_file].each do |option|
      if !options.has_key?(option)
        raise ArgumentError, "The ':#{option}' option is mandatory."
      end
    end
    @identifier = options[:identifier]
    @start_command = options[:start_command]
    @stop_command = options[:stop_command]
    @ping_command = options[:ping_command]
    @restart_command = options[:restart_command]
    @ping_interval = options[:ping_interval] || 0.1
    @pid_file = options[:pid_file]
    @log_file = options[:log_file]
    @before_start = options[:before_start]
    @start_timeout = options[:start_timeout] || 15
    @stop_timeout = options[:stop_timeout] || 15
    @log_file_activity_timeout = options[:log_file_activity_timeout] || 7
    @dont_stop_if_pid_file_invalid = options[:dont_stop_if_pid_file_invalid]
    @daemonize_for_me = options[:daemonize_for_me]
    @keep_ios = options[:keep_ios] || []
    @lock_file = determine_lock_file(options, @identifier, @pid_file)
    @env = options[:env] || {}
  end

  # Start the daemon and wait until it can be pinged.
  #
  # Raises:
  # - AlreadyStarted - the daemon is already running.
  # - StartError - the start command failed.
  # - StartTimeout - the daemon did not start in time. This could also
  #   mean that the daemon failed after it has gone into the background.
  def start
    @lock_file.exclusive_lock do
      start_without_locking
    end
  end

  # Connect to the daemon by running the given block, which contains the
  # connection logic. If the daemon isn't already running, then it will be
  # started.
  #
  # The block must return nil or raise Errno::ECONNREFUSED, Errno::ENETUNREACH,
  # Errno::ETIMEDOUT, Errno::ECONNRESET, Errno::EINVAL and Errno::EADDRNOTAVAIL
  # to indicate that the daemon cannot be
  # connected to. It must return non-nil if the daemon can be connected to.
  # Upon successful connection, the return value of the block will
  # be returned by #connect.
  #
  # Note that the block may be called multiple times.
  #
  # Raises:
  # - StartError - an attempt to start the daemon was made, but the start
  #   command failed with an error.
  # - StartTimeout - an attempt to start the daemon was made, but the daemon
  #   did not start in time, or it failed after it has gone into the background.
  # - ConnectError - the daemon wasn't already running, but we couldn't connect
  #   to the daemon even after starting it.
  def connect
    connection = nil
    @lock_file.shared_lock do
      begin
        connection = yield
      rescue *ALLOWED_CONNECT_EXCEPTIONS
        connection = nil
      end
    end
    if connection.nil?
      @lock_file.exclusive_lock do
        if !daemon_is_running?
          start_without_locking
        end
        connect_exception = nil
        begin
          connection = yield
        rescue *ALLOWED_CONNECT_EXCEPTIONS => e
          connection = nil
          connect_exception = e
        end
        if connection.nil?
          # Daemon is running but we couldn't connect to it. Possible
          # reasons:
          # - The daemon froze.
          # - Bizarre security restrictions.
          # - There's a bug in the yielded code.
          if connect_exception
            raise ConnectError, "Cannot connect to the daemon: #{connect_exception} (#{connect_exception.class})"
          else
            raise ConnectError, "Cannot connect to the daemon"
          end
        else
          connection
        end
      end
    else
      connection
    end
  end

  # Stop the daemon and wait until it has exited.
  #
  # Raises:
  # - StopError - the stop command failed.
  # - StopTimeout - the daemon didn't stop in time.
  def stop
    @lock_file.exclusive_lock do
      begin
        Timeout.timeout(@stop_timeout, Timeout::Error) do
          kill_daemon if daemon_is_running?
          wait_until do
            !daemon_is_running?
          end
        end
      rescue Timeout::Error
        raise StopTimeout, "Daemon '#{@identifier}' did not exit in time"
      end
    end
  end

  # Restarts the daemon. Uses the restart_command if provided, otherwise
  # calls #stop and #start.
  def restart
    if @restart_command
      run_command(@restart_command)
    else
      stop
      start
    end
  end

  # Returns the daemon's PID, as reported by its PID file. Returns the PID
  # as an integer, or nil there is no valid PID in the PID file.
  #
  # This method doesn't check whether the daemon's actually running.
  # Use #running? if you want to check whether it's actually running.
  #
  # Raises SystemCallError or IOError if something went wrong during
  # reading of the PID file.
  def pid
    @lock_file.shared_lock do
      read_pid_file
    end
  end

  # Checks whether the daemon is still running. This is done by reading
  # the PID file and then checking whether there is a process with that
  # PID.
  #
  # Raises SystemCallError or IOError if something went wrong during
  # reading of the PID file.
  def running?
    @lock_file.shared_lock do
      daemon_is_running?
    end
  end

  # Checks whether ping Unix domain sockets is supported. Currently
  # this is supported on all Ruby implementations, except JRuby.
  def self.can_ping_unix_sockets?
    RUBY_PLATFORM != "java"
  end

private
  def start_without_locking
    if daemon_is_running?
      raise AlreadyStarted, "Daemon '#{@identifier}' is already started"
    end
    save_log_file_information
    delete_pid_file
    begin
      started = false
      before_start
      Timeout.timeout(@start_timeout, Timeout::Error) do
        done = false
        spawn_daemon
        record_activity

        # We wait until the PID file is available and until
        # the daemon responds to pings, but we wait no longer
        # than @start_timeout seconds in total (including daemon
        # spawn time).
        # Furthermore, if the log file hasn't changed for
        # @log_file_activity_timeout seconds, and the PID file
        # still isn't available or the daemon still doesn't
        # respond to pings, then assume that the daemon has
        # terminated with an error.
        wait_until do
          if log_file_has_changed?
            record_activity
          elsif no_activity?(@log_file_activity_timeout)
            raise Timeout::Error, "Daemon seems to have exited"
          end
          pid_file_available?
        end
        wait_until(@ping_interval) do
          if log_file_has_changed?
            record_activity
          elsif no_activity?(@log_file_activity_timeout)
            raise Timeout::Error, "Daemon seems to have exited"
          end
          run_ping_command || !daemon_is_running?
        end
        started = run_ping_command
      end
      result = started
    rescue DaemonizationTimeout, Timeout::Error => e
      start_timed_out
      if pid_file_available?
        kill_daemon_with_signal(true)
      end
      if e.is_a?(DaemonizationTimeout)
        result = :daemonization_timeout
      else
        result = :start_timeout
      end
    end
    if !result
      raise(StartError, differences_in_log_file ||
        "Daemon '#{@identifier}' failed to start.")
    elsif result == :daemonization_timeout
      raise(StartTimeout, differences_in_log_file ||
        "Daemon '#{@identifier}' didn't daemonize in time.")
    elsif result == :start_timeout
      raise(StartTimeout, differences_in_log_file ||
        "Daemon '#{@identifier}' failed to start in time.")
    else
      true
    end
  end

  def before_start
    if @before_start
      @before_start.call
    end
  end

  def spawn_daemon
    if @start_command.respond_to?(:call)
      run_command(@start_command.call)
    else
      run_command(@start_command)
    end
  end

  def kill_daemon
    if @stop_command
      if @dont_stop_if_pid_file_invalid && read_pid_file.nil?
        return
      end
      begin
        run_command(@stop_command)
      rescue StartError => e
        raise StopError, e.message
      end
    else
      kill_daemon_with_signal
    end
  end

  def kill_daemon_with_signal(force = false)
    pid = read_pid_file
    if pid
      if force
        Process.kill('SIGKILL', pid)
      else
        Process.kill('SIGTERM', pid)
      end
    end
  rescue Errno::ESRCH, Errno::ENOENT
  end

  def daemon_is_running?
    begin
      pid = read_pid_file
    rescue Errno::ENOENT
      # The PID file may not exist, or another thread/process
      # executing #running? may have just deleted the PID file.
      # So we catch this error.
      pid = nil
    end
    if pid.nil?
      false
    elsif check_pid(pid)
      true
    else
      delete_pid_file
      false
    end
  end

  def read_pid_file
    begin
      pid = File.read(@pid_file).strip
    rescue Errno::ENOENT
      return nil
    end
    if pid =~ /\A\d+\Z/
      pid.to_i
    else
      nil
    end
  end

  def delete_pid_file
    File.unlink(@pid_file)
  rescue Errno::EPERM, Errno::EACCES, Errno::ENOENT # ignore
  end

  def check_pid(pid)
    Process.kill(0, pid)
    true
  rescue Errno::ESRCH
    false
  rescue Errno::EPERM
    # We didn't have permission to kill the process. Either the process
    # is owned by someone else, or the system has draconian security
    # settings and we aren't allowed to kill *any* process. Assume that
    # the process is running.
    true
  end

  def wait_until(sleep_interval = 0.1)
    while !yield
      sleep(sleep_interval)
    end
  end

  def wait_until_pid_file_is_available_or_log_file_has_changed
    while !(pid_file_available? || log_file_has_changed?)
      sleep 0.1
    end
    pid_file_is_available?
  end

  def wait_until_daemon_responds_to_ping_or_has_exited_or_log_file_has_changed
    while !(run_ping_command || !daemon_is_running? || log_file_has_changed?)
      sleep(@ping_interval)
    end
    run_ping_command
  end

  def record_activity
    @last_activity_time = Time.now
  end

  # Check whether there has been no recorded activity in the past +seconds+ seconds.
  def no_activity?(seconds)
    Time.now - @last_activity_time > seconds
  end

  def pid_file_available?
    File.exist?(@pid_file) && File.stat(@pid_file).size != 0
  end

  # This method does nothing and only serves as a hook for the unit test.
  def start_timed_out
  end

  # This method does nothing and only serves as a hook for the unit test.
  def daemonization_timed_out
  end

  def save_log_file_information
    @original_log_file_stat = File.stat(@log_file) rescue nil
    @current_log_file_stat = @original_log_file_stat
  end

  def log_file_has_changed?
    if @current_log_file_stat
      stat = File.stat(@log_file) rescue nil
      if stat
        result = @current_log_file_stat.mtime != stat.mtime ||
                 @current_log_file_stat.size  != stat.size
        @current_log_file_stat = stat
        result
      else
        true
      end
    else
      false
    end
  end

  def differences_in_log_file
    if @original_log_file_stat && @original_log_file_stat.file?
      File.open(@log_file, 'r') do |f|
        f.seek(@original_log_file_stat.size, IO::SEEK_SET)
        diff = f.read.strip
        if diff.empty?
          nil
        else
          diff
        end
      end
    else
      nil
    end
  rescue Errno::ENOENT, Errno::ESPIPE
    # ESPIPE means the log file is a pipe.
    nil
  end

  def determine_lock_file(options, identifier, pid_file)
    if options[:lock_file]
      LockFile.new(File.expand_path(options[:lock_file]))
    else
      LockFile.new(File.expand_path(pid_file + ".lock"))
    end
  end

  def self.fork_supported?
    RUBY_PLATFORM != "java" && RUBY_PLATFORM !~ /win32/
  end

  def self.spawn_supported?
    # Process.spawn doesn't work very well in JRuby.
    Process.respond_to?(:spawn) && RUBY_PLATFORM != "java"
  end

  def run_command(command)
    if should_capture_output_while_running_command?
      run_command_while_capturing_output(command)
    else
      run_command_without_capturing_output(command)
    end
  end

  def should_capture_output_while_running_command?
    if is_std_channel_chardev?(@log_file)
      false
    else
      begin
        real_log_file = Pathname.new(@log_file).realpath.to_s
      rescue SystemCallError
        real_log_file = nil
      end
      if real_log_file
        !is_std_channel_chardev?(real_log_file)
      else
        true
      end
    end
  end

  def is_std_channel_chardev?(path)
    path == "/dev/stdout" ||
      path == "/dev/stderr" ||
      path == "/dev/fd/1" ||
      path == "/dev/fd/2" ||
      path =~ %r(\A/proc/([0-9]+|self)/fd/[12]\Z)
  end

  def run_command_while_capturing_output(command)
    # Create tempfile for storing the command's output.
    tempfile = Tempfile.new('daemon-output')
    tempfile.chmod(0666)
    tempfile_path = tempfile.path
    tempfile.close

    if self.class.fork_supported? || self.class.spawn_supported?
      if Process.respond_to?(:spawn)
        options = {
          :in  => "/dev/null",
          :out => tempfile_path,
          :err => tempfile_path,
          :close_others => true
        }
        @keep_ios.each do |io|
          options[io] = io
        end
        if @daemonize_for_me
          pid = Process.spawn(@env, ruby_interpreter, SPAWNER_FILE,
            command, options)
        else
          pid = Process.spawn(@env, command, options)
        end
      else
        pid = safe_fork(@daemonize_for_me) do
          ObjectSpace.each_object(IO) do |obj|
            if !@keep_ios.include?(obj)
              obj.close rescue nil
            end
          end
          STDIN.reopen("/dev/null", "r")
          STDOUT.reopen(tempfile_path, "w")
          STDERR.reopen(tempfile_path, "w")
          ENV.update(@env)
          exec(command)
        end
      end

      # run_command might be running in a timeout block (like
      # in #start_without_locking).
      begin
        interruptable_waitpid(pid)
      rescue Errno::ECHILD
        # Maybe a background thread or whatever waitpid()'ed
        # this child process before we had the chance. There's
        # no way to obtain the exit status now. Assume that
        # it started successfully; if it didn't we'll know
        # that later by checking the PID file and by pinging
        # it.
        return
      rescue Timeout::Error
        daemonization_timed_out

        # If the daemon doesn't fork into the background
        # in time, then kill it.
        begin
          Process.kill('SIGTERM', pid)
        rescue SystemCallError
        end
        begin
          Timeout.timeout(5, Timeout::Error) do
            begin
              interruptable_waitpid(pid)
            rescue SystemCallError
            end
          end
        rescue Timeout::Error
          begin
            Process.kill('SIGKILL', pid)
            interruptable_waitpid(pid)
          rescue SystemCallError
          end
        end
        raise DaemonizationTimeout
      end
      if $?.exitstatus != 0
        raise StartError, File.read(tempfile_path).strip
      end
    else
      if @env && !@env.empty?
        raise "Setting the :env option is not supported on this Ruby implementation."
      elsif @daemonize_for_me
        raise "Setting the :daemonize_for_me option is not supported on this Ruby implementation."
      end

      cmd = "#{command} >\"#{tempfile_path}\""
      cmd << " 2>\"#{tempfile_path}\"" unless PLATFORM =~ /mswin/
      if !system(cmd)
        raise StartError, File.read(tempfile_path).strip
      end
    end
  ensure
    File.unlink(tempfile_path) rescue nil
  end

  def run_command_without_capturing_output(command)
    if self.class.fork_supported? || self.class.spawn_supported?
      if Process.respond_to?(:spawn)
        options = {
          :in  => "/dev/null",
          :out => :out,
          :err => :err,
          :close_others => true
        }
        @keep_ios.each do |io|
          options[io] = io
        end
        if @daemonize_for_me
          pid = Process.spawn(@env, ruby_interpreter, SPAWNER_FILE,
            command, options)
        else
          pid = Process.spawn(@env, command, options)
        end
      else
        pid = safe_fork(@daemonize_for_me) do
          ObjectSpace.each_object(IO) do |obj|
            if !@keep_ios.include?(obj)
              obj.close rescue nil
            end
          end
          STDIN.reopen("/dev/null", "r")
          ENV.update(@env)
          exec(command)
        end
      end

      # run_command might be running in a timeout block (like
      # in #start_without_locking).
      begin
        interruptable_waitpid(pid)
      rescue Errno::ECHILD
        # Maybe a background thread or whatever waitpid()'ed
        # this child process before we had the chance. There's
        # no way to obtain the exit status now. Assume that
        # it started successfully; if it didn't we'll know
        # that later by checking the PID file and by pinging
        # it.
        return
      rescue Timeout::Error
        daemonization_timed_out

        # If the daemon doesn't fork into the background
        # in time, then kill it.
        begin
          Process.kill('SIGTERM', pid)
        rescue SystemCallError
        end
        begin
          Timeout.timeout(5, Timeout::Error) do
            begin
              interruptable_waitpid(pid)
            rescue SystemCallError
            end
          end
        rescue Timeout::Error
          begin
            Process.kill('SIGKILL', pid)
            interruptable_waitpid(pid)
          rescue SystemCallError
          end
        end
        raise DaemonizationTimeout
      end
      if $?.exitstatus != 0
        raise StartError, "Daemon '#{@identifier}' failed to start."
      end
    else
      if @env && !@env.empty?
        raise "Setting the :env option is not supported on this Ruby implementation."
      elsif @daemonize_for_me
        raise "Setting the :daemonize_for_me option is not supported on this Ruby implementation."
      end

      if !system(command)
        raise StartError, "Daemon '#{@identifier}' failed to start."
      end
    end
  end

  def run_ping_command
    if @ping_command.respond_to?(:call)
      begin
        value = @ping_command.call
        if value.respond_to?(:close)
          value.close rescue nil
        end
        value
      rescue *ALLOWED_CONNECT_EXCEPTIONS
        false
      end
    elsif @ping_command.is_a?(Array)
      type, *args = @ping_command
      if self.class.can_ping_unix_sockets?
        case type
        when :tcp
          hostname, port = args
          sockaddr = Socket.pack_sockaddr_in(port, hostname)
          ping_tcp_socket(sockaddr)
        when :unix
          socket_domain = Socket::Constants::AF_LOCAL
          sockaddr = Socket.pack_sockaddr_un(args[0])
          ping_socket(socket_domain, sockaddr)
        else
          raise ArgumentError, "Unknown ping command type #{type.inspect}"
        end
      else
        case type
        when :tcp
          hostname, port = args
          ping_socket(hostname, port)
        when :unix
          raise "Pinging Unix domain sockets is not supported on this Ruby implementation"
        else
          raise ArgumentError, "Unknown ping command type #{type.inspect}"
        end
      end
    else
      system(@ping_command)
    end
  end

  if !can_ping_unix_sockets?
    require 'java'

    def ping_socket(host_name, port)
      channel = java.nio.channels.SocketChannel.open
      begin
        address = java.net.InetSocketAddress.new(host_name, port)
        channel.configure_blocking(false)
        if channel.connect(address)
          return true
        end

        deadline = Time.now.to_f + 0.1
        done = false
        while true
          begin
            if channel.finish_connect
              return true
            end
          rescue java.net.ConnectException => e
            if e.message =~ /Connection refused/i
              return false
            else
              throw e
            end
          end

          # Not done connecting and no error.
          sleep 0.01
          if Time.now.to_f >= deadline
            return false
          end
        end
      ensure
        channel.close
      end
    end
  else
    def ping_socket(socket_domain, sockaddr)
      begin
        socket = Socket.new(socket_domain, Socket::Constants::SOCK_STREAM, 0)
        begin
          socket.connect_nonblock(sockaddr)
        rescue Errno::ENOENT, Errno::EINPROGRESS, Errno::EAGAIN, Errno::EWOULDBLOCK
          if select(nil, [socket], nil, 0.1)
            begin
              socket.connect_nonblock(sockaddr)
            rescue Errno::EISCONN
            rescue Errno::EINVAL
              if RUBY_PLATFORM =~ /freebsd/i
                raise Errno::ECONNREFUSED
              else
                raise
              end
            end
          else
            raise Errno::ECONNREFUSED
          end
        end
        true
      rescue Errno::ECONNREFUSED, Errno::ENOENT
        false
      ensure
        socket.close if socket
      end
    end

    def ping_tcp_socket(sockaddr)
      begin
        ping_socket(Socket::Constants::AF_INET, sockaddr)
      rescue Errno::EAFNOSUPPORT
        ping_socket(Socket::Constants::AF_INET6, sockaddr)
      end
    end
  end

  def ruby_interpreter
    if defined?(RbConfig)
      rb_config = RbConfig::CONFIG
    else
      rb_config = Config::CONFIG
    end
    File.join(
      rb_config['bindir'],
      rb_config['RUBY_INSTALL_NAME']
    ) + rb_config['EXEEXT']
  end

  def safe_fork(double_fork)
    pid = fork
    if pid.nil?
      begin
        if double_fork
          pid2 = fork
          if pid2.nil?
            Process.setsid
            yield
          end
        else
          yield
        end
      rescue Exception => e
        message = "*** Exception #{e.class} " <<
          "(#{e}) (process #{$$}):\n" <<
          "\tfrom " << e.backtrace.join("\n\tfrom ")
        STDERR.write(e)
        STDERR.flush
        exit!
      ensure
        exit!(0)
      end
    else
      if double_fork
        Process.waitpid(pid) rescue nil
        pid
      else
        pid
      end
    end
  end

  if RUBY_VERSION < "1.9"
    def interruptable_waitpid(pid)
      Process.waitpid(pid)
    end
  else
    # On Ruby 1.9, Thread#kill (which is called by timeout.rb) may
    # not be able to interrupt Process.waitpid. So here we use a
    # special version that's a bit less efficient but is at least
    # interruptable.
    def interruptable_waitpid(pid)
      result = nil
      while !result
        result = Process.waitpid(pid, Process::WNOHANG)
        sleep 0.01 if !result
      end
      result
    end
  end
end

end # module PhusionPassenger
