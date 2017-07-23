#!/usr/bin/env ruby
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013-2017 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

# Parses output of the glibc backtrace() function and
# translates addresses into function names and source
# file locations.

class Addr2line
  attr_reader :stdin, :stdout, :pid

  def initialize(filename)
    a, b = IO.pipe
    c, d = IO.pipe
    @pid = fork do
      STDIN.reopen(a)
      b.close
      STDOUT.reopen(d)
      c.close
      exec("addr2line", "-Cfie", filename)
    end
    a.close
    d.close
    @stdin = b
    @stdout = c
    @stdin.sync = @stdout.sync = true
  end

  def query(address)
    @stdin.write("#{address}\n")
    function = @stdout.readline
    source = @stdout.readline
    function.strip!
    source.strip!
    return [function, source]
  end

  def close
    @stdin.close
    @stdout.close
    Process.kill('KILL', @pid)
    Process.waitpid(@pid) rescue nil
  end
end

def passthrough(input, output)
  while !input.eof?
    data = input.readline
    output.write(data)
    output.flush
  end
end

def debug(message)
  #puts message
end

input = STDIN
output = STDERR
output.sync = true
output.puts "Using backtrace sanitizer."
argv0, pid_or_filename = ARGV
if pid_or_filename =~ /\A\d+\Z/
  begin
    exe_filename = File.expand_path(File.readlink("/proc/#{pid_or_filename}/exe"))
  rescue Errno::ENOENT, Errno::EACCES => e
    warn "*** backtrace-sanitizer warning: #{e} -> passthrough input"
    passthrough(input, output)
    exit
  end
else
  exe_filename = File.expand_path(pid_or_filename)
end
exe_basename = File.basename(exe_filename)
addr2lines = {}

begin
  while !input.eof?
    line = input.readline.strip
    debug " ---> Parse #{line.inspect}"

    # Example lines:
    # ./test() [0x400b64]
    # /lib/libc.so.6(__libc_start_main+0xfd) [0x7fcc0ad00c8d]
    # Passenger core[0x4d2697]
    if line =~ /(.*)\[(.*?)\]$/
      # Split line into:
      # subject: /lib/libc.so.6(__libc_start_main+0xfd)
      # address: 0x7fcc0ad00c8d
      subject = $1
      address = $2
      subject.strip!

      if subject =~ /(.*?)(\((.*?)\))?$/
        # Split subject into:
        # file: /lib/libc.so.6
        # context: __libc_start_main+0xfd
        file = $1
        context = $3
        file.strip!
        context = nil if context && context.empty?
      else
        file = subject
        context = nil
      end

      debug "      file = #{file.inspect}"
      debug "      context = #{context.inspect}"
      debug "      address = #{address.inspect}"

      if file =~ /\A\// && File.exist?(file)
        filename = file
      elsif file == argv0 ||
            file == exe_filename ||
            file == exe_basename ||
            file.sub(/ .*/, '') == exe_basename
        filename = exe_filename
      else
        filename = nil
      end
      if filename.nil?
        output.puts line
      else
        addr2line = (addr2lines[filename] ||= Addr2line.new(filename))
        function, source = addr2line.query(address)
        new_context = "#{function} at #{source}"
        new_context << "; #{context}" if context
        output.puts "#{file}(#{new_context}) [#{address}]"
      end
    else
      output.puts line
    end
  end
ensure
  addr2lines.each_value do |addr2line|
    addr2line.close
  end
end
