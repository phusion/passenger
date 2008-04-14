#!/usr/bin/env ruby
$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/../lib")
require 'passenger/platform_info'

class MemoryStats
	class Process
		attr_accessor :pid
		attr_accessor :ppid
		attr_accessor :threads
		attr_accessor :vm_size              # in KB
		attr_accessor :name
		attr_accessor :private_dirty_rss    # in KB
		
		def vm_size_in_mb
			return sprintf("%.1f MB", vm_size / 1024.0)
		end
		
		def private_dirty_rss_in_mb
			if private_dirty_rss.is_a?(Numeric)
				return sprintf("%.1f MB", private_dirty_rss / 1024.0)
			else
				return "?"
			end
		end
		
		def print
			printf "%-6d %-6d %-6d %-9s %-9s %s\n", pid, ppid, threads,
				vm_size_in_mb, private_dirty_rss_in_mb, name
		end
	end
	
	def start
		processes = list_processes(:exe => PlatformInfo::HTTPD)
		print_process_list_stats(processes)
		
		puts
		processes = list_processes(:match => /^(Passenger|Rails) /)
		print_process_list_stats(processes)
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
			ps = "ps -C '#{name}'"
		elsif options[:name]
			ps = "ps -C '#{options[:name]}'"
		elsif options[:match]
			ps = "ps -A"
		else
			raise ArgumentError, "Invalid options."
		end
		
		processes = []
		list = `#{ps} -o pid,ppid,nlwp,vsz,command`.split("\n")
		list.shift
		list.each do |line|
			line.gsub!(/^ */, '')
			line.gsub!(/ *$/, '')
			
			p = Process.new
			p.pid, p.ppid, p.threads, p.vm_size, p.name = line.split(/ +/, 5)
			if p.name !~ /^ps/ && (!options[:match] || p.name.match(options[:match]))
				[:pid, :ppid, :threads, :vm_size].each do |attr|
					p.send("#{attr}=", p.send(attr).to_i)
				end
				p.private_dirty_rss = determine_private_dirty_rss(p.pid)
				processes << p
			end
		end
		return processes
	end

private
	# Returns the private dirty RSS for the given process, in KB.
	def determine_private_dirty_rss(pid)
		total = 0
		File.read("/proc/#{pid}/smaps").split("\n").each do |line|
			line =~ /^(Shared|Private)_Dirty: +(\d+)/
			if $2
				total += $2.to_i
			end
		end
		return total
	rescue Errno::EACCES
		return nil
	end
	
	def print_header
		puts "PID    PPID   Thrds  VM Size   Private   Name"
		puts "-" * 79
	end
	
	def print_process_list_stats(processes)
		total_private_dirty_rss = 0
		some_private_dirty_rss_cannot_be_determined = false
		
		print_header
		processes.each do |p|
			if p.private_dirty_rss.is_a?(Numeric)
				total_private_dirty_rss += p.private_dirty_rss
			else
				some_private_dirty_rss_cannot_be_determined = true
			end
			p.print
		end
		puts   "### Processes: #{processes.size}"
		printf "### Total private dirty RSS: %.2f MB", total_private_dirty_rss / 1024.0
		if some_private_dirty_rss_cannot_be_determined
			puts " (?)"
		else
			puts
		end
	end
end

MemoryStats.new.start

