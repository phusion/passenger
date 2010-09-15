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

### Ruby components tests ###

desc "Run unit tests for the Ruby libraries"
task 'test:ruby' => [:native_support, 'agents/PassengerLoggingAgent'] do
	if PlatformInfo.rspec.nil?
		abort "RSpec is not installed for Ruby interpreter '#{PlatformInfo.ruby_command}'. Please install it."
	else
		Dir.chdir("test") do
			ruby "#{PlatformInfo.rspec} -c -f s ruby/*_spec.rb ruby/*/*_spec.rb"
		end
	end
end

desc "Run coverage tests for the Ruby libraries"
task 'test:rcov' => :native_support do
	if PlatformInfo.rspec.nil?
		abort "RSpec is not installed for Ruby interpreter '#{PlatformInfo.ruby_command}'. Please install it."
	else
		Dir.chdir("test") do
			sh "rcov", "--exclude",
				"lib\/spec,\/spec$,_spec\.rb$,support\/,platform_info,integration_tests",
				PlatformInfo.rspec, "--", "-c", "-f", "s",
				*Dir["ruby/*.rb", "ruby/*/*.rb", "integration_tests.rb"]
		end
	end
end