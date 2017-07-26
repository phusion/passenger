# frozen_string_literal: true
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2017 Phusion Holding B.V.
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

require 'socket'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'config/command'
PhusionPassenger.require_passenger_lib 'utils/json'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'
PhusionPassenger.require_passenger_lib 'platform_info/networking'
PhusionPassenger.require_passenger_lib 'platform_info/linux'

module PhusionPassenger
  module Config

    class SystemPropertiesCommand < Command
      def run
        # Output format must be compatible with Appminton
        # "get server_properties" protocol command.
        network_interfaces = PlatformInfo.network_interfaces
        default_network_interfaces = PlatformInfo.default_network_interfaces
        puts PhusionPassenger::Utils::JSON.generate(
          :ipv4_addresses => ip_addresses(network_interfaces, :ipv4, default_network_interfaces),
          :ipv6_addresses => ip_addresses(network_interfaces, :ipv6, default_network_interfaces),
          :default_ipv4 => default_ip(network_interfaces, :ipv4, default_network_interfaces),
          :default_ipv6 => default_ip(network_interfaces, :ipv6, default_network_interfaces),
          :network_interfaces => network_interfaces,
          :hostname => Socket.gethostname,
          :fqdn => fqdn,
          :os => os
        )
      end

    private
      def ip_addresses(network_interfaces, type, default_network_interfaces)
        result = network_interfaces.map { |iface| iface[type] }.flatten
        device = default_network_interfaces[type]
        if device && device != :unknown
          default_iface = network_interfaces.find do |iface|
            iface[:device] == device
          end
          result << default_iface[type].first
        end
        result.uniq!
        result
      end

      def default_ip(network_interfaces, type, default_network_interfaces)
        device = default_network_interfaces[type]
        if device == :unknown
          '<UNKNOWN>'
        elsif device
          result = network_interfaces.find do |iface|
            iface[:device] == device
          end
          result[type].first
        else
          nil
        end
      end

      def fqdn
        hostname = PlatformInfo.find_command('hostname')
        if hostname
          `#{Shellwords.escape hostname} -f`.strip
        else
          nil
        end
      end

      def os
        {
          :simple_name => PlatformInfo.os_name_simple,
          :full_name => PlatformInfo.os_name_full,
          :version => PlatformInfo.os_version,
          :linux_distro => PlatformInfo.linux_distro,
          :linux_distro_tags => PlatformInfo.linux_distro_tags
        }
      end
    end

  end # module Config
end # module PhusionPassenger
