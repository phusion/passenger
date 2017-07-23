# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'platform_info/operating_system'

module PhusionPassenger
  module Utils

    # A /etc/hosts parser. Also supports writing groups of data to the file.
    class HostsFileParser
      def self.flush_dns_cache!
        if PlatformInfo.os_name_simple == "macosx"
          system("dscacheutil -flushcache")
        end
      end

      def initialize(filename_or_io = "/etc/hosts")
        if filename_or_io.respond_to?(:readline)
          read_and_parse(filename_or_io)
        else
          File.open(filename_or_io, "rb") do |f|
            read_and_parse(f)
          end
        end
      end

      def ip_count
        return @ips.size
      end

      def host_count
        return @host_names.size
      end

      def resolve(host_name)
        if host_name.downcase == "localhost"
          return "127.0.0.1"
        else
          return @host_names[host_name.downcase]
        end
      end

      def resolves_to_localhost?(hostname)
        ip = resolve(hostname)
        return ip == "127.0.0.1" || ip == "::1" || ip == "0.0.0.0"
      end

      def add_group_data(marker, data)
        begin_index = find_line(0, "###### BEGIN #{marker} ######")
        end_index = find_line(begin_index + 1, "###### END #{marker} ######") if begin_index
        if begin_index && end_index
          @lines[begin_index + 1 .. end_index - 1] = data.split("\n")
        else
          @lines << "###### BEGIN #{marker} ######"
          @lines.concat(data.split("\n"))
          @lines << "###### END #{marker} ######"
        end
      end

      def write(io)
        @lines.each do |line|
          io.puts(line)
        end
      end

    private
      def read_and_parse(io)
        lines = []
        ips = {}
        all_host_names = {}
        while !io.eof?
          line = io.readline
          line.sub!(/\n\Z/m, '')
          lines << line
          ip, host_names = parse_line(line)
          if ip
            ips[ip] ||= []
            ips[ip].concat(host_names)
            host_names.each do |host_name|
              all_host_names[host_name.downcase] = ip
            end
          end
        end
        @lines      = lines
        @ips        = ips
        @host_names = all_host_names
      end

      def parse_line(line)
        return nil if line =~ /^[\s\t]*#/
        line = line.strip
        return nil if line.empty?
        ip, *host_names = line.split(/[ \t]+/)
        return [ip, host_names]
      end

      def find_line(start_index, content)
        i = start_index
        while i < @lines.size
          if @lines[i] == content
            return i
          else
            i += 1
          end
        end
        return nil
      end
    end

  end
end
