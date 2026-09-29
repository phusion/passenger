# A WebSocket echo server, written against nothing but the standard library so
# that the demo has no gem dependencies.
#
# Rack's full hijack hands the application the connection, after which it is
# responsible for the opening handshake and for the framing. That is also how
# ActionCable and other real WebSocket stacks work, so if this app is reachable
# through Apache then so are they.

require 'digest/sha1'

# RFC 6455 section 1.3.
WEBSOCKET_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'

OPCODE_TEXT = 0x1
OPCODE_BINARY = 0x2
OPCODE_CLOSE = 0x8

# Reads exactly `bytes` bytes, or nil if the stream ended first.
def read_exactly(io, bytes)
  data = io.read(bytes)
  (data.nil? || data.bytesize < bytes) ? nil : data
end

# Reads one frame. Returns [opcode, payload], or nil at end of stream.
# Continuation frames are not handled: this demo only ever receives whole
# messages.
def read_frame(io)
  header = read_exactly(io, 2)
  return nil if header.nil?

  first, second = header.unpack('C2')
  opcode = first & 0x0f
  masked = (second & 0x80) != 0
  length = second & 0x7f
  length = read_exactly(io, 2)&.unpack1('n') if length == 126
  length = read_exactly(io, 8)&.unpack1('Q>') if length == 127
  return nil if length.nil?

  mask = masked ? read_exactly(io, 4)&.bytes : nil
  return nil if masked && mask.nil?

  payload = length.zero? ? ''.b : read_exactly(io, length)
  return nil if payload.nil?

  if mask
    payload = payload.bytes.each_with_index.map { |byte, i| byte ^ mask[i % 4] }.pack('C*')
  end

  [ opcode, payload ]
end

# Builds an unmasked frame. Servers must not mask; clients must.
def build_frame(opcode, payload)
  frame = [ 0x80 | opcode ].pack('C').b
  length = payload.bytesize

  if length < 126
    frame << [ length ].pack('C')
  elsif length < 65536
    frame << [ 126, length ].pack('Cn')
  else
    frame << [ 127, length ].pack('CQ>')
  end

  frame << payload.b
end

app = lambda do |env|
  if env['PATH_INFO'] != '/echo'
    body = "WebSocket echo server. Connect to ws://<host>/echo.\n"
    next [ 200, { 'Content-Type' => 'text/plain', 'Content-Length' => body.bytesize.to_s }, [ body ] ]
  end

  key = env['HTTP_SEC_WEBSOCKET_KEY']
  if env['HTTP_UPGRADE'].to_s.downcase != 'websocket' || key.nil?
    body = "Not a WebSocket handshake\n"
    next [ 400, { 'Content-Type' => 'text/plain', 'Content-Length' => body.bytesize.to_s }, [ body ] ]
  end

  accept = [ Digest::SHA1.digest("#{key}#{WEBSOCKET_GUID}") ].pack('m0')
  env['rack.hijack'].call
  io = env['rack.hijack_io']

  begin
    io.write("HTTP/1.1 101 Switching Protocols\r\n")
    io.write("Upgrade: websocket\r\n")
    io.write("Connection: Upgrade\r\n")
    io.write("Sec-WebSocket-Accept: #{accept}\r\n")
    io.write("\r\n")
    io.flush

    while (frame = read_frame(io))
      opcode, payload = frame
      break if opcode == OPCODE_CLOSE

      io.write(build_frame(opcode, payload))
      io.flush
    end
  rescue EOFError, Errno::ECONNRESET, Errno::EPIPE
  ensure
    io.close
  end
end

run app
