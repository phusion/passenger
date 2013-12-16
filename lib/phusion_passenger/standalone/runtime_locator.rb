# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013 Phusion
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

PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
PhusionPassenger.require_passenger_lib 'utils/json'
require 'etc'

module PhusionPassenger
module Standalone

# Helper class for locating runtime files used by Passenger Standalone.
class RuntimeLocator
	attr_reader :runtime_dir

	def initialize(runtime_dir, nginx_version = PhusionPassenger::PREFERRED_NGINX_VERSION)
		@runtime_dir = runtime_dir || default_runtime_dir
		@nginx_version = nginx_version
	end

	def self.looks_like_support_dir?(dir)
		File.exist?("#{dir}/agents/PassengerWatchdog") &&
			File.exist?("#{dir}/common/libboost_oxt.a") &&
			File.exist?("#{dir}/common/libpassenger_common/ApplicationPool2/Implementation.o")
	end

	def reload
		@has_support_dir = @has_nginx_binary = false
	end

	# Returns the directory in which Passenger Standalone
	# support binaries are stored, or nil if not installed.
	def find_support_dir
		return @support_dir if @has_support_dir

		if PhusionPassenger.originally_packaged?
			if debugging?
				@support_dir = PhusionPassenger.buildout_dir
			else
				dir = "#{@runtime_dir}/#{version}/support-#{cxx_compat_id}"
				if self.class.looks_like_support_dir?(dir)
					@support_dir = dir
				else
					@support_dir = nil
				end
			end
		else
			@support_dir = PhusionPassenger.lib_dir
		end
		@has_support_dir = true
		return @support_dir
	end

	# Returns the path to the Nginx binary that Passenger Standalone
	# may use, or nil if not installed.
	def find_nginx_binary
		return @nginx_binary if @has_nginx_binary

		if File.exist?(config_filename)
			config = PhusionPassenger::Utils::JSON.parse(File.read(config_filename))
		else
			config = {}
		end
		if result = config["nginx_binary"]
			@nginx_binary = result
		elsif PhusionPassenger.natively_packaged? && @nginx_version == PhusionPassenger::PREFERRED_NGINX_VERSION
			@nginx_binary = "#{PhusionPassenger.lib_dir}/PassengerWebHelper"
		else
			filename = "#{@runtime_dir}/#{version}/webhelper-#{@nginx_version}-#{cxx_compat_id}/PassengerWebHelper"
			if File.exist?(filename)
				@nginx_binary = filename
			else
				@nginx_binary = nil
			end
		end
		@has_nginx_binary = true
		return @nginx_binary
	end

	def find_agents_dir
		return "#{find_support_dir}/agents"
	end

	def find_lib_dir
		return find_support_dir
	end

	def everything_installed?
		return find_support_dir && find_nginx_binary
	end

	def install_targets
		result = []
		result << :nginx if find_nginx_binary.nil?
		result << :support_binaries if find_support_dir.nil?
		return result
	end

	# Returns the directory to which support binaries may be installed,
	# in case the RuntimeInstaller is to be invoked.
	def support_dir_install_destination
		if PhusionPassenger.originally_packaged?
			if debugging?
				return "#{PhusionPassenger.lib_dir}/common/libpassenger_common"
			else
				return "#{@runtime_dir}/#{version}/support-#{cxx_compat_id}"
			end
		else
			return nil
		end
	end

	# Returns the directory to which the Nginx binary may be installed,
	# in case the RuntimeInstaller is to be invoked.
	def nginx_binary_install_destination
		return "#{@runtime_dir}/#{version}/webhelper-#{@nginx_version}-#{cxx_compat_id}"
	end

private
	def default_runtime_dir
		if Process.uid == 0
			# It is important that the default runtime dir for the root user
			# is a publicly accessible directory, because when --user is given,
			# the agents are run as non-root users.
			return "/var/lib/#{GLOBAL_NAMESPACE_DIRNAME}/standalone"
		else
			home = Etc.getpwuid.dir
			return "#{home}/#{USER_NAMESPACE_DIRNAME}/standalone"
		end
	end

	def debugging?
		return ['yes', 'y', 'true', '1'].include?(ENV['PASSENGER_DEBUG'].to_s.downcase)
	end

	def version
		return PhusionPassenger::VERSION_STRING
	end

	def cxx_compat_id
		return PlatformInfo.cxx_binary_compatibility_id
	end

	def config_filename
		return "#{@runtime_dir}/config.json"
	end
end

end # module Standalone
end # module PhusionPassenger
