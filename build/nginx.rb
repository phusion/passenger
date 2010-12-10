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

desc "Build Nginx helper agent"
task :nginx => [
	AGENT_OUTPUT_DIR + 'nginx/PassengerHelperAgent',
	AGENT_OUTPUT_DIR + 'PassengerWatchdog',
	AGENT_OUTPUT_DIR + 'PassengerLoggingAgent',
	:native_support
]

dependencies = [
	'ext/nginx/HelperAgent.cpp',
	'ext/nginx/ScgiRequestParser.h',
	'ext/nginx/HttpStatusExtractor.h',
	'ext/common/StaticString.h',
	'ext/common/Account.h',
	'ext/common/AccountsDatabase.h',
	'ext/common/MessageServer.h',
	'ext/common/FileDescriptor.h',
	'ext/common/SpawnManager.h',
	'ext/common/Logging.h',
	'ext/common/ResourceLocator.h',
	'ext/common/Utils/ProcessMetricsCollector.h',
	'ext/common/Utils/VariantMap.h',
	'ext/common/HelperAgent/BacktracesServer.h',
	'ext/common/ApplicationPool/Interface.h',
	'ext/common/ApplicationPool/Pool.h',
	'ext/common/ApplicationPool/Server.h',
	LIBBOOST_OXT,
	LIBCOMMON,
]
file AGENT_OUTPUT_DIR + 'nginx/PassengerHelperAgent' => dependencies do
	output_dir = "#{AGENT_OUTPUT_DIR}nginx"
	sh "mkdir -p #{output_dir}" if !File.directory?(output_dir)
	create_executable "#{output_dir}/PassengerHelperAgent",
		'ext/nginx/HelperAgent.cpp',
		"-Iext -Iext/common " <<
		"#{PlatformInfo.portability_cflags} " <<
		"#{EXTRA_CXXFLAGS}  " <<
		"#{LIBCOMMON} " <<
		"#{LIBBOOST_OXT} " <<
		"#{PlatformInfo.portability_ldflags} " <<
		"#{AGENT_LDFLAGS} " <<
		"#{EXTRA_LDFLAGS}"
end

task :clean => 'nginx:clean'
desc "Clean all compiled Nginx files"
task 'nginx:clean' => 'common:clean' do
	sh("rm", "-rf", AGENT_OUTPUT_DIR + "nginx/PassengerHelperAgent")
end
