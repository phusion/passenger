#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2013 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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

### Ruby components tests ###

dependencies = [NATIVE_SUPPORT_TARGET, AGENT_OUTPUT_DIR + 'PassengerLoggingAgent'].compact
desc "Run unit tests for the Ruby libraries"
task 'test:ruby' => dependencies do
	if PlatformInfo.rspec.nil?
		abort "RSpec is not installed for Ruby interpreter '#{PlatformInfo.ruby_command}'. Please install it."
	else
		if maybe_grep = string_option('E')
			require 'shellwords'
			maybe_grep = "-e #{Shellwords.escape(maybe_grep)}"
		end
		command = "#{PlatformInfo.rspec} -c -f s -P 'dont-autoload-anything' #{maybe_grep} ruby/*_spec.rb ruby/*/*_spec.rb"
		sh "cd test && exec #{command}"
	end
end

dependencies = [NATIVE_SUPPORT_TARGET].compact
desc "Run coverage tests for the Ruby libraries"
task 'test:rcov' => dependencies do
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
