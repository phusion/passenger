require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
PhusionPassenger.require_passenger_lib 'debug_logging'
require 'stringio'
require 'tmpdir'
require 'fileutils'

module PhusionPassenger

describe DebugLogging do
  after :each do
    DebugLogging.log_level = DEFAULT_LOG_LEVEL
    DebugLogging.log_file = nil
    DebugLogging.stderr_evaluator = nil
    FileUtils.rm_rf(@tmpdir) if @tmpdir
  end

  class MyClass
    include DebugLogging
  end

  def use_log_file!
    @tmpdir = Dir.mktmpdir
    @log_file = "#{@tmpdir}/debug.log"
    DebugLogging.log_file = @log_file
  end

  describe "#debug" do
    it "doesn't print the message if log level is LVL_INFO" do
      use_log_file!
      DebugLogging.log_level = LVL_INFO
      DebugLogging.debug("hello")
      File.exist?(@log_file).should be_false
    end

    it "prints the message if log level is LVL_DEBUG" do
      use_log_file!
      DebugLogging.log_level = LVL_DEBUG
      DebugLogging.debug("hello")
      File.exist?(@log_file).should be_true
    end

    it "prints the message if log level is greater than LVL_DEBUG" do
      use_log_file!
      DebugLogging.log_level = LVL_DEBUG2
      DebugLogging.debug("hello")
      File.exist?(@log_file).should be_true
    end

    it "prints the location of the calling function" do
      use_log_file!
      DebugLogging.log_level = LVL_DEBUG
      DebugLogging.debug("hello")
      File.read(@log_file).should include("debug_logging_spec.rb")
    end

    it "prints to STDERR by default" do
      io = StringIO.new
      DebugLogging.log_level = LVL_DEBUG
      DebugLogging.stderr_evaluator = lambda { io }
      DebugLogging.debug("hello")
      io.string.should include("hello")
    end

    it "reopens the log file handle if it has been closed" do
      use_log_file!
      DebugLogging.log_level = LVL_DEBUG
      DebugLogging.debug("hello")
      DebugLogging._log_device.close
      DebugLogging.debug("world")
      File.read(@log_file).should include("world")
    end

    it "also works as included method" do
      use_log_file!
      DebugLogging.log_level = LVL_DEBUG
      MyClass.new.send(:debug, "hello")
      File.read(@log_file).should include("hello")
    end

    it "is private when included" do
      MyClass.private_method_defined?(:debug)
    end
  end

  describe "#trace" do
    specify "#trace(x, ...) doesn't print the message if the log level is lower than LVL_INFO + x" do
      use_log_file!
      DebugLogging.log_level = LVL_INFO + 1
      DebugLogging.trace(2, "hello")
      File.exist?(@log_file).should be_false
    end

    specify "#trace(x, ...) prints the message if the log level equals LVL_INFO + 2" do
      use_log_file!
      DebugLogging.log_level = LVL_INFO + 2
      DebugLogging.trace(2, "hello")
      File.exist?(@log_file).should be_true
    end

    specify "#trace(x, ...) prints the message if the log level is greater than LVL_INFO + 3" do
      use_log_file!
      DebugLogging.log_level = LVL_INFO + 3
      DebugLogging.trace(2, "hello")
      File.exist?(@log_file).should be_true
    end

    specify "#trace prints the location of the calling function" do
      io = StringIO.new
      DebugLogging.log_level = LVL_DEBUG
      DebugLogging.stderr_evaluator = lambda { io }
      DebugLogging.trace(1, "hello")
      io.string.should include("hello")
    end

    it "prints to STDERR by default" do
      io = StringIO.new
      DebugLogging.log_level = LVL_DEBUG
      DebugLogging.stderr_evaluator = lambda { io }
      DebugLogging.trace(1, "hello")
      io.string.should include("hello")
    end

    it "reopens the log file handle if it has been closed" do
      use_log_file!
      DebugLogging.log_level = LVL_DEBUG
      DebugLogging.trace(1, "hello")
      DebugLogging._log_device.close
      DebugLogging.trace(1, "world")
      File.read(@log_file).should include("world")
    end

    it "also works as included method" do
      use_log_file!
      DebugLogging.log_level = LVL_DEBUG
      MyClass.new.send(:trace, 1, "hello")
      File.read(@log_file).should include("hello")
    end

    it "is private when included" do
      MyClass.private_method_defined?(:trace)
    end
  end
end

end # module PhusionPassenger
