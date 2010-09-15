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

### Integration tests ###

desc "Run all integration tests"
task 'test:integration' => ['test:integration:apache2', 'test:integration:nginx'] do
end

desc "Run Apache 2 integration tests"
task 'test:integration:apache2' => [:apache2, :native_support] do
	if PlatformInfo.rspec.nil?
		abort "RSpec is not installed for Ruby interpreter '#{PlatformInfo.ruby_command}'. Please install it."
	else
		Dir.chdir("test") do
			ruby "#{PlatformInfo.rspec} -c -f s integration_tests/apache2_tests.rb"
		end
	end
end

desc "Run Nginx integration tests"
task 'test:integration:nginx' => :nginx do
	if PlatformInfo.rspec.nil?
		abort "RSpec is not installed for Ruby interpreter '#{PlatformInfo.ruby_command}'. Please install it."
	else
		Dir.chdir("test") do
			ruby "#{PlatformInfo.rspec} -c -f s integration_tests/nginx_tests.rb"
		end
	end
end

desc "Run the 'restart' integration test infinitely, and abort if/when it fails"
task 'test:restart' => [:apache2, :native_support] do
	Dir.chdir("test") do
		color_code_start = "\e[33m\e[44m\e[1m"
		color_code_end = "\e[0m"
		i = 1
		while true do
			puts "#{color_code_start}Test run #{i} (press Ctrl-C multiple times to abort)#{color_code_end}"
			sh "spec -c -f s integration_tests/apache2.rb -e 'mod_passenger running in Apache 2 : MyCook(tm) beta running on root URI should support restarting via restart.txt'"
			i += 1
		end
	end
end