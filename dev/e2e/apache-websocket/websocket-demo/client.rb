# A WebSocket client, written against nothing but the standard library. It
# talks to the echo server in config.ru through Apache and checks that the
# messages come back intact.
#
# Usage: ruby client.rb <host> <port> [--tls]

require 'digest/sha1'
require 'openssl'
require 'securerandom'
require 'socket'

WEBSOCKET_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
OPCODE_TEXT = 0x1
OPCODE_CLOSE = 0x8

host = ARGV.fetch(0)
port = Integer(ARGV.fetch(1))
tls = ARGV.include?('--tls')

def handshake(socket, host, port)
  key = [ SecureRandom.bytes(16) ].pack('m0')
  socket.write(
    "GET /echo HTTP/1.1\r\n" \
    "Host: #{host}:#{port}\r\n" \
    "Upgrade: websocket\r\n" \
    "Connection: Upgrade\r\n" \
    "Sec-WebSocket-Version: 13\r\n" \
    "Sec-WebSocket-Key: #{key}\r\n" \
    "\r\n"
  )

  head = +''
  head << socket.readpartial(1) until head.end_with?("\r\n\r\n")

  lines = head.split("\r\n")
  status_line = lines.shift
  headers = lines.to_h do |line|
    name, value = line.split(':', 2)
    [ name.downcase.strip, value.to_s.strip ]
  end

  expected = [ Digest::SHA1.digest("#{key}#{WEBSOCKET_GUID}") ].pack('m0')
  [ status_line, headers, expected ]
end

def send_message(socket, payload)
  payload = payload.b
  mask = SecureRandom.bytes(4).bytes
  frame = [ 0x80 | OPCODE_TEXT ].pack('C').b

  # The 0x80 in each length encoding is the mask bit: clients must mask, see
  # RFC 6455 section 5.3.
  length = payload.bytesize
  if length < 126
    frame << [ 0x80 | length ].pack('C')
  elsif length < 65536
    frame << [ 0x80 | 126, length ].pack('Cn')
  else
    frame << [ 0x80 | 127, length ].pack('CQ>')
  end

  frame << mask.pack('C*')
  frame << payload.bytes.each_with_index.map { |byte, i| byte ^ mask[i % 4] }.pack('C*')
  socket.write(frame)
end

def read_exactly(socket, bytes)
  data = socket.read(bytes)
  if data.nil? || data.bytesize < bytes
    raise 'The connection closed while a frame was being read'
  end
  data
end

def read_message(socket)
  first, second = read_exactly(socket, 2).unpack('C2')
  opcode = first & 0x0f
  length = second & 0x7f
  length = read_exactly(socket, 2).unpack1('n') if length == 126
  length = read_exactly(socket, 8).unpack1('Q>') if length == 127

  [ opcode, length.zero? ? ''.b : read_exactly(socket, length) ]
end

failures = 0

def check(description, actual, expected)
  if actual == expected
    puts "ok       #{description}"
    0
  else
    puts "FAILED   #{description}"
    puts "         expected: #{expected.to_s[0, 120].inspect}"
    puts "         actual:   #{actual.to_s[0, 120].inspect}"
    1
  end
end

tcp_socket = TCPSocket.new(host, port)
tcp_socket.sync = true
# A tunnel that buffers instead of streaming would otherwise leave this script
# blocked forever, which is a worse way to learn about it than a failure.
tcp_socket.timeout = 30

if tls
  # The demo's certificate is self-signed, so verification is off on purpose.
  context = OpenSSL::SSL::SSLContext.new
  context.verify_mode = OpenSSL::SSL::VERIFY_NONE
  socket = OpenSSL::SSL::SSLSocket.new(tcp_socket, context)
  socket.hostname = host
  socket.sync_close = true
  socket.connect
else
  socket = tcp_socket
end

begin
  status_line, headers, expected_accept = handshake(socket, host, port)
  failures += check('handshake is answered with 101', status_line,
    'HTTP/1.1 101 Switching Protocols')
  failures += check('Upgrade header survives', headers['upgrade'], 'websocket')
  failures += check('Sec-WebSocket-Accept survives', headers['sec-websocket-accept'],
    expected_accept)
  # The stream that follows a 101 is not an HTTP message body, so neither
  # framing header may be present.
  failures += check('no Content-Length on the 101', headers.key?('content-length'), false)
  failures += check('no Transfer-Encoding on the 101', headers.key?('transfer-encoding'), false)

  send_message(socket, 'hello')
  failures += check('short message round trip', read_message(socket), [ OPCODE_TEXT, 'hello'.b ])

  # Several messages in a row, so that we can tell a working tunnel from one
  # that happens to deliver the first message.
  5.times do |i|
    send_message(socket, "message #{i}")
    failures += check("message #{i} round trip", read_message(socket),
      [ OPCODE_TEXT, "message #{i}".b ])
  end

  # Larger than the tunnel's buffer and past the 16-bit frame length, so both
  # directions have to reassemble across reads and the 64-bit length encoding
  # gets used.
  large = 'abcdefgh' * 32_768
  send_message(socket, large)
  failures += check('256 KB message round trip', read_message(socket),
    [ OPCODE_TEXT, large.b ])

  # Idle for longer than the RequestReadTimeout the demo configures, since a
  # WebSocket spends most of its life doing nothing.
  sleep 4
  send_message(socket, 'still here')
  failures += check('message after an idle period', read_message(socket),
    [ OPCODE_TEXT, 'still here'.b ])

  socket.write([ 0x80 | OPCODE_CLOSE, 0x80 ].pack('C2') + "\x00\x00\x00\x00".b)
ensure
  socket.close
end

puts
if failures.zero?
  puts 'All WebSocket checks passed.'
else
  puts "#{failures} WebSocket check(s) failed."
end
exit(failures.zero? ? 0 : 1)
