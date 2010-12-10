# kate: syntax ruby

#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008, 2009  Phusion
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

source_root = File.expand_path(File.dirname(__FILE__))
$LOAD_PATH.unshift(source_root)
$LOAD_PATH.unshift("#{source_root}/lib")

require "#{source_root}/config" if File.exist?("#{source_root}/config.rb")
require 'build/basics'
require 'build/config'
require 'build/common_library'
require 'build/ruby_extension'
require 'build/agents'
require 'build/apache2'
require 'build/nginx'
require 'build/documentation'
require 'build/packaging'
require 'build/test_basics'
require 'build/oxt_tests'
require 'build/cxx_tests'
require 'build/ruby_tests'
require 'build/integration_tests'
require 'build/misc'
require 'build/rpm'


#### Default tasks

task :default do
	abort "Please type one of:\n" +
		"  rake apache2\n" +
		"  rake nginx"
end

desc "Remove compiled files"
task :clean do
	sh "rm -rf build/cache"
end

desc "Remove all generated files"
task :clobber
