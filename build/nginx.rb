#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2010  Phusion
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
task 'nginx:clean' do
	sh("rm", "-rf", AGENT_OUTPUT_DIR + "nginx/PassengerHelperAgent")
end
