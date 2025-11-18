# frozen_string_literal: true

# daemon_controller, library for robust daemon management
# Copyright (c) 2010-2025 Asynchronous B.V.
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

require "tempfile"
require "fcntl"
require "socket"
require "pathname"
require "timeout"
require "rbconfig"

require_relative "daemon_controller/lock_file"

module PhusionPassenger
# Main daemon controller object. See the README for an introduction and tutorial.
class DaemonController
  ALLOWED_CONNECT_EXCEPTIONS = [Errno::ECONNREFUSED, Errno::ENETUNREACH,
    Errno::ETIMEDOUT, Errno::ECONNRESET, Errno::EINVAL,
    Errno::EADDRNOTAVAIL]

  SPAWNER_FILE = File.absolute_path(File.join(File.dirname(__FILE__),
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

  InternalCommandOkResult = Struct.new(:pid, :output)
  InternalCommandErrorResult = Struct.new(:pid, :output, :exit_status)
  InternalCommandTimeoutResult = Struct.new(:pid, :output)

  # Create a new DaemonController object.
  #
  # See doc/OPTIONS.md for options docs.
  def initialize(identifier:, start_command:, ping_command:, pid_file:, log_file:,
    lock_file: nil, stop_command: nil, restart_command: nil, before_start: nil,
    start_timeout: 30, start_abort_timeout: 10, stop_timeout: 30,
    log_file_activity_timeout: 10, ping_interval: 0.1, stop_graceful_signal: "TERM", dont_stop_if_pid_file_invalid: false,
    daemonize_for_me: false, keep_ios: nil, env: {}, logger: nil)
    @identifier = identifier
    @start_command = start_command
    @ping_command = ping_command
    @pid_file = pid_file
    @log_file = log_file

    @lock_file = determine_lock_file(lock_file, identifier, pid_file)
    @stop_command = stop_command
    @restart_command = restart_command
    @before_start = before_start
    @start_timeout = start_timeout
    @start_abort_timeout = start_abort_timeout
    @stop_timeout = stop_timeout
    @log_file_activity_timeout = log_file_activity_timeout
    @ping_interval = ping_interval
    @stop_graceful_signal = stop_graceful_signal
    @dont_stop_if_pid_file_invalid = dont_stop_if_pid_file_invalid
    @daemonize_for_me = daemonize_for_me
    @keep_ios = keep_ios
    @env = env
    @logger = logger
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
      connection = yield
    rescue *ALLOWED_CONNECT_EXCEPTIONS
      connection = nil
    end
    if connection.nil?
      @lock_file.exclusive_lock do
        unless daemon_is_running?
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
      timeoutable(@stop_timeout) do
        allow_timeout do
          kill_daemon
          wait_until { !daemon_is_running? }
        end
      end
    end
  rescue Timeout::Error
    kill_daemon_with_signal(force: true)
    wait_until { !daemon_is_running? }
    raise StopTimeout, "Daemon '#{@identifier}' did not exit in time (force killed)"
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
    raise AlreadyStarted, "Daemon '#{@identifier}' is already started" if daemon_is_running?

    save_log_file_information
    delete_pid_file
    spawn_result = nil

    begin
      _, remaining_time = timeoutable(@start_timeout) do
        allow_timeout { before_start }
        spawn_result = allow_timeout { spawn_daemon }
        daemon_spawned
        record_activity

        if spawn_result.is_a?(InternalCommandOkResult)
          allow_timeout do
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
                raise Timeout::Error, "Log file inactivity"
              end
              pid_file_available?
            end
            wait_until(sleep_interval: @ping_interval) do
              if log_file_has_changed?
                record_activity
              elsif no_activity?(@log_file_activity_timeout)
                raise Timeout::Error, "Log file inactivity"
              end
              run_ping_command || !daemon_is_running?
            end
          end
        end

        spawn_result
      end
    rescue Timeout::Error
      # If we got here then it means either the #before_start timed out (= no PID),
      # or the code after #spawn_daemon timed out (already daemonized, so use PID file).
      # #spawn_daemon itself won't trigger Timeout:Error because that's handled as
      # InternalCommandTimeoutResult.
      pid = spawn_result ? read_pid_file : nil
      start_timed_out(pid)
      debug "Timeout waiting for daemon to be ready, PID #{pid.inspect}"
      abort_start(pid: pid, is_direct_child: false) if pid
      raise StartTimeout, concat_spawn_output_and_logs(spawn_result ? spawn_result.output : nil,
        differences_in_log_file, nil, "timed out")
    end

    case spawn_result
    when InternalCommandOkResult
      success, _ = timeoutable(remaining_time) { allow_timeout { run_ping_command } }
      if success
        true
      else
        raise StartError, concat_spawn_output_and_logs(spawn_result.output, differences_in_log_file)
      end

    when InternalCommandErrorResult
      raise StartError, concat_spawn_output_and_logs(spawn_result.output,
        differences_in_log_file, spawn_result.exit_status)

    when InternalCommandTimeoutResult
      daemonization_timed_out(spawn_result.pid)
      abort_start(pid: spawn_result.pid, is_direct_child: true)
      debug "Timeout waiting for daemon to fork, PID #{spawn_result.pid}"
      raise StartTimeout, concat_spawn_output_and_logs(spawn_result.output,
        differences_in_log_file, nil, "timed out")

    else
      raise "Bug: unexpected result from #spawn_daemon: #{spawn_result.inspect}"
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
      return if @dont_stop_if_pid_file_invalid && read_pid_file.nil?

      result = run_command(@stop_command)
      case result
      when InternalCommandOkResult
        # Success
      when InternalCommandErrorResult
        raise StopError, concat_spawn_output_and_logs(result.output, nil, result.exit_status)
      when InternalCommandTimeoutResult
        raise StopError, concat_spawn_output_and_logs(result.output, nil, nil, "timed out")
      else
        raise "Bug: unexpected result from #run_command: #{result.inspect}"
      end
    else
      kill_daemon_with_signal
    end
  end

  def kill_daemon_with_signal(force: false)
    if (pid = read_pid_file)
      if force
        Process.kill("SIGKILL", pid)
      else
        Process.kill(normalize_signal_name(@stop_graceful_signal), pid)
      end
    end
  rescue Errno::ESRCH, Errno::ENOENT
  end

  def daemon_is_running?
    pid = read_pid_file
    if pid.nil?
      nil
    elsif check_pid(pid)
      true
    else
      delete_pid_file
      false
    end
  end

  def read_pid_file
    pid = File.read(@pid_file).strip
    if /\A\d+\Z/.match?(pid)
      pid.to_i
    end
  rescue Errno::ENOENT
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

  def wait_until(sleep_interval: 0.1)
    until yield
      sleep(sleep_interval)
    end
  end

  def wait_until_pid_file_is_available_or_log_file_has_changed
    until pid_file_available? || log_file_has_changed?
      sleep 0.1
    end
    pid_file_is_available?
  end

  def wait_until_daemon_responds_to_ping_or_has_exited_or_log_file_has_changed
    until run_ping_command || !daemon_is_running? || log_file_has_changed?
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
    File.exist?(@pid_file) && !File.zero?(@pid_file)
  end

  # This method does nothing and only serves as a hook for the unit test.
  def daemon_spawned
  end

  # This method does nothing and only serves as a hook for the unit test.
  def start_timed_out(pid)
  end

  # This method does nothing and only serves as a hook for the unit test.
  def daemonization_timed_out(pid)
  end

  # Aborts a daemon that we tried to start, but timed out.
  def abort_start(pid:, is_direct_child:)
    begin
      debug "Killing process #{pid}"
      Process.kill("SIGTERM", pid)
    rescue SystemCallError
    end

    begin
      timeoutable(@start_abort_timeout) do
        allow_timeout do
          wait_for_aborted_process(pid: pid, is_direct_child: is_direct_child)
        end
      end
    rescue Timeout::Error
      begin
        Process.kill("SIGKILL", pid)
      rescue SystemCallError
      end

      allow_timeout do
        wait_for_aborted_process(pid: pid, is_direct_child: is_direct_child)
      end
    end
  end

  def wait_for_aborted_process(pid:, is_direct_child:)
    if is_direct_child
      begin
        debug "Waiting directly for process #{pid}"
        Process.waitpid(pid)
      rescue SystemCallError
      end

      # The daemon may have:
      # 1. Written a PID file before forking. We delete this PID file.
      #    -OR-
      # 2. It might have forked (and written a PID file) right before
      #    we terminated it. We'll want the fork to stay alive rather
      #    than going through the (complicated) trouble of killing it.
      #    Don't touch the PID file.
      pid2 = read_pid_file
      debug "PID file contains #{pid2.inspect}"
      delete_pid_file if pid == pid2
    else
      debug "Waiting until daemon is no longer running"
      wait_until { !daemon_is_running? }
    end
  end

  def save_log_file_information
    @original_log_file_stat = begin
      File.stat(@log_file)
    rescue
      nil
    end
    @current_log_file_stat = @original_log_file_stat
  end

  def log_file_has_changed?
    if @current_log_file_stat
      stat = begin
        File.stat(@log_file)
      rescue
        nil
      end
      if stat
        result = @current_log_file_stat.mtime != stat.mtime ||
          @current_log_file_stat.size != stat.size
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
      File.open(@log_file, "r") do |f|
        f.seek(@original_log_file_stat.size, IO::SEEK_SET)
        f.read.strip
      end
    end
  rescue Errno::ENOENT, Errno::ESPIPE
    # ESPIPE means the log file is a pipe.
    nil
  end

  def determine_lock_file(given_lock_file, identifier, pid_file)
    if given_lock_file
      LockFile.new(File.absolute_path(given_lock_file))
    else
      LockFile.new(File.absolute_path(pid_file + ".lock"))
    end
  end

  def run_command(command)
    if should_capture_output_while_running_command?
      # Create tempfile for storing the command's output.
      tempfile = Tempfile.new("daemon-output")
      tempfile.chmod(0o666)
      tempfile_path = tempfile.path
      tempfile.close

      spawn_options = {
        in: "/dev/null",
        out: tempfile_path,
        err: tempfile_path,
        close_others: true
      }
    else
      spawn_options = {
        in: "/dev/null",
        out: :out,
        err: :err,
        close_others: true
      }
    end

    if @keep_ios
      @keep_ios.each do |io|
        spawn_options[io] = io
      end
    end

    pid = if @daemonize_for_me
      Process.spawn(@env, ruby_interpreter, SPAWNER_FILE,
        command, spawn_options)
    else
      Process.spawn(@env, command, spawn_options)
    end

    # run_command might be running in a timeout block (like
    # in #start_without_locking).
    begin
      Process.waitpid(pid)
    rescue Errno::ECHILD
      # Maybe a background thread or whatever waitpid()'ed
      # this child process before we had the chance. There's
      # no way to obtain the exit status now. Assume that
      # it started successfully; if it didn't we'll know
      # that later by checking the PID file and by pinging
      # it.
      return InternalCommandOkResult.new(pid, tempfile_path && File.read(tempfile_path).strip)
    rescue Timeout::Error
      return InternalCommandTimeoutResult.new(pid, tempfile_path && File.read(tempfile_path).strip)
    end

    child_status = $?
    output = File.read(tempfile_path).strip if tempfile_path
    if child_status.success?
      InternalCommandOkResult.new(pid, output)
    else
      InternalCommandErrorResult.new(pid, output, child_status)
    end
  ensure
    begin
      tempfile.unlink if tempfile
    rescue SystemCallError
      nil
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
      path =~ %r{\A/proc/([0-9]+|self)/fd/[12]\Z}
  end

  def run_ping_command
    if @ping_command.respond_to?(:call)
      begin
        value = @ping_command.call
        if value.respond_to?(:close)
          begin
            value.close
          rescue
            nil
          end
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

  if can_ping_unix_sockets?
    def ping_socket(socket_domain, sockaddr)
      socket = Socket.new(socket_domain, Socket::Constants::SOCK_STREAM, 0)
      begin
        socket.connect_nonblock(sockaddr)
      rescue Errno::ENOENT, Errno::EINPROGRESS, Errno::EAGAIN, Errno::EWOULDBLOCK
        if select(nil, [socket], nil, 0.1)
          begin
            socket.connect_nonblock(sockaddr)
          rescue Errno::EISCONN
          rescue Errno::EINVAL
            if RUBY_PLATFORM.match?(/freebsd/i)
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

    def ping_tcp_socket(sockaddr)
      ping_socket(Socket::Constants::AF_INET, sockaddr)
    rescue Errno::EAFNOSUPPORT
      ping_socket(Socket::Constants::AF_INET6, sockaddr)
    end
  else
    require "java"

    def ping_socket(host_name, port)
      channel = java.nio.channels.SocketChannel.open
      begin
        address = java.net.InetSocketAddress.new(host_name, port)
        channel.configure_blocking(false)
        return true if channel.connect(address)

        deadline = Time.now.to_f + 0.1
        loop do
          begin
            return true if channel.finish_connect
          rescue java.net.ConnectException => e
            if /Connection refused/i.match?(e.message)
              return false
            else
              throw e
            end
          end

          # Not done connecting and no error.
          sleep 0.01
          return false if Time.now.to_f >= deadline
        end
      ensure
        channel.close
      end
    end
  end

  def ruby_interpreter
    File.join(
      RbConfig::CONFIG["bindir"],
      RbConfig::CONFIG.values_at("RUBY_INSTALL_NAME", "EXEEXT").join
    )
  end

  def timeoutable(amount, &block)
    Thread.handle_interrupt(Timeout::Error => :never) do
      start_time = monotonic_time
      result = Timeout.timeout(amount, Timeout::Error, &block)
      [result, [monotonic_time - start_time, 0].max]
    end
  end

  def allow_timeout(&block)
    Thread.handle_interrupt(Timeout::Error => :on_blocking, &block)
  end

  def monotonic_time
    Process.clock_gettime(Process::CLOCK_MONOTONIC)
  end

  def signal_termination_message(process_status)
    if process_status.signaled?
      "terminated with signal #{signal_name_for(process_status.termsig)}"
    else
      "exited with status #{process_status.exitstatus}"
    end
  end

  def normalize_signal_name(name)
    name.start_with?("SIG") ? name : "SIG#{name}"
  end

  def signal_name_for(num)
    if (name = Signal.list.key(num))
      "SIG#{name}"
    else
      num.to_s
    end
  end

  def concat_spawn_output_and_logs(output, logs, exit_status = nil, suffix_message = nil)
    format_full_suffix_message = lambda do |main_message = nil|
      [
        main_message,
        exit_status ? signal_termination_message(exit_status) : nil,
        suffix_message
      ].compact.join("; ")
    end

    if output.nil? && logs.nil?
      "(#{format_full_suffix_message.call("logs not available")})"
    elsif (output && output.empty? && logs && logs.empty?) || (output && output.empty? && logs.nil?) || (output.nil? && logs && logs.empty?)
      "(#{format_full_suffix_message.call("logs empty")})"
    else
      full_suffix_message = format_full_suffix_message.call
      if full_suffix_message.empty?
        "#{output}\n#{logs}".strip
      elsif logs && logs.empty?
        "#{output}\n(#{full_suffix_message})".strip
      else
        "#{output}\n#{logs}\n(#{full_suffix_message})".strip
      end
    end
  end

  def debug(message)
    @logger.debug(message) if @logger
  end
end
end # module PhusionPassenger
