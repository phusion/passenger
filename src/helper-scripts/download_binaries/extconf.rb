#!/usr/bin/env ruby
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2018 Phusion Holding B.V.
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

# This script is called during 'gem install'. Its role is to download
# Phusion Passenger binaries.

# Create a dummy Makefile to prevent 'gem install' from borking out.
File.open("Makefile", "w") do |f|
  f.puts "all:"
  f.puts "\ttrue"
  f.puts "install:"
  f.puts "\ttrue"
  f.puts "clean:"
  f.puts "\ttrue"
end

# Don't do anything on Windows. We don't support Windows but exiting now
# will at least prevent the gem from being not installable on Windows.
exit if RUBY_PLATFORM =~ /mswin/i || RUBY_PLATFORM =~ /win32/i || RUBY_PLATFORM =~ /mingw/

source_root = File.expand_path("../../..", File.dirname(__FILE__))
$LOAD_PATH.unshift("#{source_root}/src/ruby_supportlib")
require 'phusion_passenger'
PhusionPassenger.locate_directories
require 'fileutils'

if PhusionPassenger.custom_packaged?
  puts "Binary downloading is only available when originally packaged. Stopping."
  exit 0
end
if !PhusionPassenger.installed_from_release_package?
  puts "This Phusion Passenger is not installed from an official release package. Stopping."
  exit 0
end

# Create download directory and do some preparation
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
cxx_compat_id = PhusionPassenger::PlatformInfo.cxx_binary_compatibility_id
ruby_compat_id = PhusionPassenger::PlatformInfo.ruby_extension_binary_compatibility_id

ABORT_ON_ERROR = ARGV[0] == "--abort-on-error"
if url_root = ENV['BINARIES_URL_ROOT']
  SITES = [{ :url => url_root }]
else
  SITES = PhusionPassenger.binaries_sites
end

FileUtils.mkdir_p(PhusionPassenger.download_cache_dir)
Dir.chdir(PhusionPassenger.download_cache_dir)

# Initiate downloads
require 'phusion_passenger/utils/download'
require 'logger'

def download(name, options = {})
  if File.exist?(name)
    puts "#{Dir.pwd}/#{name} already exists"
    return true
  end

  logger = Logger.new(STDOUT)
  logger.level = Logger::WARN
  logger.formatter = proc { |severity, datetime, progname, msg| "*** #{msg}\n" }

  SITES.each do |site|
    if really_download(site, name, logger, options)
      return true
    end
  end
  abort "Cannot download #{name}, aborting" if ABORT_ON_ERROR
  return false
end

def really_download(site, name, logger, options)
  if site[:url].include?('{{VERSION}}')
    url = site[:url].gsub('{{VERSION}}', PhusionPassenger::VERSION_STRING) + "/#{name}"
  else
    url = "#{site[:url]}/#{PhusionPassenger::VERSION_STRING}/#{name}"
  end
  puts "Attempting to download #{url} into #{Dir.pwd}"
  File.unlink("#{name}.tmp") rescue nil

  options = {
    :cacert => site[:cert],
    :logger => logger
  }.merge(options)
  result = PhusionPassenger::Utils::Download.download(url, "#{name}.tmp", options)
  if result
    File.rename("#{name}.tmp", name)
  else
    File.unlink("#{name}.tmp") rescue nil
  end
  return result
end

download "rubyext-#{ruby_compat_id}.tar.gz", :total_timeout => 10
download "nginx-#{PhusionPassenger::PREFERRED_NGINX_VERSION}-#{cxx_compat_id}.tar.gz", :total_timeout => 120
download "agent-#{cxx_compat_id}.tar.gz", :total_timeout => 900
