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

PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'
PhusionPassenger.require_passenger_lib 'utils/shellwords'

module PhusionPassenger

  module PlatformInfo
    # Returns a list of network interfaces active on the current system, or
    # nil if unable to autodetect.
    #
    #   [
    #     {
    #       :device => 'en0',
    #       :type   => :ethernet,
    #       :flags  => [:promisc, :broadcast, :multicast],
    #       :ipv4   => ['127.0.0.1'],
    #       :ipv6   => ['::1']
    #     },
    #     ...
    #   ]
    def self.network_interfaces
      case os_name_simple
      when 'linux'
        network_interfaces_linux
      when 'macosx', 'freebsd'
        network_interfaces_bsd
      else
        nil
      end
    end

    # Returns the names of the network interfaces used for public
    # Internet IPv4 and IPv6 traffic. This is autodetected
    # by checking which interfaces can route to Google.
    #
    # The format is:
    #
    #   {
    #     :ipv4 => "device name" | nil | :unknown,
    #     :ipv6 => "device name" | nil | :unknown
    #   }
    #
    # A value of nil means that there is no interface that supports
    # the corresponding IP traffic.
    # A value of :unknown means that the information cannot be
    # autodetected.
    def self.default_network_interfaces
      case os_name_simple
      when 'linux'
        {
          :ipv4 => default_network_interface_linux_for_ip_type(:ipv4),
          :ipv6 => default_network_interface_linux_for_ip_type(:ipv6)
        }
      when 'macosx', 'freebsd'
        {
          :ipv4 => default_network_interface_bsd_for_ip_type(:ipv4),
          :ipv6 => default_network_interface_bsd_for_ip_type(:ipv6)
        }
      else
        {
          :ipv4 => :unknown,
          :ipv6 => :unknown
        }
      end
    end

  private
    def self.network_interfaces_linux
      result = {}
      ip_command = find_system_command('ip')

      if ip_command
        current_iface = nil
        output = `#{Shellwords.escape ip_command} addr show`
        output.split("\n").each do |line|
          elems = line.split

          if line =~ /^[0-9]+: /
            # Format:
            # <NUMBER>: <DEVICE>: <<FLAGS>> ...
            device = elems[1].sub(/:$/, '')
            flags = parse_bsd_or_linux_ifconfig_flags(elems[2])
            current_iface = {
              :device => device,
              :type   => flags.include?(:loopback) ? :loopback : :unknown,
              :flags  => flags,
              :ipv4   => [],
              :ipv6   => []
            }
            result[device] = current_iface

          elsif elems[0] == 'link/ether'
            current_iface[:type] = :ethernet

          elsif elems[0] == 'inet'
            # Format: inet <IP[/CIDR PREFIX]> ... <ACTUAL DEVICE>
            device = elems.last
            ip = elems[1].sub(%r{/.*}, '')
            if current_iface[:device] == device
              actual_iface = current_iface
            else
              actual_iface = (result[device] ||= {
                :device => device,
                :type   => :unknown,
                :flags  => [],
                :ipv4   => [],
                :ipv6   => []
              })
            end
            actual_iface[:ipv4] << ip

          elsif elems[0] == 'inet6'
            # Format: inet6 <IP[/CIDR PREFIX]> ...
            ip = elems[1].sub(%r{/.*}, '')
            current_iface[:ipv6] << ip
          end
        end
      end

      Dir['/sys/class/net/*'].each do |path|
        next if !File.directory?(path)
        device = File.basename(path)
        iface = (result[device] ||= {
          :device => device,
          :type   => :unknown,
          :flags  => [],
          :ipv4   => [],
          :ipv6   => []
        })

        if iface[:type] == :unknown && File.exist?("#{path}/type")
          case File.read("#{path}/type").strip
          when '1'
            iface[:type] = :ethernet
          when '772'
            iface[:type] = :loopback
          end
        end

        if File.exist?("#{path}/flags")
          flags = File.read("#{path}/flags").to_i(16)
          if flags & 0x0100 && !iface[:flags].include?(:promisc)
            iface[:flags] << :promisc
          end
        end
      end

      result.values
    end

    def self.default_network_interface_linux_for_ip_type(type)
      ip_command = PlatformInfo.find_system_command('ip')
      return :unknown if ip_command.nil?

      # See the comments for default_network_interface_bsd_for_ip_type
      if type == :ipv4
        address = '8.8.8.8'
        output = `#{Shellwords.escape ip_command} -4 route get #{address} 2>/dev/null`
      else
        address = '2404:6800:400a:800::1012'
        output = `#{Shellwords.escape ip_command} -6 route get #{address} 2>/dev/null`
      end

      first_line = output.split("\n").first
      elems = (first_line || '').split
      if elems[0] == address
        first_line =~ / dev (.+?) /
        $1
      else
        nil
      end
    end


    def self.network_interfaces_bsd
      ifconfig = find_system_command('ifconfig')
      return nil if ifconfig.nil?

      result = []
      current_interface = nil

      lines = `#{Shellwords.escape ifconfig} -a`.split("\n")
      lines.each do |line|
        elems = line.split

        if line =~ /^[a-z0-9\-_]+:/
          device = elems[0].chomp(':')
          flags = parse_bsd_or_linux_ifconfig_flags(elems[1])
          current_interface = {
            :device => device,
            :type   => flags.include?(:loopback) ? :loopback : :unknown,
            :flags  => flags,
            :ipv4   => [],
            :ipv6   => []
          }
          result << current_interface

        elsif elems[0] == 'ether'
          current_interface[:type] = :ethernet

        elsif elems[0] == 'inet' || elems[0] == 'inet6'
          # Format:
          # inet [alias] <IP ADDRESS[/CIDR PREFIX]> ...

          if elems[1] == 'alias'
            ip = elems[2]
          else
            ip = elems[1]
          end
          if ip.include?('/')
            ip = ip.split('/', 2)[0]
          end

          if elems[0] == 'inet'
            current_interface[:ipv4] << ip
          else
            current_interface[:ipv6] << ip
          end
        end
      end

      result
    end

    def self.default_network_interface_bsd_for_ip_type(type)
      route = PlatformInfo.find_system_command('route')
      return :unknown if route.nil?

      # We ping a public server to test which of our interfaces
      # is used for outgoing Internet traffic.
      # 8.8.8.8 = Google's public DNS
      # 2404:6800:400a:800::1012 = ipv6.google.com
      # This should work for everybody outside Google's cluster infrastructure,
      # and who didn't block Google on the network routing level.
      #
      # Note that this even works for mainland Chinese users, as the GFW
      # blocks on the TCP level.
      #
      # We redirect stderr to /dev/null because we don't want routing
      # errors to be printed.
      if type == :ipv4
        output = `#{Shellwords.escape route} -n get 8.8.8.8 2>/dev/null`
      else
        output = `#{Shellwords.escape route} -n get -inet6 2404:6800:400a:800::1012 2>/dev/null`
      end

      lines = output.split("\n")
      lines.each do |line|
        elems = line.split
        if elems[0] == 'interface:' && elems[1]
          return elems[1]
        end
      end

      nil
    end


    def self.parse_bsd_or_linux_ifconfig_flags(flags)
      if flags =~ /<(.+)>/
        $1.split(',').map { |x| x.downcase.to_sym }
      else
        []
      end
    end
  end

end # module PhusionPassenger
