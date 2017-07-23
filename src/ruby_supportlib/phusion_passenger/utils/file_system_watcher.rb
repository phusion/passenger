#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'native_support'

module PhusionPassenger
  module Utils

    # Watches changes on one or more files or directories. To use this class,
    # construct an object, passing it file or directory names to watch, then
    # call #wait_for_change. #wait_for_change waits until one of the following
    # events has happened since the constructor was called:
    #
    # - One of the specified files has been renamed, deleted, or its access
    #   revoked. This will cause +true+ to be returned.
    # - One of the specified directories has been modified, renamed, deleted,
    #   or its access revoked. This will cause +true+ to be returned.
    # - +termination_pipe+ (as passed to the constructor) becomes readable.
    #   This will cause +nil+ to be returned.
    # - The thread is interrupted. This will cause +nil+ to be returned.
    #
    # The constructor will attempt to stat and possibly also open all specified
    # files/directories. If one of them cannot be statted or opened, then
    # +false+ will be returned by #wait_for_change.
    #
    # #wait_for_change may only be called once. After calling it one should
    # create a new object if one wishes to watch the filesystem again.
    #
    # Always call #close when a FileSystemWatcher object is no longer needed
    # in order to free resources.
    #
    # This class tries to use kqueue for efficient filesystem watching on
    # platforms that support it. On other platforms it'll fallback to stat
    # polling instead.

    if defined?(NativeSupport::FileSystemWatcher)
      FileSystemWatcher = NativeSupport::FileSystemWatcher

      FileSystemWatcher.class_eval do
        def self.new(filenames, termination_pipe = nil)
          # Default parameter values, type conversion and exception
          # handling in C is too much of a pain.
          filenames = filenames.map do |filename|
            filename.to_s
          end
          return _new(filenames, termination_pipe)
        end

        def self.opens_files?
          return true
        end
      end
    else
      class FileSystemWatcher
        attr_accessor :poll_interval

        def self.opens_files?
          return false
        end

        def initialize(filenames, termination_pipe = nil)
          @poll_interval = 3
          @termination_pipe = termination_pipe
          @dirs  = []
          @files = []

          begin
            filenames.each do |filename|
              stat = File.stat(filename)
              if stat.directory?
                @dirs << DirInfo.new(filename, stat)
              else
                @files << FileInfo.new(filename, stat)
              end
            end
          rescue Errno::EACCES, Errno::ENOENT
            @dirs = @files = nil
          end
        end

        def wait_for_change
          if !@dirs
            return false
          end

          while true
            if changed?
              return true
            elsif select([@termination_pipe], nil, nil, @poll_interval)
              return nil
            end
          end
        end

        def close
        end

      private
        class DirInfo
          DOT    = "."
          DOTDOT = ".."

          def initialize(filename, stat)
            @filename = filename
            @stat = stat
            @subfiles = {}
            Dir.foreach(filename) do |entry|
              next if entry == DOT || entry == DOTDOT
              subfilename = "#{filename}/#{entry}"
              @subfiles[entry] = FileInfo.new(subfilename, File.stat(subfilename))
            end
          end

          def changed?
            new_stat = File.stat(@filename)
            if @stat.ino != new_stat.ino || !new_stat.directory? || @stat.mtime != new_stat.mtime
              return true
            end

            count = 0
            Dir.foreach(@filename) do |entry|
              next if entry == DOT || entry == DOTDOT
              subfilename = "#{@filename}/#{entry}"

              file_info = @subfiles[entry]
              if !file_info || file_info.changed?(false)
                return true
              else
                count += 1
              end
            end

            return count != @subfiles.size
          rescue Errno::EACCES, Errno::ENOENT
            return true
          end
        end

        class FileInfo
          def initialize(filename, stat)
            @filename = filename
            @stat = stat
          end

          def changed?(check_mtime = true)
            new_stat = File.stat(@filename)
            if check_mtime
              mtime_changed = @stat.mtime != new_stat.mtime || @stat.size != new_stat.size
            else
              mtime_changed = false
            end
            return @stat.ino != new_stat.ino || @stat.ftype != new_stat.ftype || mtime_changed
          rescue Errno::EACCES, Errno::ENOENT
            return true
          end
        end

        def changed?
          return @dirs.any?  { |dir_info| dir_info.changed? } ||
                 @files.any? { |file_info| file_info.changed? }
        end
      end
    end

  end # module Utils
end # module PhusionPassenger
