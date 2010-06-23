# Modified version of Rack::RewindableInput with Ruby 1.9 fix.

require 'tempfile'

module PhusionPassenger
module Utils

  # Class which can make any IO object rewindable, including non-rewindable ones. It does
  # this by buffering the data into a tempfile, which is rewindable.
  #
  # rack.input is required to be rewindable, so if your input stream IO is non-rewindable
  # by nature (e.g. a pipe or a socket) then you can wrap it in an object of this class
  # to easily make it rewindable.
  #
  # Don't forget to call #close when you're done. This frees up temporary resources that
  # RewindableInput uses, though it does *not* close the original IO object.
  class RewindableInput
    def initialize(io)
      @io = io
      @rewindable_io = nil
      @unlinked = false
    end
    
    def gets
      make_rewindable unless @rewindable_io
      @rewindable_io.gets
    end
    
    def read(*args)
      make_rewindable unless @rewindable_io
      @rewindable_io.read(*args)
    end
    
    def each(&block)
      make_rewindable unless @rewindable_io
      @rewindable_io.each(&block)
    end
    
    def rewind
      make_rewindable unless @rewindable_io
      @rewindable_io.rewind
    end
    
    def size
      make_rewindable unless @rewindable_io
      @rewindable_io.size
    end
    
    # Closes this RewindableInput object without closing the originally
    # wrapped IO oject. Cleans up any temporary resources that this RewindableInput
    # has created.
    #
    # This method may be called multiple times. It does nothing on subsequent calls.
    def close
      if @rewindable_io
        if @unlinked
          @rewindable_io.close
        else
          @rewindable_io.close!
        end
        @rewindable_io = nil
      end
    end
    
    private
    
    if RUBY_VERSION < '1.9.0'
      # Many Ruby 1.8's tempfile libraries have a bug that can cause
      # the #close method to raise an exception. Subclass it and fix
      # it.
      class Tempfile < ::Tempfile
        def _close
          @tmpfile.close if @tmpfile
          @data[1] = nil if @data
          @tmpfile = nil
        end
      end
    end

    def make_rewindable
      # Buffer all data into a tempfile. Since this tempfile is private to this
      # RewindableInput object, we chmod it so that nobody else can read or write
      # it. On POSIX filesystems we also unlink the file so that it doesn't
      # even have a file entry on the filesystem anymore, though we can still
      # access it because we have the file handle open.
      @rewindable_io = Tempfile.new('RackRewindableInput')
      @rewindable_io.chmod(0000)
      @rewindable_io.set_encoding(Encoding::BINARY) if @rewindable_io.respond_to?(:set_encoding)
      @rewindable_io.binmode
      if filesystem_has_posix_semantics? && !tempfile_unlink_contains_bug?
        @rewindable_io.unlink
        @unlinked = true
      end
      
      buffer = ""
      while @io.read(1024 * 4, buffer)
        entire_buffer_written_out = false
        while !entire_buffer_written_out
          written = @rewindable_io.write(buffer)
          entire_buffer_written_out = written == buffer.size
          if !entire_buffer_written_out
            buffer.slice!(0 .. written - 1)
          end
        end
      end
      @rewindable_io.rewind
    end
    
    def filesystem_has_posix_semantics?
      RUBY_PLATFORM !~ /(mswin|mingw|cygwin|java)/
    end
    
    def tempfile_unlink_contains_bug?
      # The tempfile library as included in Ruby 1.9.1-p152 and later
      # contains a bug: unlinking an open Tempfile object also closes
      # it, which breaks our expected POSIX semantics. This problem
      # has been fixed in Ruby 1.9.2, but the Ruby team chose not to
      # include the bug fix in later versions of the 1.9.1 series.
      ruby_engine = defined?(RUBY_ENGINE) ? RUBY_ENGINE : "ruby"
      ruby_engine == "ruby" && RUBY_VERSION == "1.9.1" && RUBY_PATCHLEVEL >= 152
    end
  end

end # module Utils
end # module PhusionPassenger
