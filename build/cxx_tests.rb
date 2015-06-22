#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2015 Phusion
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

### C++ components tests ###

TEST_CXX_CFLAGS = "-Iext -Iext/common " <<
  "#{EXTRA_PRE_CXXFLAGS} " <<
  "#{LIBEV_CFLAGS} #{LIBUV_CFLAGS} #{PlatformInfo.curl_flags} -Itest/cxx -Itest/support " <<
  "#{TEST_COMMON_CFLAGS}"
TEST_CXX_CFLAGS << " #{PlatformInfo.adress_sanitizer_flag}" if USE_ASAN

def test_cxx_ldflags
  @test_cxx_ldflags ||= begin
    result = "#{EXTRA_PRE_CXX_LDFLAGS} " <<
      "#{TEST_COMMON_LIBRARY.link_objects_as_string} " <<
      "#{TEST_BOOST_OXT_LIBRARY} #{libev_libs} #{libuv_libs} " <<
      "#{PlatformInfo.curl_libs} " <<
      "#{PlatformInfo.zlib_libs} " <<
      "#{PlatformInfo.portability_cxx_ldflags}"
    result << " #{PlatformInfo.dmalloc_ldflags}" if USE_DMALLOC
    result << " #{PlatformInfo.adress_sanitizer_flag}" if USE_ASAN
    result << " #{EXTRA_CXX_LDFLAGS}"
    result.strip!
    result
  end
end

TEST_CXX_OBJECTS = {
  'test/cxx/CxxTestMain.o' => %w(
    test/cxx/CxxTestMain.cpp),
  'test/cxx/TestSupport.o' => %w(
    test/cxx/TestSupport.cpp
    test/cxx/TestSupport.h
    ext/common/SafeLibev.h
    ext/common/BackgroundEventLoop.cpp
    ext/common/Exceptions.h
    ext/common/Utils.h),
  'test/cxx/ApplicationPool2/OptionsTest.o' => %w(
    test/cxx/ApplicationPool2/OptionsTest.cpp
    ext/common/ApplicationPool2/Options.h),
  'test/cxx/ApplicationPool2/ProcessTest.o' => %w(
    test/cxx/ApplicationPool2/ProcessTest.cpp
    ext/common/ApplicationPool2/Process.h
    ext/common/ApplicationPool2/Socket.h
    ext/common/ApplicationPool2/Session.h
    ext/common/SpawningKit/Spawner.h
    ext/common/SpawningKit/DummySpawner.h),
  'test/cxx/ApplicationPool2/PoolTest.o' => %w(
    test/cxx/ApplicationPool2/PoolTest.cpp
    ext/common/ApplicationPool2/Pool.h
    ext/common/ApplicationPool2/Group.h
    ext/common/ApplicationPool2/BasicGroupInfo.h
    ext/common/ApplicationPool2/BasicProcessInfo.h
    ext/common/ApplicationPool2/Context.h
    ext/common/ApplicationPool2/Process.h
    ext/common/ApplicationPool2/Socket.h
    ext/common/ApplicationPool2/Options.h
    ext/common/SpawningKit/Spawner.h
    ext/common/SpawningKit/Factory.h
    ext/common/SpawningKit/SmartSpawner.h
    ext/common/SpawningKit/DirectSpawner.h
    ext/common/SpawningKit/DummySpawner.h),
  'test/cxx/SpawningKit/DirectSpawnerTest.o' => %w(
    test/cxx/SpawningKit/DirectSpawnerTest.cpp
    test/cxx/SpawningKit/SpawnerTestCases.cpp
    ext/common/ApplicationPool2/Options.h
    ext/common/SpawningKit/Config.h
    ext/common/SpawningKit/Options.h
    ext/common/SpawningKit/Result.h
    ext/common/SpawningKit/Spawner.h
    ext/common/SpawningKit/DirectSpawner.h),
  'test/cxx/SpawningKit/SmartSpawnerTest.o' => %w(
    test/cxx/SpawningKit/SmartSpawnerTest.cpp
    test/cxx/SpawningKit/SpawnerTestCases.cpp
    ext/common/ApplicationPool2/Options.h
    ext/common/SpawningKit/Config.h
    ext/common/SpawningKit/Options.h
    ext/common/SpawningKit/Result.h
    ext/common/SpawningKit/Spawner.h
    ext/common/SpawningKit/SmartSpawner.h),
  'test/cxx/MemoryKit/MbufTest.o' => %w(
    test/cxx/MemoryKit/MbufTest.cpp
    ext/common/MemoryKit/mbuf.h),
  'test/cxx/ServerKit/ChannelTest.o' => %w(
    test/cxx/ServerKit/ChannelTest.cpp
    ext/common/ServerKit/Channel.h
    ext/common/ServerKit/Context.h),
  'test/cxx/ServerKit/FileBufferedChannelTest.o' => %w(
    test/cxx/ServerKit/FileBufferedChannelTest.cpp
    ext/common/ServerKit/FileBufferedChannel.h
    ext/common/ServerKit/Context.h),
  'test/cxx/ServerKit/HeaderTableTest.o' => %w(
    test/cxx/ServerKit/HeaderTableTest.cpp
    ext/common/ServerKit/HeaderTable.h
    ext/common/DataStructures/LString.h),
  'test/cxx/ServerKit/ServerTest.o' => %w(
    test/cxx/ServerKit/ServerTest.cpp
    ext/common/ServerKit/Server.h
    ext/common/ServerKit/Client.h
    ext/common/ServerKit/Channel.h
    ext/common/ServerKit/FdSourceChannel.h
    ext/common/ServerKit/FileBufferedChannel.h
    ext/common/ServerKit/FileBufferedFdSinkChannel.h
    ext/common/ServerKit/Context.h),
  'test/cxx/ServerKit/HttpServerTest.o' => %w(
    test/cxx/ServerKit/HttpServerTest.cpp
    ext/common/ServerKit/Server.h
    ext/common/ServerKit/Client.h
    ext/common/ServerKit/Channel.h
    ext/common/ServerKit/FdSourceChannel.h
    ext/common/ServerKit/FileBufferedChannel.h
    ext/common/ServerKit/FileBufferedFdSinkChannel.h
    ext/common/ServerKit/Context.h
    ext/common/ServerKit/HttpServer.h
    ext/common/ServerKit/HttpClient.h
    ext/common/ServerKit/HttpRequest.h
    ext/common/ServerKit/HttpHeaderParser.h
    ext/common/ServerKit/HttpChunkedBodyParser.h),
  'test/cxx/ServerKit/CookieUtilsTest.o' => %w(
    test/cxx/ServerKit/CookieUtilsTest.cpp
    ext/common/ServerKit/CookieUtils.h
    ext/common/DataStructures/LString.h),
  'test/cxx/DataStructures/LStringTest.o' => %w(
    test/cxx/DataStructures/LStringTest.cpp
    ext/common/DataStructures/LString.h),
  'test/cxx/DataStructures/StringKeyTableTest.o' => %w(
    test/cxx/DataStructures/StringKeyTableTest.cpp
    ext/common/DataStructures/StringKeyTable.h),
  'test/cxx/MessageReadersWritersTest.o' => %w(
    test/cxx/MessageReadersWritersTest.cpp
    ext/common/MessageReadersWriters.h
    ext/common/Exceptions.h
    ext/common/StaticString.h
    ext/common/Utils/MemZeroGuard.h),
  'test/cxx/StaticStringTest.o' => %w(
    test/cxx/StaticStringTest.cpp
    ext/common/StaticString.h),
  'test/cxx/DechunkerTest.o' => %w(
    test/cxx/DechunkerTest.cpp
    ext/common/Utils/Dechunker.h),
  'test/cxx/ResponseCacheTest.o' => %w(
    test/cxx/ResponseCacheTest.cpp
    ext/common/agent/Core/ResponseCache.h
    ext/common/agent/Core/RequestHandler/Request.h
    ext/common/agent/Core/RequestHandler/AppResponse.h
    ext/common/ServerKit/HttpRequest.h),
  'test/cxx/UnionStationTest.o' => %w(
    test/cxx/UnionStationTest.cpp
    ext/common/agent/UstRouter/LoggingServer.h
    ext/common/agent/UstRouter/RemoteSender.h
    ext/common/agent/UstRouter/DataStoreId.h
    ext/common/agent/UstRouter/FilterSupport.h
    ext/common/UnionStation/Connection.h
    ext/common/UnionStation/Core.h
    ext/common/UnionStation/Transaction.h
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
    ext/common/Logging.h
    ext/common/Account.h
    ext/common/AccountsDatabase.h
    ext/common/MessageServer.h),
  # 'test/cxx/RequestHandlerTest.o' => %w(
  #   test/cxx/RequestHandlerTest.cpp
  #   ext/common/agent/Core/RequestHandler.h
  #   ext/common/agent/Core/FileBackedPipe.h
  #   ext/common/agent/Core/ScgiRequestParser.h
  #   ext/common/agent/Core/AgentOptions.h
  #   ext/common/UnionStation/Connection.h
  #   ext/common/UnionStation/Core.h
  #   ext/common/UnionStation/Transaction.h
  #   ext/common/UnionStation/ScopeLog.h
  #   ext/common/ApplicationPool2/Pool.h
  #   ext/common/ApplicationPool2/SuperGroup.h
  #   ext/common/ApplicationPool2/Group.h
  #   ext/common/ApplicationPool2/Process.h
  #   ext/common/ApplicationPool2/Options.h
  #   ext/common/ApplicationPool2/Spawner.h
  #   ext/common/ApplicationPool2/SpawnerFactory.h
  #   ext/common/ApplicationPool2/SmartSpawner.h
  #   ext/common/ApplicationPool2/DirectSpawner.h
  #   ext/common/ApplicationPool2/DummySpawner.h),
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
  'test/cxx/FilterSupportTest.o' => %w(
    test/cxx/FilterSupportTest.cpp
    ext/common/agent/UstRouter/FilterSupport.h),
  'test/cxx/CachedFileStatTest.o' => %w(
    test/cxx/CachedFileStatTest.cpp
    ext/common/Utils/CachedFileStat.hpp
    ext/common/Utils/CachedFileStat.cpp),
  'test/cxx/BufferedIOTest.o' => %w(
    test/cxx/BufferedIOTest.cpp
    ext/common/Utils/BufferedIO.h
    ext/common/Utils/Timer.h),
  'test/cxx/MessageIOTest.o' => %w(
    test/cxx/MessageIOTest.cpp
    ext/common/Utils/MessageIO.h
    ext/common/Utils/IOUtils.h),
  'test/cxx/MessagePassingTest.o' => %w(
    test/cxx/MessagePassingTest.cpp
    ext/common/Utils/MessagePassing.h),
  'test/cxx/VariantMapTest.o' => %w(
    test/cxx/VariantMapTest.cpp
    ext/common/Utils/VariantMap.h),
  'test/cxx/StringMapTest.o' => %w(
    test/cxx/StringMapTest.cpp
    ext/common/Utils/StringMap.h
    ext/common/Utils/HashMap.h),
  'test/cxx/ProcessMetricsCollectorTest.o' => %w(
    test/cxx/ProcessMetricsCollectorTest.cpp
    ext/common/Utils/ProcessMetricsCollector.h),
  'test/cxx/DateParsingTest.o' => %w(
    test/cxx/DateParsingTest.cpp
    ext/common/Utils/DateParsing.h),
  'test/cxx/UtilsTest.o' => %w(
    test/cxx/UtilsTest.cpp
    ext/common/Utils.h),
  'test/cxx/Utils/StrIntUtilsTest.o' => %w(
    test/cxx/Utils/StrIntUtilsTest.cpp
    ext/common/Utils/StrIntUtils.cpp),
  'test/cxx/IOUtilsTest.o' => %w(
    test/cxx/IOUtilsTest.cpp
    ext/common/Utils/IOUtils.h),
  'test/cxx/TemplateTest.o' => %w(
    test/cxx/TemplateTest.cpp
    ext/common/Utils/Template.h)
}

dependencies = [
  'test/cxx/CxxTestMain',
  'test/support/allocate_memory',
  NATIVE_SUPPORT_TARGET,
  AGENT_OUTPUT_DIR + AGENT_EXE
].compact
desc "Run unit tests for the Apache 2 and Nginx C++ components"
task 'test:cxx' => dependencies do
  args = ENV['GROUPS'].to_s.split(";").map{ |name| "-g #{name}" }
  command = "./cxx/CxxTestMain #{args.join(' ')}".strip
  if boolean_option('GDB')
    command = "gdb --args #{command}"
  elsif boolean_option('VALGRIND')
    command = "valgrind --dsymutil=yes --db-attach=yes --child-silent-after-fork=yes #{command}"
  end
  if boolean_option('SUDO')
    command = "#{PlatformInfo.ruby_sudo_command} #{command}"
  end
  if boolean_option('REPEAT')
    if boolean_option('GDB')
      abort "You cannot set both REPEAT=1 and GDB=1."
    end
    sh "cd test && while #{command}; do echo -------------------------------------------; done"
  elsif boolean_option('REPEAT_FOREVER')
    if boolean_option('GDB')
      abort "You cannot set both REPEAT_FOREVER=1 and GDB=1."
    end
    sh "cd test && while true; do #{command}; echo -------------------------------------------; done"
  else
    sh "cd test && exec #{command}"
  end
end

dependencies = [
  TEST_CXX_OBJECTS.keys,
  LIBEV_TARGET,
  LIBUV_TARGET,
  TEST_BOOST_OXT_LIBRARY,
  TEST_COMMON_LIBRARY.link_objects,
  'ext/common/Constants.h'
].flatten.compact
file 'test/cxx/CxxTestMain' => dependencies.flatten do
  objects = TEST_CXX_OBJECTS.keys.join(' ')
  create_executable("test/cxx/CxxTestMain", objects, test_cxx_ldflags)
end

deps = [
  'test/cxx/TestSupport.h',
  'test/tut/tut.h',
  'ext/oxt/thread.hpp',
  'ext/oxt/tracable_exception.hpp',
  'ext/common/Constants.h',
  'ext/common/Exceptions.h',
  'ext/common/InstanceDirectory.h',
  'ext/common/Utils.h',
  'ext/common/Utils/SystemTime.h'
]
file 'test/cxx/TestSupport.h.gch' => deps do
  compile_cxx 'test/cxx/TestSupport.h', "-x c++-header -o test/cxx/TestSupport.h.gch #{TEST_CXX_CFLAGS}"
end

TEST_CXX_OBJECTS.each_pair do |target, sources|
  extra_deps = ['test/cxx/TestSupport.h', 'test/cxx/TestSupport.h.gch', 'ext/common/Constants.h']
  file(target => sources + extra_deps) do
    # To use precompiled headers in Clang, we must -include them on them command line.
    compile_cxx sources[0], "-o #{target} -include test/cxx/TestSupport.h #{TEST_CXX_CFLAGS}"
  end
end
