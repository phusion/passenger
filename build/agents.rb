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

AGENT_OBJECTS = {
  'WatchdogMain.o' => [
    'ext/common/agent/Watchdog/Main.cpp',
    'ext/common/agent/Watchdog/Main.cpp',
    'ext/common/agent/Watchdog/AgentWatcher.cpp',
    'ext/common/agent/Watchdog/CoreWatcher.cpp',
    'ext/common/agent/Watchdog/UstRouterWatcher.cpp',
    'ext/common/agent/Watchdog/InstanceDirToucher.cpp',
    'ext/common/agent/Watchdog/ApiServer.h',
    'ext/common/agent/ApiServerUtils.h',
    'ext/common/agent/Core/OptionParser.h',
    'ext/common/agent/UstRouter/OptionParser.h',
    'ext/common/ServerKit/Server.h',
    'ext/common/ServerKit/HttpServer.h',
    'ext/common/ServerKit/HttpHeaderParser.h',
    'ext/common/ServerKit/FileBufferedChannel.h',
    'ext/common/Constants.h',
    'ext/common/InstanceDirectory.h',
    'ext/common/ResourceLocator.h',
    'ext/common/Utils/VariantMap.h'
  ],
  'CoreMain.o' => [
    'ext/common/agent/Core/Main.cpp',
    'ext/common/agent/Core/OptionParser.h',
    'ext/common/agent/Core/ApiServer.h',
    'ext/common/agent/Core/ResponseCache.h',
    'ext/common/agent/Core/RequestHandler.h',
    'ext/common/agent/Core/RequestHandler/Client.h',
    'ext/common/agent/Core/RequestHandler/AppResponse.h',
    'ext/common/agent/Core/RequestHandler/TurboCaching.h',
    'ext/common/agent/Core/RequestHandler/Utils.cpp',
    'ext/common/agent/Core/RequestHandler/Hooks.cpp',
    'ext/common/agent/Core/RequestHandler/InitRequest.cpp',
    'ext/common/agent/Core/RequestHandler/BufferBody.cpp',
    'ext/common/agent/Core/RequestHandler/CheckoutSession.cpp',
    'ext/common/agent/Core/RequestHandler/SendRequest.cpp',
    'ext/common/agent/Core/RequestHandler/ForwardResponse.cpp',
    'ext/common/agent/ApiServerUtils.h',
    'ext/common/ServerKit/Server.h',
    'ext/common/ServerKit/HttpServer.h',
    'ext/common/ServerKit/HttpHeaderParser.h',
    'ext/common/ServerKit/AcceptLoadBalancer.h',
    'ext/common/ServerKit/FileBufferedChannel.h',
    'ext/common/ApplicationPool2/Pool.h',
    'ext/common/ApplicationPool2/Group.h',
    'ext/common/ApplicationPool2/BasicGroupInfo.h',
    'ext/common/ApplicationPool2/BasicProcessInfo.h',
    'ext/common/ApplicationPool2/Context.h',
    'ext/common/ApplicationPool2/Process.h',
    'ext/common/ApplicationPool2/Session.h',
    'ext/common/SpawningKit/Spawner.h',
    'ext/common/Constants.h',
    'ext/common/StaticString.h',
    'ext/common/Account.h',
    'ext/common/AccountsDatabase.h',
    'ext/common/MessageServer.h',
    'ext/common/FileDescriptor.h',
    'ext/common/Logging.h',
    'ext/common/ResourceLocator.h',
    'ext/common/Utils/ProcessMetricsCollector.h',
    'ext/common/Utils/SystemMetricsCollector.h',
    'ext/common/Utils/VariantMap.h'
  ],
  'UstRouterMain.o' => [
    'ext/common/agent/UstRouter/Main.cpp',
    'ext/common/agent/UstRouter/OptionParser.h',
    'ext/common/agent/UstRouter/ApiServer.h',
    'ext/common/agent/UstRouter/LoggingServer.h',
    'ext/common/agent/UstRouter/RemoteSender.h',
    'ext/common/agent/UstRouter/DataStoreId.h',
    'ext/common/agent/UstRouter/FilterSupport.h',
    'ext/common/agent/ApiServerUtils.h',
    'ext/common/ServerKit/Server.h',
    'ext/common/ServerKit/HttpServer.h',
    'ext/common/ServerKit/HttpHeaderParser.h',
    'ext/common/ServerKit/FileBufferedChannel.h',
    'ext/common/Constants.h',
    'ext/common/Logging.h',
    'ext/common/EventedServer.h',
    'ext/common/EventedClient.h',
    'ext/common/Utils/VariantMap.h',
    'ext/common/Utils/BlockingQueue.h'
  ],
  'SystemMetricsMain.o' => [
    'ext/common/agent/SystemMetrics/Main.cpp',
    'ext/common/Utils/SystemMetricsCollector.h'
  ],
  'TempDirToucherMain.o' => [
    'ext/common/agent/TempDirToucher/Main.cpp'
  ],
  'SpawnPreparerMain.o' => [
    'ext/common/agent/SpawnPreparer/Main.cpp'
  ]
}

AGENT_OBJECTS.each_pair do |agent_object, agent_dependencies|
  full_agent_object = "#{AGENT_OUTPUT_DIR}#{agent_object}"
  full_agent_object_dir = File.dirname(full_agent_object)
  file(full_agent_object => agent_dependencies) do
    sh "mkdir -p #{full_agent_object_dir}" if !File.directory?(full_agent_object_dir)
    compile_cxx(agent_dependencies[0],
      "-o #{full_agent_object} " <<
      "#{EXTRA_PRE_CXXFLAGS} " <<
      "-Iext -Iext/common " <<
      "#{AGENT_CFLAGS} #{LIBEV_CFLAGS} #{LIBUV_CFLAGS} " <<
      "#{PlatformInfo.curl_flags} " <<
      "#{PlatformInfo.zlib_flags} " <<
      "#{EXTRA_CXXFLAGS}")
  end
end

agent_libs = COMMON_LIBRARY.
  only(:base, :ust_router, :other).
  exclude('AgentsStarter.o')
agent_objects = AGENT_OBJECTS.keys.map { |x| "#{AGENT_OUTPUT_DIR}#{x}" }
dependencies = agent_objects + [
  'ext/common/Constants.h',
  'ext/common/agent/Main.cpp',
  LIBBOOST_OXT,
  agent_libs.link_objects,
  LIBEV_TARGET,
  LIBUV_TARGET
].flatten.compact
file AGENT_OUTPUT_DIR + AGENT_EXE => dependencies do
  agent_objects_as_string = agent_objects.join(" ")
  sh "mkdir -p #{AGENT_OUTPUT_DIR}" if !File.directory?(AGENT_OUTPUT_DIR)
  compile_cxx("ext/common/agent/Main.cpp",
    "-o #{AGENT_OUTPUT_DIR}#{AGENT_EXE}.o " <<
    "#{EXTRA_PRE_CXXFLAGS} " <<
    "-Iext -Iext/common " <<
    "#{AGENT_CFLAGS} #{LIBEV_CFLAGS} #{LIBUV_CFLAGS} " <<
    "#{PlatformInfo.curl_flags} " <<
    "#{PlatformInfo.zlib_flags} " <<
    "#{EXTRA_CXXFLAGS}")
  create_executable("#{AGENT_OUTPUT_DIR}#{AGENT_EXE}",
    "#{AGENT_OUTPUT_DIR}#{AGENT_EXE}.o",
    "#{agent_libs.link_objects_as_string} " <<
    "#{agent_objects_as_string} " <<
    "#{LIBBOOST_OXT_LINKARG} " <<
    "#{EXTRA_PRE_CXX_LDFLAGS} " <<
    "#{libev_libs} " <<
    "#{libuv_libs} " <<
    "#{PlatformInfo.curl_libs} " <<
    "#{PlatformInfo.zlib_libs} " <<
    "#{PlatformInfo.portability_cxx_ldflags} " <<
    "#{AGENT_LDFLAGS} " <<
    "#{EXTRA_CXX_LDFLAGS}")
end

task 'common:clean' do
  sh "rm -rf #{AGENT_OUTPUT_DIR}"
end
