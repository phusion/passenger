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

### OXT library tests ###

TEST_OXT_CFLAGS = "-I../../ext -I../support #{TEST_COMMON_CFLAGS}"
TEST_OXT_LDFLAGS = "#{TEST_BOOST_OXT_LIBRARY} #{PlatformInfo.portability_ldflags} #{EXTRA_LDFLAGS}"
TEST_OXT_OBJECTS = {
	'oxt_test_main.o' => %w(oxt_test_main.cpp),
	'backtrace_test.o' => %w(backtrace_test.cpp counter.hpp),
	'spin_lock_test.o' => %w(spin_lock_test.cpp),
	'dynamic_thread_group_test.o' => %w(dynamic_thread_group_test.cpp counter.hpp),
	'syscall_interruption_test.o' => %w(syscall_interruption_test.cpp)
}

desc "Run unit tests for the OXT library"
task 'test:oxt' => 'test/oxt/oxt_test_main' do
	sh "cd test && ./oxt/oxt_test_main"
end

# Define task for test/oxt/oxt_test_main.
oxt_test_main_dependencies = TEST_OXT_OBJECTS.keys.map do |object|
	"test/oxt/#{object}"
end
oxt_test_main_dependencies << TEST_BOOST_OXT_LIBRARY
file 'test/oxt/oxt_test_main' => oxt_test_main_dependencies do
	objects = TEST_OXT_OBJECTS.keys.map{ |x| "test/oxt/#{x}" }.join(' ')
	create_executable("test/oxt/oxt_test_main", objects, TEST_OXT_LDFLAGS)
end

# Define tasks for each OXT test source file.
TEST_OXT_OBJECTS.each_pair do |target, sources|
	file "test/oxt/#{target}" => sources.map{ |x| "test/oxt/#{x}" } do
		Dir.chdir('test/oxt') do
			puts "### In test/oxt:"
			compile_cxx sources[0], TEST_OXT_CFLAGS
		end
	end
end
