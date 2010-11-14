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

### C++ components tests ###

TEST_CXX_CFLAGS = "-Iext -Iext/common -Iext/nginx " <<
	"#{LIBEV_CFLAGS} #{PlatformInfo.curl_flags} -Itest/support " <<
	"#{TEST_COMMON_CFLAGS}"
TEST_CXX_LDFLAGS = "#{TEST_COMMON_LIBRARY} #{TEST_BOOST_OXT_LIBRARY} #{LIBEV_LIBS} " <<
	"#{PlatformInfo.curl_libs} " <<
	"#{PlatformInfo.portability_ldflags} #{EXTRA_LDFLAGS}"
TEST_CXX_OBJECTS = {
	'test/cxx/CxxTestMain.o' => %w(
		test/cxx/CxxTestMain.cpp),
	'test/cxx/TestSupport.o' => %w(
		test/cxx/TestSupport.cpp
		test/cxx/TestSupport.h),
	'test/cxx/MessageChannelTest.o' => %w(
		test/cxx/MessageChannelTest.cpp
		ext/common/MessageChannel.h
		ext/common/Exceptions.h
		ext/common/Utils.h
		ext/common/Utils/Timer.h),
	'test/cxx/MessageReadersWritersTest.o' => %w(
		test/cxx/MessageReadersWritersTest.cpp
		ext/common/MessageReadersWriters.h
		ext/common/Exceptions.h
		ext/common/StaticString.h
		ext/common/Utils/MemZeroGuard.h),
	'test/cxx/SpawnManagerTest.o' => %w(
		test/cxx/SpawnManagerTest.cpp
		ext/common/SpawnManager.h
		ext/common/AbstractSpawnManager.h
		ext/common/PoolOptions.h
		ext/common/Logging.h
		ext/common/StringListCreator.h
		ext/common/Process.h
		ext/common/AccountsDatabase.h
		ext/common/Account.h
		ext/common/Session.h
		ext/common/Constants.h
		ext/common/MessageChannel.h),
	'test/cxx/ApplicationPool_ServerTest.o' => %w(
		test/cxx/ApplicationPool_ServerTest.cpp
		ext/common/ApplicationPool/Interface.h
		ext/common/ApplicationPool/Server.h
		ext/common/ApplicationPool/Client.h
		ext/common/ApplicationPool/Pool.h
		ext/common/Account.h
		ext/common/AccountsDatabase.h
		ext/common/MessageServer.h
		ext/common/Session.h
		ext/common/PoolOptions.h
		ext/common/Logging.h
		ext/common/StringListCreator.h
		ext/common/MessageChannel.h
		ext/common/Utils/ProcessMetricsCollector.h),
	'test/cxx/ApplicationPool_Server_PoolTest.o' => %w(
		test/cxx/ApplicationPool_Server_PoolTest.cpp
		test/cxx/ApplicationPool_PoolTestCases.cpp
		ext/common/ApplicationPool/Interface.h
		ext/common/ApplicationPool/Server.h
		ext/common/ApplicationPool/Client.h
		ext/common/ApplicationPool/Pool.h
		ext/common/AbstractSpawnManager.h
		ext/common/Account.h
		ext/common/AccountsDatabase.h
		ext/common/MessageServer.h
		ext/common/SpawnManager.h
		ext/common/PoolOptions.h
		ext/common/Logging.h
		ext/common/StringListCreator.h
		ext/common/Process.h
		ext/common/Session.h
		ext/common/MessageChannel.h
		ext/common/Utils/ProcessMetricsCollector.h),
	'test/cxx/ApplicationPool_PoolTest.o' => %w(
		test/cxx/ApplicationPool_PoolTest.cpp
		test/cxx/ApplicationPool_PoolTestCases.cpp
		ext/common/ApplicationPool/Interface.h
		ext/common/ApplicationPool/Pool.h
		ext/common/AbstractSpawnManager.h
		ext/common/SpawnManager.h
		ext/common/PoolOptions.h
		ext/common/Logging.h
		ext/common/StringListCreator.h
		ext/common/Utils/FileChangeChecker.h
		ext/common/Utils/CachedFileStat.hpp
		ext/common/Process.h
		ext/common/Session.h),
	'test/cxx/PoolOptionsTest.o' => %w(
		test/cxx/PoolOptionsTest.cpp
		ext/common/PoolOptions.h
		ext/common/Session.h
		ext/common/Logging.h
		ext/common/StringListCreator.h),
	'test/cxx/StaticStringTest.o' => %w(
		test/cxx/StaticStringTest.cpp
		ext/common/StaticString.h),
	'test/cxx/Base64Test.o' => %w(
		test/cxx/Base64Test.cpp
		ext/common/Utils/Base64.h
		ext/common/Utils/Base64.cpp),
	'test/cxx/ScgiRequestParserTest.o' => %w(
		test/cxx/ScgiRequestParserTest.cpp
		ext/nginx/ScgiRequestParser.h
		ext/common/StaticString.h),
	'test/cxx/HttpStatusExtractorTest.o' => %w(
		test/cxx/HttpStatusExtractorTest.cpp
		ext/nginx/HttpStatusExtractor.h),
	'test/cxx/LoggingTest.o' => %w(
		test/cxx/LoggingTest.cpp
		ext/common/LoggingAgent/LoggingServer.h
		ext/common/LoggingAgent/RemoteSender.h
		ext/common/LoggingAgent/ChangeNotifier.h
		ext/common/LoggingAgent/DataStoreId.h
		ext/common/Logging.h
		ext/common/Utils.h
		ext/common/EventedServer.h
		ext/common/EventedClient.h
		ext/common/EventedMessageServer.h
		ext/common/MessageReadersWriters.h
		ext/common/MessageClient.h),
	'test/cxx/EventedClientTest.o' => %w(
		test/cxx/EventedClientTest.cpp
		ext/common/EventedClient.h),
	'test/cxx/MessageServerTest.o' => %w(
		test/cxx/MessageServerTest.cpp
		ext/common/ApplicationPool/Client.h
		ext/common/ApplicationPool/Pool.h
		ext/common/PoolOptions.h
		ext/common/SpawnManager.h
		ext/common/Session.h
		ext/common/Logging.h
		ext/common/Account.h
		ext/common/AccountsDatabase.h
		ext/common/Session.h
		ext/common/MessageServer.h
		ext/common/MessageChannel.h),
	'test/cxx/ServerInstanceDir.o' => %w(
		test/cxx/ServerInstanceDirTest.cpp
		ext/common/ServerInstanceDir.h
		ext/common/Utils.h),
	'test/cxx/FileChangeCheckerTest.o' => %w(
		test/cxx/FileChangeCheckerTest.cpp
		ext/common/Utils/FileChangeChecker.h
		ext/common/Utils/CachedFileStat.hpp),
	'test/cxx/FileDescriptorTest.o' => %w(
		test/cxx/FileDescriptorTest.cpp
		ext/common/FileDescriptor.h),
	'test/cxx/SystemTimeTest.o' => %w(
		test/cxx/SystemTimeTest.cpp
		ext/common/Utils/SystemTime.h
		ext/common/Utils/SystemTime.cpp),
	'test/cxx/CachedFileStatTest.o' => %w(
		test/cxx/CachedFileStatTest.cpp
		ext/common/Utils/CachedFileStat.hpp
		ext/common/Utils/CachedFileStat.cpp),
	'test/cxx/BufferedIOTest.o' => %w(
		test/cxx/BufferedIOTest.cpp
		ext/common/Utils/BufferedIO.h
		ext/common/Utils/Timer.h),
	'test/cxx/VariantMapTest.o' => %w(
		test/cxx/VariantMapTest.cpp
		ext/common/MessageChannel.h
		ext/common/Utils/VariantMap.h),
	'test/cxx/ProcessMetricsCollectorTest.o' => %w(
		test/cxx/ProcessMetricsCollectorTest.cpp
		ext/common/Utils/ProcessMetricsCollector.h),
	'test/cxx/UtilsTest.o' => %w(
		test/cxx/UtilsTest.cpp
		ext/common/Utils.h),
	'test/cxx/IOUtilsTest.o' => %w(
		test/cxx/IOUtilsTest.cpp
		ext/common/Utils/IOUtils.h)
}

desc "Run unit tests for the Apache 2 and Nginx C++ components"
task 'test:cxx' => ['test/cxx/CxxTestMain', 'test/support/allocate_memory', :native_support] do
        if ENV['GROUPS'].to_s.empty?
	        sh "cd test && ./cxx/CxxTestMain"
        else
                args = ENV['GROUPS'].split(",").map{ |name| "-g #{name}" }
                sh "cd test && ./cxx/CxxTestMain #{args.join(' ')}"
        end
end

cxx_tests_dependencies = [TEST_CXX_OBJECTS.keys, :libev,
	TEST_BOOST_OXT_LIBRARY, TEST_COMMON_LIBRARY]
file 'test/cxx/CxxTestMain' => cxx_tests_dependencies.flatten do
	objects = TEST_CXX_OBJECTS.keys.join(' ')
	create_executable("test/cxx/CxxTestMain", objects, TEST_CXX_LDFLAGS)
end

TEST_CXX_OBJECTS.each_pair do |target, sources|
	file(target => sources + ['test/cxx/TestSupport.h']) do
		compile_cxx sources[0], "-o #{target} #{TEST_CXX_CFLAGS}"
	end
end