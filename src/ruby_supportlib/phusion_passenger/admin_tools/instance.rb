# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014-2017 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'
PhusionPassenger.require_passenger_lib 'utils/json'

module PhusionPassenger
  module AdminTools

    class Instance
      STALE_TIMEOUT = 60

      attr_reader :path, :state, :name, :server_software, :properties

      def initialize(path)
        @path = path
        # Possible states:
        #
        # * :good - Everything is good. But the Watchdog process that created
        #   it might have died. In that case, the directory should be cleaned up.
        #   Use `locked?` to check.
        #
        # * :not_finalized - The instance directory hasn't been finalized yet,
        #   so we can't read any information from it. It is possible that the
        #   process that has created it has died, i.e. that it will never
        #   become finalized. In that case, the directory should be cleaned up.
        #   Use `stale?` to check.
        #
        # * :structure_version_unsupported - The properties file advertised a
        #   structure version that we don't support. We can't read any
        #   information from it.
        #
        # * :corrupted - The instance directory is corrupted in some way.
        @state = nil

        reload_properties
      end

      def locked?
        if PlatformInfo.supports_flock?
          begin
            !File.open("#{@path}/lock", "r") do |f|
              f.flock(File::LOCK_EX | File::LOCK_NB)
            end
          rescue Errno::ENOENT
            false
          end
        else
          # Solaris :-(
          # Since using fcntl locks in Ruby is a huge pain,
          # we'll fallback to checking the watchdog PID.
          watchdog_alive?
        end
      end

      def stale?
        stat = File.stat(@path)
        if stat.mtime < Time.now - STALE_TIMEOUT
          !locked?
        else
          false
        end
      end

      def watchdog_alive?
        process_is_alive?(@watchdog_pid)
      end

      def http_request(socket_path, request)
        sock = Net::BufferedIO.new(UNIXSocket.new("#{@path}/#{socket_path}"))
        begin
          request.exec(sock, "1.1", request.path)

          done = false
          while !done
            response = Net::HTTPResponse.read_new(sock)
            done = !response.kind_of?(Net::HTTPContinue)
          end

          response.reading_body(sock, request.response_body_permitted?) do
            # Nothing
          end
        ensure
          sock.close
        end

        return response
      end

      def watchdog_pid
        properties["watchdog_pid"]
      end

      # Returns the Core's PID, or nil if it is not running
      # or isn't finished initializing.
      def core_pid
        @core_pid ||= begin
          begin
            data = File.read("#{@path}/core.pid")
            if data.empty?
              nil
            else
              data.to_i
            end
          rescue Errno::ENOENT
            nil
          end
        end
      end

      def web_server_control_process_pid
        File.read("#{@path}/web_server_info/control_process.pid").to_i
      end

      def full_admin_password
        @full_admin_password ||= File.read("#{@path}/full_admin_password.txt")
      end

      def read_only_admin_password
        @read_only_admin_password ||= File.read("#{@path}/read_only_admin_password.txt")
      end

      def as_json
        json = properties
        json["instance_dir"]["path"] = path
        json["core_pid"] = core_pid
        json
      end

    private
      class CorruptedError < StandardError
      end

      def reload_properties
        @properties = nil
        @state      = nil
        begin
          reload_properties_internal
        rescue CorruptedError
          @state = :corrupted
        end
      end

      def reload_properties_internal
        if !File.exist?("#{@path}/creation_finalized")
          @state = :unfinalized
          return
        end
        check(File.file?("#{@path}/creation_finalized"))

        properties = nil
        begin
          File.open("#{@path}/properties.json", "r") do |f|
            properties = Utils::JSON.parse(f.read)
          end
        rescue Errno::ENOENT
          raise CorruptedError
        rescue RuntimeError => e
          if e.message =~ /parse error/
            raise CorruptedError
          else
            raise
          end
        end

        check(!properties.nil?)
        props = properties["instance_dir"]
        check(!props.nil?)
        major_version = props["major_version"]
        minor_version = props["minor_version"]
        check(major_version.is_a?(Integer))
        check(minor_version.is_a?(Integer))
        name = properties["name"]
        check(name.is_a?(String))
        check(!name.empty?)
        server_software = properties["server_software"]
        check(server_software.is_a?(String))
        check(!server_software.empty?)
        watchdog_pid = properties["watchdog_pid"]
        check(watchdog_pid.is_a?(Integer))

        version_supported =
          major_version == PhusionPassenger::SERVER_INSTANCE_DIR_STRUCTURE_MAJOR_VERSION &&
          minor_version >= PhusionPassenger::SERVER_INSTANCE_DIR_STRUCTURE_MIN_SUPPORTED_MINOR_VERSION
        if !version_supported
          @state = :structure_version_unsupported
          return
        end

        check(File.file?("#{@path}/lock"))
        check(File.directory?("#{@path}/agents.s"))
        check(File.directory?("#{@path}/apps.s"))

        @properties = properties
        @name = name
        @server_software = server_software
        @watchdog_pid = watchdog_pid
        @state = :good
      end

      def check(val)
        raise CorruptedError if !val
      end

      def process_is_alive?(pid)
        begin
          Process.kill(0, pid)
          return true
        rescue Errno::ESRCH
          return false
        rescue SystemCallError => e
          return true
        end
      end
    end

  end # module AdminTools
end # module PhusionPassenger
