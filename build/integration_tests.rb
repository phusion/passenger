#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
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