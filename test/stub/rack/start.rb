#!/usr/bin/env ruby
require 'socket'

STDOUT.sync = true
STDERR.sync = true

work_dir = ENV['PASSENGER_SPAWN_WORK_DIR']
ruby_libdir = File.read("#{work_dir}/args/ruby_libdir").strip
passenger_root = File.read("#{work_dir}/args/passenger_root").strip
require "#{ruby_libdir}/phusion_passenger"
PhusionPassenger.locate_directories(passenger_root)
PhusionPassenger.require_passenger_lib 'utils/json'

if ARGV[0] == "--execself"
  # Used for testing https://code.google.com/p/phusion-passenger/issues/detail?id=842#c19
  exec("ruby", $0)
end

server = TCPServer.new('127.0.0.1', 0)
File.open("#{work_dir}/response/properties.json", 'w') do |f|
  f.write(PhusionPassenger::Utils::JSON.generate(
    :sockets => [
      {
        :address => "tcp://127.0.0.1:#{server.addr[1]}",
        :protocol => "test",
        :concurrency => 1,
        :accept_http_requests => true
      }
    ]
  ))
end
File.open("#{work_dir}/response/finish", 'w') do |f|
  f.write('1')
end

while true
  ios = select([server, STDIN])[0]
  if ios.include?(server)
    client = server.accept
    line = client.readline
    if line == "ping\n"
      client.write("pong\n")
    elsif line == "pid\n"
      client.write("#{Process.pid}\n")
    elsif line == "envvars\n"
      str = ""
      ENV.each_pair do |key, value|
        str << "#{key} = #{value}\n"
      end
      client.write(str)
    else
      client.write("unknown request\n")
    end
    client.close
  end
  if ios.include?(STDIN)
    begin
      STDIN.readline
    rescue EOFError
      exit
    end
  end
end
