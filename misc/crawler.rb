#!/usr/bin/env ruby
require 'rubygems'
require 'hawler'
require 'hawleroptions'

CONCURRENT_USERS = 24

def show(uri, referer, response)
	puts uri
end

if ARGV.empty?
	puts "Usage: crawler.rb <hostname>"
	puts "Crawls the given website until you press Ctrl-C."
	exit 1
else
	site = ARGV[0]
	pids = []
	CONCURRENT_USERS.times do
		pid = fork do
			begin
				Process.setsid
				while true
					crawler = Hawler.new(site, method(:show))
					crawler.recurse = true
					crawler.depth = 20
					crawler.start
				end
			rescue Exception => e
				puts e
				exit!
			end
		end
		pids << pid
	end
	begin
		while !pids.empty? do
			Process.waitpid(pids.first) rescue nil
			pids.shift
		end
	rescue Interrupt
		puts "Shutting down..."
		pids.each do |pid|
			Process.kill('SIGKILL', pid)
		end
		while !pids.empty? do
			Process.waitpid(pids.first) rescue nil
			pids.shift
		end
	end
end

