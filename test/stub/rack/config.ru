# encoding: binary

require File.expand_path(File.dirname(__FILE__) + '/library')
require 'cgi'
require 'digest/sha1'

# RFC 6455 section 1.3. A local rather than a constant, because config.ru is
# evaluated rather than required and may be evaluated more than once.
websocket_guid = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'

app = lambda do |env|
  case env['PATH_INFO']
  when '/'
    params = CGI.parse(env['QUERY_STRING'])
    if params['sleep_seconds'].first
      sleep params['sleep_seconds'].first.to_f
    end

    if File.exist?('front_page.txt')
      text_response(File.read('front_page.txt'))
    else
      text_response('front page')
    end
  when '/parameters'
    req = Rack::Request.new(env)
    method = env['REQUEST_METHOD']
    first = req.params['first']
    second = req.params['second']
    text_response("Method: #{method}\nFirst: #{first}\nSecond: #{second}\n")
  when '/chunked'
    chunks = [ "7\r\nchunk1\n\r\n", "7\r\nchunk2\n\r\n", "7\r\nchunk3\n\r\n", "0\r\n\r\n" ]
    [ 200, { 'Content-Type' => 'text/html', 'Transfer-Encoding' => 'chunked' }, chunks ]
  when '/pid'
    text_response(Process.pid)
  when /^\/env/
    body = ''
    env.sort.each do |key, value|
      body << "#{key} = #{value}\n"
    end
    text_response(body)
  when '/system_env'
    body = ''
    ENV.sort.each do |key, value|
      body << "#{key} = #{value}\n"
    end
    text_response(body)
  when '/touch_file'
    req = Rack::Request.new(env)
    filename = req.params['file']
    File.open(filename, 'w').close
    text_response('ok')
  when '/extra_header'
    [ 200, { 'Content-Type' => 'text/html', 'X-Foo' => 'Bar' }, [ 'ok' ] ]
  when '/cached'
    text_response('This is the uncached version of /cached')
  when '/upload_with_params'
    req = Rack::Request.new(env)
    name1 = req.params['name1'].b
    name2 = req.params['name2'].b
    file = req.params['data'][:tempfile]
    file.binmode
    text_response(
      "name 1 = #{name1}\n" <<
      "name 2 = #{name2}\n" <<
      "data = #{file.read}")
  when '/raw_upload_to_file'
    File.open(env['HTTP_X_OUTPUT'], 'w') do |f|
      while line = env['rack.input'].gets
        f.write(line)
        f.flush
      end
    end
    text_response('ok')
  when '/print_stderr'
    STDERR.puts 'hello world!'
    text_response('ok')
  when '/print_stdout_and_stderr'
    STDOUT.puts 'hello stdout!'
    sleep 0.1  # Give Passenger core some time to process stdout first.
    STDERR.puts 'hello stderr!'
    text_response('ok')
  when '/switch_protocol'
    if env['HTTP_UPGRADE'] != 'raw' || env['HTTP_CONNECTION'].downcase != 'upgrade'
      return [ 500, { 'Content-Type' => 'text/plain' }, [ 'Invalid headers' ] ]
    end
    env['rack.hijack'].call
    io = env['rack.hijack_io']
    begin
      # A hijacked connection that switches protocols must announce it with a
      # real status line; a CGI-style "Status:" header is not enough for
      # Passenger to recognise the switch.
      io.write("HTTP/1.1 101 Switching Protocols\r\n")
      io.write("Upgrade: raw\r\n")
      io.write("Connection: Upgrade\r\n")
      io.write("\r\n")
      while !io.eof?
        line = io.readline
        io.write("Echo: #{line}")
        io.flush
      end
    rescue EOFError, Errno::ECONNRESET, Errno::EPIPE
    ensure
      io.close
    end
  when '/switch_protocol_stream_echo'
    # Echoes each chunk as it arrives, rather than a line at a time, so that
    # the application is writing back while the peer is still sending. That
    # saturates both directions at once.
    env['rack.hijack'].call
    io = env['rack.hijack_io']
    begin
      io.write("HTTP/1.1 101 Switching Protocols\r\n")
      io.write("Upgrade: raw\r\n")
      io.write("Connection: Upgrade\r\n")
      io.write("\r\n")
      io.flush
      loop do
        io.write(io.readpartial(16384))
        io.flush
      end
    rescue EOFError, Errno::ECONNRESET, Errno::EPIPE
    ensure
      io.close
    end
  when '/switch_protocol_with_headers'
    # Switches protocols with response headers chosen by the query string:
    # `upgrade` sets the Upgrade header (omitted when empty), and every other
    # parameter is sent as a header of its own.
    params = CGI.parse(env['QUERY_STRING'])
    env['rack.hijack'].call
    io = env['rack.hijack_io']
    begin
      io.write("HTTP/1.1 101 Switching Protocols\r\n")
      upgrade = params.delete('upgrade')&.first
      if upgrade && !upgrade.empty?
        io.write("Upgrade: #{upgrade}\r\n")
        io.write("Connection: Upgrade\r\n")
      else
        # Without an Upgrade header the core does not recognise the switch,
        # takes this for an ordinary response, and would put the hijacked
        # (and about to be closed) connection back into its keep-alive pool
        # for the next request to trip over.
        io.write("Connection: close\r\n")
      end
      params.each_pair { |name, values| io.write("#{name}: #{values.first}\r\n") }
      io.write("\r\n")
      io.write("ok\n")
      io.flush
    rescue Errno::ECONNRESET, Errno::EPIPE
    ensure
      io.close
    end
  when '/switch_protocol_and_close'
    # Switches protocols, says one thing and hangs up.
    env['rack.hijack'].call
    io = env['rack.hijack_io']
    begin
      io.write("HTTP/1.1 101 Switching Protocols\r\n")
      io.write("Upgrade: raw\r\n")
      io.write("Connection: Upgrade\r\n")
      io.write("\r\n")
      io.write("goodbye\n")
      io.flush
    rescue Errno::ECONNRESET, Errno::EPIPE
    ensure
      io.close
    end
  when '/websocket_handshake'
    # Performs a real WebSocket handshake and then echoes bytes back; no frame
    # parsing.
    key = env['HTTP_SEC_WEBSOCKET_KEY']
    if env['HTTP_UPGRADE'].to_s.downcase != 'websocket' || key.nil?
      return [ 400, { 'Content-Type' => 'text/plain' }, [ 'Not a WebSocket handshake' ] ]
    end

    accept = [ Digest::SHA1.digest("#{key}#{websocket_guid}") ].pack('m0')
    env['rack.hijack'].call
    io = env['rack.hijack_io']
    begin
      io.write("HTTP/1.1 101 Switching Protocols\r\n")
      io.write("Upgrade: websocket\r\n")
      io.write("Connection: Upgrade\r\n")
      io.write("Sec-WebSocket-Accept: #{accept}\r\n")
      io.write("\r\n")
      io.flush
      loop do
        io.write(io.readpartial(16384))
        io.flush
      end
    rescue EOFError, Errno::ECONNRESET, Errno::EPIPE
    ensure
      io.close
    end
  else
    [ 404, { 'Content-Type' => 'text/plain' }, [ 'Unknown URI' ] ]
  end
end

run app
