# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2015 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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

begin
  require 'rubygems'
rescue LoadError
end
require 'socket'
require 'thread'
if (!defined?(RUBY_ENGINE) || RUBY_ENGINE == "ruby") && RUBY_VERSION < "1.8.7"
  begin
    require 'fastthread'
  rescue LoadError
    abort "You are using a very old Ruby version. You must install " +
      "the 'fastthread' gem to fix some bugs in the Ruby threading system: " +
      "gem install fastthread"
  end
end

class Exception
  def backtrace_string(current_location = nil)
    if current_location.nil?
      location = nil
    else
      location = "in #{current_location} "
    end
    current_thread = Thread.current
    if !(thread_id = current_thread[:id])
      current_thread.to_s =~ /:(0x[0-9a-f]+)/i
      thread_id = $1 || '?'
    end
    if thread_name = current_thread[:name]
      thread_name = "(#{thread_name})"
    end
    return "*** Exception #{self.class} #{location}" <<
      "(#{self}) (process #{$$}, thread #{thread_id}#{thread_name}):\n" <<
      "\tfrom " << backtrace.join("\n\tfrom ")
  end
end

class Dir
  # Dir.pwd resolves symlinks. This version tries not to, by shelling
  # out to the pwd tool.
  def self.pwd_no_resolve
    begin
      result = `pwd`.strip
    rescue Errno::ENOENT
      result = `/bin/pwd`.strip
    end
    if result.empty?
      return Dir.pwd
    else
      return result
    end
  end
end

class File
  # Dir.pwd resolves symlinks. So in turn, File.expand_path/File.absolute_path
  # do that too. This method fixes that by using Dir.pwd_no_resolve.
  if File.respond_to?(:absolute_path)
    def self.absolute_path_no_resolve(path)
      return File.absolute_path(path, Dir.pwd_no_resolve)
    end
  else
    def self.absolute_path_no_resolve(path)
      return File.expand_path(path, Dir.pwd_no_resolve)
    end
  end
end

module Signal
  # Like Signal.list, but only returns signals that we can actually trap.
  def self.list_trappable
    ruby_engine = defined?(RUBY_ENGINE) ? RUBY_ENGINE : "ruby"
    case ruby_engine
    when "jruby"
      result = Signal.list.dup
      result.delete("QUIT")
      result.delete("ILL")
      result.delete("FPE")
      result.delete("SEGV")
      result.delete("USR1")
      result.delete("IOT")
      result.delete("EXIT")
    else
      result = Signal.list.dup
      result.delete("ALRM")
      result.delete("VTALRM")
    end

    # Don't touch SIGCHLD no matter what! On OS X waitpid() will
    # malfunction if SIGCHLD doesn't have a correct handler.
    result.delete("CLD")
    result.delete("CHLD")

    # Other stuff that we don't want to trap no matter which
    # Ruby engine.
    result.delete("STOP")
    result.delete("KILL")

    return result
  end
end

module GC
  if !respond_to?(:copy_on_write_friendly?)
    # Checks whether the current Ruby interpreter's garbage
    # collector is copy-on-write friendly.
    def self.copy_on_write_friendly?
      return false
    end
  end
end
