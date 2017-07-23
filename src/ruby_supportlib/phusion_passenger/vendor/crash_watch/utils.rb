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

module CrashWatch
  module Utils
    extend Utils

    def self.included(klass)
      # When included into another class, make sure that Utils
      # methods are made private.
      public_instance_methods(false).each do |method_name|
        klass.send(:private, method_name)
      end
    end

    def gdb_installed?
      command_installed?('gdb')
    end

    def lldb_installed?
      command_installed?('lldb')
    end

    def command_installed?(command)
      path = ENV['PATH'].to_s
      path.split(File::PATH_SEPARATOR).each do |dir|
        next if dir.empty?
        filename = "#{dir}/#{command}"
        if File.file?(filename) && File.executable?(filename)
          return true
        end
      end
      false
    end

    def popen_command(*command)
      a, b = IO.pipe
      c, d = IO.pipe
      if Process.respond_to?(:spawn)
        args = command.dup
        args << {
          STDIN  => a,
          STDOUT => d,
          STDERR => d,
          :close_others => true
        }
        pid = Process.spawn(*args)
      else
        pid = fork do
          STDIN.reopen(a)
          STDOUT.reopen(d)
          STDERR.reopen(d)
          b.close
          c.close
          exec(*command)
        end
      end
      a.close
      d.close
      b.binmode
      c.binmode
      [pid, b, c]
    end

    def find_signal_name(signo)
      Signal.list.each_pair do |name, number|
        if number == signo
          return name
        end
      end
      nil
    end
  end
end
