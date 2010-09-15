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
