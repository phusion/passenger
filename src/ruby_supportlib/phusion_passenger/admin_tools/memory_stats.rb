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

PhusionPassenger.require_passenger_lib 'platform_info/apache'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'

module PhusionPassenger
  module AdminTools

    class MemoryStats
      # Information about a single process.
      class Process
        attr_accessor :pid
        attr_accessor :ppid
        attr_accessor :threads
        attr_accessor :vm_size              # in KB
        attr_accessor :rss                  # in KB
        attr_accessor :cpu
        attr_accessor :name
        attr_accessor :private_dirty_rss    # in KB

        def vm_size_in_mb
          return sprintf("%.1f MB", vm_size / 1024.0)
        end

        def rss_in_mb
          return sprintf("%.1f MB", rss / 1024.0)
        end

        def private_dirty_rss_in_mb
          if private_dirty_rss.is_a?(Numeric)
            return sprintf("%.1f MB", private_dirty_rss / 1024.0)
          else
            return "?"
          end
        end

        def to_a
          return [pid, ppid, vm_size_in_mb, private_dirty_rss_in_mb, rss_in_mb, name]
        end
      end

      # Returns a list of Apache processes, which may be the empty list if Apache is
      # not running. If the Apache executable name is unknown then nil will be returned.
      def apache_processes
        @apache_processes ||= begin
          if PlatformInfo.httpd
            processes = list_processes(:exe => PlatformInfo.httpd)
            if processes.empty?
              # On some Linux distros, the Apache worker processes
              # are called "httpd.worker"
              processes = list_processes(:exe => "#{PlatformInfo.httpd}.worker")
            end
            processes
          else
            nil
          end
        end
      end

      # Returns a list of Nginx processes, which may be the empty list if
      # Nginx is not running.
      def nginx_processes
        @nginx_processes ||= list_processes(:exe => "nginx")
      end

      # Returns a list of Phusion Passenger processes, which may be the empty list if
      # Phusion Passenger is not running.
      def passenger_processes
        @passenger_processes ||= list_processes(:match =>
          /((^| )Passenger|(^| )Rails:|(^| )Rack:|wsgi-loader.py|(.*)PassengerAgent|rack-loader.rb)/)
      end

      # Returns the sum of the memory usages of all given processes.
      # Returns a pair [usage, accurate]. +usage+ is the summed memory usage in KB,
      # and +accurate+ indicates whether this sum is accurate. This may be false
      # if some process's memory usage cannot be determined.
      def sum_memory_usage(processes)
        total = 0
        if should_show_private_dirty_rss?
          accurate = true
          processes.each do |p|
            if p.private_dirty_rss.is_a?(Numeric)
              total += p.private_dirty_rss
            else
              accurate = true
            end
          end
          return [total, accurate]
        else
          processes.each do |p|
            total += p.rss
          end
          return [total, true]
        end
      end

      def platform_provides_private_dirty_rss_information?
        return os_name_simple == "linux"
      end

      # Returns whether root privileges are required in order to measure private dirty RSS.
      # Only meaningful if #platform_provides_private_dirty_rss_information? returns true.
      def root_privileges_required_for_private_dirty_rss?
        all_processes = (apache_processes || []) + nginx_processes + passenger_processes
        return all_processes.any?{ |p| p.private_dirty_rss.nil? }
      end

      def should_show_private_dirty_rss?
        return platform_provides_private_dirty_rss_information? &&
          (::Process.euid == 0 || root_privileges_required_for_private_dirty_rss?)
      end

      # Determine the system's RAM usage, not including swap.
      # Returns a tuple [total, used] where both numbers are in KB, or nil
      # if the system's RAM usage cannot be determined.
      def system_ram_usage
        @total_system_ram ||= begin
          case os_name_simple
          when "linux"
            free_text = `free -k`

            free_text =~ %r{Mem:(.+)$}
            line = $1.strip
            total = line.split(/ +/).first.to_i

            free_text =~ %r{buffers/cache:(.+)$}
            line = $1.strip
            used = line.split(/ +/).first.to_i

            [total, used]
          when "macosx"
            vm_stat = `vm_stat`
            vm_stat =~ /page size of (\d+) bytes/
            page_size = $1
            vm_stat =~ /Pages free: +(\d+)/
            free = $1
            vm_stat =~ /Pages active: +(\d+)/
            active = $1
            vm_stat =~ /Pages inactive: +(\d+)/
            inactive = $1
            vm_stat =~ /Pages wired down: +(\d+)/
            wired = $1

            if page_size && free && active && inactive && wired
              page_size = page_size.to_i
              free = free.to_i * page_size / 1024
              active = active.to_i * page_size / 1024
              inactive = inactive.to_i * page_size / 1024
              wired = wired.to_i * page_size / 1024

              used = active + wired
              [free + inactive + used, used]
            else
              nil
            end
          else
            `top` =~ /(\d+)(K|M) Active, (\d+)(K|M) Inact, (\d+)(K|M) Wired,.*?(\d+)(K|M) Free/
            if $1 && $2 && $3 && $4 && $5 && $6 && $7 && $8
              to_kb = lambda do |number, unit|
                if unit == 'K'
                  number.to_i
                else
                  number.to_i * 1024
                end
              end

              active = to_kb.call($1, $2)
              inactive = to_kb.call($3, $4)
              wired = to_kb.call($5, $6)
              free = to_kb.call($7, $8)

              used = active + wired
              [free + inactive + used, used]
            else
              nil
            end
          end
        end
      end

    private
      def os_name_simple
        return PlatformInfo.os_name_simple
      end

      # Returns a list of Process objects that match the given search criteria.
      #
      #  # Search by executable path.
      #  list_processes(:exe => '/usr/sbin/apache2')
      #
      #  # Search by executable name.
      #  list_processes(:name => 'ruby1.8')
      #
      #  # Search by process name.
      #  list_processes(:match => 'Passenger FrameworkSpawner')
      def list_processes(options)
        if options[:exe]
          name = options[:exe].sub(/.*\/(.*)/, '\1')
          if os_name_simple == "linux"
            ps = "ps -C '#{name}'"
          else
            ps = "ps -A"
            options[:match] = Regexp.new(Regexp.escape(name))
          end
        elsif options[:name]
          if os_name_simple == "linux"
            ps = "ps -C '#{options[:name]}'"
          else
            ps = "ps -A"
            options[:match] = Regexp.new(" #{Regexp.escape(options[:name])}")
          end
        elsif options[:match]
          ps = "ps -A"
        else
          raise ArgumentError, "Invalid options."
        end

        processes = []
        case os_name_simple
        when "solaris"
          ps_output = `#{ps} -o pid,ppid,nlwp,vsz,rss,pcpu,comm`
          threads_known = true
        when "macosx"
          ps_output = `#{ps} -w -o pid,ppid,vsz,rss,%cpu,command`
          threads_known = false
        else
          ps_output = `#{ps} -w -o pid,ppid,nlwp,vsz,rss,%cpu,command`
          threads_known = true
        end
        list = force_binary(ps_output).split("\n")
        list.shift
        list.each do |line|
          line.gsub!(/^ */, '')
          line.gsub!(/ *$/, '')

          p = Process.new
          if threads_known
            p.pid, p.ppid, p.threads, p.vm_size, p.rss, p.cpu, p.name = line.split(/ +/, 7)
          else
            p.pid, p.ppid, p.vm_size, p.rss, p.cpu, p.name = line.split(/ +/, 6)
          end
          p.name.sub!(/\Aruby: /, '')
          p.name.sub!(/ \(ruby\)\Z/, '')
          if p.name !~ /^ps/ && (!options[:match] || p.name.match(options[:match]))
            # Convert some values to integer.
            [:pid, :ppid, :vm_size, :rss].each do |attr|
              p.send("#{attr}=", p.send(attr).to_i)
            end
            p.threads = p.threads.to_i if threads_known

            if platform_provides_private_dirty_rss_information?
              p.private_dirty_rss = determine_private_dirty_rss(p.pid)
            end
            processes << p
          end
        end
        return processes
      end

      # Returns the private dirty RSS for the given process, in KB.
      def determine_private_dirty_rss(pid)
        total = 0
        File.read("/proc/#{pid}/smaps").split("\n").each do |line|
          line =~ /^(Private)_Dirty: +(\d+)/
          if $2
            total += $2.to_i
          end
        end
        if total == 0
          return nil
        else
          return total
        end
      rescue Errno::EACCES, Errno::ENOENT
        return nil
      end

      if ''.respond_to?(:force_encoding)
        def force_binary(str)
          str.force_encoding('binary')
        end
      else
        def force_binary(str)
          str
        end
      end
    end

  end # module AdminTools
end # module PhusionPassenger
