# encoding: binary
#
# This file is taken from Unicorn. The following license applies to this file
# (and this file only, not to the rest of Phusion Passenger):
#
# 1. You may make and give away verbatim copies of the source form of the
#    software without restriction, provided that you duplicate all of the
#    original copyright notices and associated disclaimers.
#
# 2. You may modify your copy of the software in any way, provided that
#    you do at least ONE of the following:
#
#      a) place your modifications in the Public Domain or otherwise make them
#      Freely Available, such as by posting said modifications to Usenet or an
#      equivalent medium, or by allowing the author to include your
#      modifications in the software.
#
#      b) use the modified software only within your corporation or
#         organization.
#
#      c) rename any non-standard executables so the names do not conflict with
#      standard executables, which must also be provided.
#
#      d) make other distribution arrangements with the author.
#
# 3. You may distribute the software in object code or executable
#    form, provided that you do at least ONE of the following:
#
#      a) distribute the executables and library files of the software,
#      together with instructions (in the manual page or equivalent) on where
#      to get the original distribution.
#
#      b) accompany the distribution with the machine-readable source of the
#      software.
#
#      c) give non-standard executables non-standard names, with
#         instructions on where to get the original software distribution.
#
#      d) make other distribution arrangements with the author.
#
# 4. You may modify and include the part of the software into any other
#    software (possibly commercial).  But some files in the distribution
#    are not written by the author, so that they are not under this terms.
#
# 5. The scripts and library files supplied as input to or produced as
#    output from the software do not automatically fall under the
#    copyright of the software, but belong to whomever generated them,
#    and may be sold commercially, and may be aggregated with this
#    software.
#
# 6. THIS SOFTWARE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR
#    IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
#    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#    PURPOSE.

require 'stringio'
PhusionPassenger.require_passenger_lib 'utils/tmpio'

module PhusionPassenger
module Utils

# acts like tee(1) on an input input to provide a input-like stream
# while providing rewindable semantics through a File/StringIO backing
# store.  On the first pass, the input is only read on demand so your
# Rack application can use input notification (upload progress and
# like).  This should fully conform to the Rack::Lint::InputWrapper
# specification on the public API.  This class is intended to be a
# strict interpretation of Rack::Lint::InputWrapper functionality and
# will not support any deviations from it.
#
# When processing uploads, Unicorn exposes a TeeInput object under
# "rack.input" of the Rack environment.
class TeeInput
  CONTENT_LENGTH = "CONTENT_LENGTH".freeze
  HTTP_TRANSFER_ENCODING = "HTTP_TRANSFER_ENCODING".freeze
  CHUNKED = "chunked".freeze

  # The maximum size (in +bytes+) to buffer in memory before
  # resorting to a temporary file.  Default is 112 kilobytes.
  @@client_body_buffer_size = 112 * 1024

  # sets the maximum size of request bodies to buffer in memory,
  # amounts larger than this are buffered to the filesystem
  def self.client_body_buffer_size=(bytes)
    @@client_body_buffer_size = bytes
  end

  # returns the maximum size of request bodies to buffer in memory,
  # amounts larger than this are buffered to the filesystem
  def self.client_body_buffer_size
    @@client_body_buffer_size
  end

  # Initializes a new TeeInput object.  You normally do not have to call
  # this unless you are writing an HTTP server.
  def initialize(socket, env)
    if @len = env[CONTENT_LENGTH]
      @len = @len.to_i
    elsif env[HTTP_TRANSFER_ENCODING] != CHUNKED
      @len = 0
    end
    @socket = socket
    @bytes_read = 0
    if @len && @len <= @@client_body_buffer_size
      @tmp = StringIO.new("")
    else
      @tmp = TmpIO.new("PassengerTeeInput")
    end
    @tmp.binmode
  end

  def close
    @tmp.close
  end

  def size
    if @len
      @len
    else
      pos = @tmp.pos
      consume!
      @tmp.pos = pos
      @len = @tmp.size
    end
  end

  def read(len = nil, buf = "")
    buf ||= ""
    if len
      if len < 0
        raise ArgumentError, "negative length #{len} given"
      elsif len == 0
        buf.replace('')
        buf
      else
        if socket_drained?
          @tmp.read(len, buf)
        else
          tee(read_exact(len, buf))
        end
      end
    else
      if socket_drained?
        @tmp.read(nil, buf)
      else
        tee(read_all(buf))
      end
    end
  end

  def gets
    if socket_drained?
      @tmp.gets
    else
      if @bytes_read == @len
        nil
      elsif line = @socket.gets
        if @len
          max_len = @len - @bytes_read
          line.slice!(max_len, line.size - max_len)
        end
        @bytes_read += line.size
        tee(line)
      else
        nil
      end
    end
  end

  def seek(*args)
    if !socket_drained?
      # seek may be forward, or relative to the end, so we need to consume the socket fully into tmp
      pos = @tmp.pos # save/restore tmp.pos, to not break relative seeks
      consume!
      @tmp.pos = pos
    end
    @tmp.seek(*args)
  end

  def rewind
    return 0 if 0 == @tmp.size
    consume! if !socket_drained?
    @tmp.rewind # Rack does not specify what the return value is here
  end

  def each
    while line = gets
      yield line
    end

    self # Rack does not specify what the return value is here
  end

  # Rack repeatedly introduces bugs that rely on this method existing
  # https://github.com/rack/rack/pull/1201
  def eof?
    socket_drained?
  end

private

  def socket_drained?
    if @socket
      if @socket.eof?
        @socket = nil
        true
      else
        false
      end
    else
      true
    end
  end

  # consumes the stream of the socket
  def consume!
    junk = ""
    nil while read(16 * 1024, junk)
    @socket = nil
  end

  def tee(buffer)
    if buffer && buffer.size > 0
      @tmp.write(buffer)
    end
    buffer
  end

  def read_exact(len, buf)
    if @len
      max_len = @len - @bytes_read
      len = max_len if len > max_len
      return nil if len == 0
    end
    ret = @socket.read(len, buf)
    @bytes_read += ret.size if ret
    ret
  end

  def read_all(buf)
    if @len
      ret = @socket.read(@len - @bytes_read, buf)
      if ret
        @bytes_read += ret.size
        ret
      else
        buf.replace("")
        buf
      end
    else
      ret = @socket.read(nil, buf)
      @bytes_read += ret.size
      ret
    end
  end
end

end # module Utils
end # module PhusionPassenger
