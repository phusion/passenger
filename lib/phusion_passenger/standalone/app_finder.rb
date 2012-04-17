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
require 'phusion_passenger/utils/file_system_watcher'

module PhusionPassenger
module Standalone

# Security note: can run arbitrary ruby code by evaluating passenger.conf
class AppFinder
	attr_accessor :dirs
	attr_reader :apps
	
	def self.looks_like_app_directory?(dir)
		return File.exist?("#{dir}/config.ru") ||
			File.exist?("#{dir}/config/environment.rb") ||
			File.exist?("#{dir}/passenger_wsgi.py")
	end
	
	def initialize(dirs, options = {})
		@dirs = dirs
		@options = options
	end
	
	def scan
		apps = []
		watchlist = []
		
		app_root = find_app_root
		apps << {
			:server_names => ["_"],
			:root => app_root
		}
		watchlist << app_root
		watchlist << "#{app_root}/config" if File.exist?("#{app_root}/config")
		watchlist << "#{app_root}/passenger.conf" if File.exist?("#{app_root}/passenger.conf")
		
		apps.sort! do |a, b|
			a[:root] <=> b[:root]
		end
		apps.map! do |app|
			config_filename = File.join(app[:root], "passenger.conf")
			if File.exist?(config_filename)
				local_options = load_config_file(:local_config, config_filename)
				merged_options = @options.merge(app)
				merged_options.merge!(local_options)
				merged_options
			else
				@options.merge(app)
			end
		end
		
		@apps = apps
		@watchlist = watchlist
		return apps
	end
	
	def monitor(termination_pipe)
		raise "You must call #scan first" if !@apps
		
		watcher = PhusionPassenger::Utils::FileSystemWatcher.new(@watchlist, termination_pipe)
		if wait_on_io(termination_pipe, 3)
			return
		end
		
		while true
			changed = watcher.wait_for_change
			watcher.close
			if changed
				old_apps = @apps
				# The change could be caused by a write to some passenger.conf file.
				# Wait for a short period so that the write has a chance to finish.
				if wait_on_io(termination_pipe, 0.25)
					return
				end
				
				new_apps = scan
				watcher = PhusionPassenger::Utils::FileSystemWatcher.new(@watchlist, termination_pipe)
				if old_apps != new_apps
					yield(new_apps)
				end
				
				# Don't process change events again for a short while,
				# but do detect changes while waiting.
				if wait_on_io(termination_pipe, 3)
					return
				end
			else
				return
			end
		end
	ensure
		watcher.close if watcher
	end
	
	##################

private
	def find_app_root
		if @dirs.empty?
			return File.expand_path(".")
		else
			return File.expand_path(@dirs[0])
		end
	end
	
	def load_config_file(context, filename)
		require 'phusion_passenger/standalone/config_file' unless defined?(ConfigFile)
		return ConfigFile.new(context, filename).options
	end
	
	def looks_like_app_directory?(dir)
		return AppFinder.looks_like_app_directory?(dir)
	end
	
	def filename_to_server_names(filename)
		basename = File.basename(filename)
		names = [basename]
		if basename !~ /^www\.$/i
			names << "www.#{basename}"
		end
		return names
	end
	
	# Wait until the given IO becomes readable, or until the timeout has
	# been reached. Returns true if the IO became readable, false if the
	# timeout has been reached.
	def wait_on_io(io, timeout)
		return !!select([io], nil, nil, timeout)
	end
end

end # module Standalone
end # module PhusionPassenger
