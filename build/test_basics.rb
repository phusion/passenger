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

TEST_BOOST_OXT_LIBRARY = LIBBOOST_OXT
TEST_COMMON_LIBRARY    = LIBCOMMON

TEST_COMMON_CFLAGS = "-DTESTING_APPLICATION_POOL " <<
	"#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"

desc "Run all unit tests and integration tests"
task :test => ['test:oxt', 'test:cxx', 'test:ruby', 'test:integration']

desc "Clean all compiled test files"
task 'test:clean' do
	sh("rm -rf test/oxt/oxt_test_main test/oxt/*.o test/cxx/CxxTestMain test/cxx/*.o")
	sh("rm -f test/support/allocate_memory")
end

task :clean => 'test:clean'

file 'test/support/allocate_memory' => 'test/support/allocate_memory.c' do
	create_c_executable('test/support/allocate_memory', 'test/support/allocate_memory.c')
end
