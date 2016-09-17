#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2016 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
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

TEST_CXX_TARGET = "#{TEST_OUTPUT_DIR}cxx/main"
TEST_CXX_OBJECTS = {
  "#{TEST_OUTPUT_DIR}cxx/CxxTestMain.o" =>
    "test/cxx/CxxTestMain.cpp",
  "#{TEST_OUTPUT_DIR}cxx/TestSupport.o" =>
    "test/cxx/TestSupport.cpp",

  "#{TEST_OUTPUT_DIR}cxx/Core/ApplicationPool/OptionsTest.o" =>
    "test/cxx/Core/ApplicationPool/OptionsTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/Core/ApplicationPool/ProcessTest.o" =>
    "test/cxx/Core/ApplicationPool/ProcessTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/Core/ApplicationPool/PoolTest.o" =>
    "test/cxx/Core/ApplicationPool/PoolTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/Core/SpawningKit/DirectSpawnerTest.o" =>
    "test/cxx/Core/SpawningKit/DirectSpawnerTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/Core/SpawningKit/SmartSpawnerTest.o" =>
    "test/cxx/Core/SpawningKit/SmartSpawnerTest.cpp",

  "#{TEST_OUTPUT_DIR}cxx/Core/UnionStationTest.o" =>
    "test/cxx/Core/UnionStationTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/Core/ResponseCacheTest.o" =>
    "test/cxx/Core/ResponseCacheTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/Core/SecurityUpdateCheckerTest.o" =>
      "test/cxx/Core/SecurityUpdateCheckerTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/Core/ControllerTest.o" =>
    "test/cxx/Core/ControllerTest.cpp",

  "#{TEST_OUTPUT_DIR}cxx/UstRouter/TransactionTest.o" =>
    "test/cxx/UstRouter/TransactionTest.cpp",

  "#{TEST_OUTPUT_DIR}cxx/ServerKit/ChannelTest.o" =>
    "test/cxx/ServerKit/ChannelTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/ServerKit/FileBufferedChannelTest.o" =>
    "test/cxx/ServerKit/FileBufferedChannelTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/ServerKit/HeaderTableTest.o" =>
    "test/cxx/ServerKit/HeaderTableTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/ServerKit/ServerTest.o" =>
    "test/cxx/ServerKit/ServerTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/ServerKit/HttpServerTest.o" =>
    "test/cxx/ServerKit/HttpServerTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/ServerKit/CookieUtilsTest.o" =>
    "test/cxx/ServerKit/CookieUtilsTest.cpp",

  "#{TEST_OUTPUT_DIR}cxx/MemoryKit/MbufTest.o" =>
    "test/cxx/MemoryKit/MbufTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/MemoryKit/PallocTest.o" =>
    "test/cxx/MemoryKit/PallocTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/DataStructures/LStringTest.o" =>
    "test/cxx/DataStructures/LStringTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/DataStructures/StringKeyTableTest.o" =>
    "test/cxx/DataStructures/StringKeyTableTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/MessageReadersWritersTest.o" =>
    "test/cxx/MessageReadersWritersTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/StaticStringTest.o" =>
    "test/cxx/StaticStringTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/FileChangeCheckerTest.o" =>
    "test/cxx/FileChangeCheckerTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/FileDescriptorTest.o" =>
    "test/cxx/FileDescriptorTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/SystemTimeTest.o" =>
    "test/cxx/SystemTimeTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/FilterSupportTest.o" =>
    "test/cxx/FilterSupportTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/CachedFileStatTest.o" =>
    "test/cxx/CachedFileStatTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/BufferedIOTest.o" =>
    "test/cxx/BufferedIOTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/MessageIOTest.o" =>
    "test/cxx/MessageIOTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/MessagePassingTest.o" =>
    "test/cxx/MessagePassingTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/VariantMapTest.o" =>
    "test/cxx/VariantMapTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/StringMapTest.o" =>
    "test/cxx/StringMapTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/ProcessMetricsCollectorTest.o" =>
    "test/cxx/ProcessMetricsCollectorTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/DateParsingTest.o" =>
    "test/cxx/DateParsingTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/UtilsTest.o" =>
    "test/cxx/UtilsTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/Utils/StrIntUtilsTest.o" =>
    "test/cxx/Utils/StrIntUtilsTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/IOUtilsTest.o" =>
    "test/cxx/IOUtilsTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/TemplateTest.o" =>
    "test/cxx/TemplateTest.cpp",
  "#{TEST_OUTPUT_DIR}cxx/Base64DecodingTest.o" =>
    "test/cxx/Base64DecodingTest.cpp"
}

def basic_test_cxx_flags
  @basic_test_cxx_flags ||= begin
    flags = [
      LIBEV_CFLAGS,
      LIBUV_CFLAGS,
      PlatformInfo.curl_flags,
      TEST_COMMON_CFLAGS
    ]
    if USE_ASAN
      flags << PlatformInfo.adress_sanitizer_flag
    end
    flags
  end
end

def test_cxx_include_paths
  @test_cxx_include_paths ||= [
    "test/cxx",
    "test/support",
    "src/agent",
    *CXX_SUPPORTLIB_INCLUDE_PATHS
  ]
end

def test_cxx_flags
  @test_cxx_flags ||= ["-include test/cxx/TestSupport.h"] +
    basic_test_cxx_flags
end

def test_cxx_ldflags
  @test_cxx_ldflags ||= begin
    result = "#{EXTRA_PRE_CXX_LDFLAGS} " <<
      "#{TEST_COMMON_LIBRARY.link_objects_as_string} " <<
      "#{TEST_BOOST_OXT_LIBRARY} #{libev_libs} #{libuv_libs} " <<
      "#{PlatformInfo.curl_libs} " <<
      "#{PlatformInfo.zlib_libs} " <<
      "#{PlatformInfo.crypto_libs} " <<
      "#{PlatformInfo.portability_cxx_ldflags}"
    result << " #{PlatformInfo.dmalloc_ldflags}" if USE_DMALLOC
    result << " #{PlatformInfo.adress_sanitizer_flag}" if USE_ASAN
    result << " #{EXTRA_CXX_LDFLAGS}"
    result.strip!
    result
  end
end

# Define compilation tasks for object files.
TEST_CXX_OBJECTS.each_pair do |object, source|
  define_cxx_object_compilation_task(
    object,
    source,
    :include_paths => test_cxx_include_paths,
    :flags => test_cxx_flags,
    :deps => 'test/cxx/TestSupport.h.gch'
  )
end

# Define compilation task for the agent executable.
dependencies = [
  TEST_CXX_OBJECTS.keys,
  LIBEV_TARGET,
  LIBUV_TARGET,
  TEST_BOOST_OXT_LIBRARY,
  TEST_COMMON_LIBRARY.link_objects,
  AGENT_OBJECTS.keys - [AGENT_MAIN_OBJECT]
].flatten.compact
file(TEST_CXX_TARGET => dependencies) do
  create_cxx_executable(
    TEST_CXX_TARGET,
    TEST_CXX_OBJECTS.keys + AGENT_OBJECTS.keys - [AGENT_MAIN_OBJECT],
    :flags => test_cxx_ldflags
  )
end

dependencies = [
  TEST_CXX_TARGET,
  "#{TEST_OUTPUT_DIR}allocate_memory",
  NATIVE_SUPPORT_TARGET,
  AGENT_TARGET
].compact
desc "Run unit tests for the C++ components"
task 'test:cxx' => dependencies do
  args = ENV['GROUPS'].to_s.split(";").map{ |name| "-g #{name}" }

  command = "#{File.expand_path(TEST_CXX_TARGET)} #{args.join(' ')}".strip
  if boolean_option('GDB')
    command = "gdb --args #{command}"
  elsif boolean_option('VALGRIND')
    valgrind_args = "--dsymutil=yes --vgdb=yes --vgdb-error=1 --child-silent-after-fork=yes"
    if boolean_option('LEAK_CHECK')
      valgrind_args << " --leak-check=yes"
    end
    if RUBY_PLATFORM =~ /darwin/
      valgrind_args << " --suppressions=valgrind-osx.supp"
    end
    command = "valgrind #{valgrind_args} #{command}"
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

file('test/cxx/TestSupport.h.gch' => generate_compilation_task_dependencies('test/cxx/TestSupport.h')) do
  compile_cxx(
    'test/cxx/TestSupport.h.gch',
    'test/cxx/TestSupport.h',
    :include_paths => test_cxx_include_paths,
    :flags => [
      "-x c++-header",
      basic_test_cxx_flags
    ].flatten
  )
end
