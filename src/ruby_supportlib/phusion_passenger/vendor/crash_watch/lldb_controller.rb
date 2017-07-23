# encoding: binary
#
# Copyright (c) 2016-2017 Phusion Holding B.V.
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

require 'rbconfig'
PhusionPassenger.require_passenger_lib 'vendor/crash_watch/base'
PhusionPassenger.require_passenger_lib 'vendor/crash_watch/utils'

module CrashWatch
  class LldbNotFound < Error
  end

  class LldbController
    class ExitInfo
      attr_reader :exit_code, :signal, :backtrace, :snapshot

      def initialize(exit_code, signal, backtrace, snapshot)
        @exit_code = exit_code
        @signal = signal
        @backtrace = backtrace
        @snapshot = snapshot
      end

      def signaled?
        !!@signal
      end
    end

    END_OF_RESPONSE_MARKER = '--------END_OF_RESPONSE--------'

    attr_accessor :debug

    def initialize
      @pid, @in, @out = Utils.popen_command(find_lldb, "-x")
      execute("settings set prompt 'LLDB:'")
      execute("settings set auto-confirm false")
    end

    def execute(command_string, timeout = nil)
      raise "LLDB session is already closed" if !@pid
      puts "lldb write #{command_string.inspect}" if @debug
      marker = "\n#{END_OF_RESPONSE_MARKER}\n"
      @in.puts(command_string)
      @in.puts("script print #{marker.inspect}")
      done = false
      result = []
      while !done
        begin
          if select([@out], nil, nil, timeout)
            line = @out.readline.chomp
            line.sub!(/^LLDB:/, '')
            puts "lldb read #{line.inspect}" if @debug
            if line == "#{END_OF_RESPONSE_MARKER}"
              done = true
            else
              result << line
            end
          else
            close!
            done = true
            result = nil
          end
        rescue EOFError
          done = true
        end
      end

      if result
        # Remove echo of the command string
        result.slice!(0, 2)
        # Remove echo of the marker print command
        result.pop
        result.pop

        result.join("\n") << "\n"
      else
        nil
      end
    end

    def closed?
      !@pid
    end

    def close
      if !closed?
        begin
          execute("process detach", 5) if !closed?
          execute("quit", 5) if !closed?
        rescue Errno::EPIPE
        end
        if !closed?
          @in.close
          @out.close
          Process.waitpid(@pid)
          @pid = nil
        end
      end
    end

    def close!
      if !closed?
        @in.close
        @out.close
        Process.kill('KILL', @pid)
        Process.waitpid(@pid)
        @pid = nil
      end
    end

    def attach(pid)
      pid = pid.to_s.strip
      raise ArgumentError if pid.empty?
      result = execute("attach -p #{pid}")
      result !~ /(unable to attach|cannot attach)/
    end

    def program_counter
      execute("p/x $pc").gsub(/.* = /, '')
    end

    def current_thread
      execute("thread info") =~ /^thread #(.+?): /
      $1
    end

    def current_thread_backtrace
      execute("bt").strip
    end

    def all_threads_backtraces
      execute("bt all").strip
    end

  private
    def find_lldb
      result = nil
      if ENV['LLDB'] && File.executable?(ENV['LLDB'])
        result = ENV['LLDB']
      else
        ENV['PATH'].to_s.split(/:+/).each do |path|
          filename = "#{path}/lldb"
          if File.file?(filename) && File.executable?(filename)
            result = filename
            break
          end
        end
      end

      puts "Found lldb at: #{result}" if result

      if result.nil?
        raise LldbNotFound
      else
        result
      end
    end
  end
end
