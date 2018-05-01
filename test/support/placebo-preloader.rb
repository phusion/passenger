#!/usr/bin/env ruby
# An application preloader which doesn't actually preload anything
# and executes the requested start command.

DIR = File.expand_path(File.dirname(__FILE__))
require File.expand_path("#{DIR}/../../src/ruby_supportlib/phusion_passenger")
PhusionPassenger.locate_directories
PhusionPassenger.require_passenger_lib 'native_support'
PhusionPassenger.require_passenger_lib 'utils/json'
require 'socket'

STDOUT.sync = true
STDERR.sync = true

work_dir = ENV['PASSENGER_SPAWN_WORK_DIR']

socket_filename = "/tmp/placebo-preloader.sock.#{Process.pid}"
server = UNIXServer.new(socket_filename)
File.open("#{work_dir}/response/properties.json", 'w') do |f|
  f.write(PhusionPassenger::Utils::JSON.generate(
    :sockets => [
      {
        :address => "unix:#{socket_filename}",
        :protocol => 'preloader',
        :concurrency => 1
      }
    ]
  ))
end
File.open("#{work_dir}/response/finish", 'w') do |f|
  f.write('1')
end

def process_client_command(server, client, data)
  doc = PhusionPassenger::Utils::JSON.parse(data)
  if doc['command'] == 'spawn'
    work_dir = doc['work_dir']
    options = PhusionPassenger::Utils::JSON.parse(File.read("#{work_dir}/args.json"))

    pid = fork
    if pid.nil?
      STDIN.reopen("#{work_dir}/stdin", 'r')
      STDOUT.reopen("#{work_dir}/stdout_and_err", 'w')
      STDERR.reopen(STDERR)
      STDOUT.sync = STDERR.sync = true
      server.close
      client.close

      ENV['PASSENGER_SPAWN_WORK_DIR'] = work_dir
      exec(options['start_command'])
    else
      client.write(PhusionPassenger::Utils::JSON.generate(
        :result => 'ok',
        :pid => pid
      ))
      if defined?(NativeSupport)
        NativeSupport.detach_process(pid)
      else
        Process.detach(pid)
      end
    end
  elsif doc['command'] == 'pid'
    client.write(PhusionPassenger::Utils::JSON.generate(
      :result => 'ok',
      :pid => Process.pid
    ))
  else
    client.write(PhusionPassenger::Utils::JSON.generate(
      :result => 'error',
      :message => "Unknown command #{doc.inspect}"
    ))
  end
end

begin
  exit if ARGV[0] == "exit-immediately"
  while true
    ios = select([server, STDIN])[0]
    if ios.include?(server)
      client = server.accept
      begin
        process_client_command(server, client, client.readline)
      ensure
        client.close
      end
    end
    if ios.include?(STDIN)
      begin
        STDIN.readline
      rescue EOFError
        exit
      end
    end
  end
ensure
  File.unlink(socket_filename) rescue nil
end
