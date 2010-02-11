module PhusionPassenger
module Utils

# A /etc/hosts parser. Also supports writing groups of data to the file.
class HostsFileParser
	def self.flush_dns_cache!
		if RUBY_PLATFORM =~ /darwin/
			system("dscacheutil -flushcache")
		end
	end
	
	def initialize(filename_or_io = "/etc/hosts")
		if filename_or_io.respond_to?(:readline)
			read_and_parse(filename_or_io)
		else
			File.open(filename_or_io, "r") do |f|
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