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

module PhusionPassenger

# This module autodetects various platform-specific information, and
# provides that information through constants.
module PlatformInfo
private
	THIS_DIR = File.expand_path(File.dirname(__FILE__))
	THIS_FILE_MTIME = File.stat(__FILE__).mtime
	
	def self.private_class_method(name)
		metaclass = class << self; self; end
		metaclass.send(:private, name)
	end
	private_class_method :private_class_method
	
	# Turn the specified class method into a memoized one. If the given
	# class method is called without arguments, then its result will be
	# memoized, frozen, and returned upon subsequent calls without arguments.
	# Calls with arguments are never memoized.
	#
	# If +cache_to_disk+ is true, then result is cached to a file on disk,
	# so that memoized results persist over multiple process runs. This
	# cache file expires in 3600 seconds (1 hour) after it has been written.
	#
	#   def self.foo(max = 10)
	#      return rand(max)
	#   end
	#   memoize :foo
	#   
	#   foo   # => 3
	#   foo   # => 3
	#   foo(100)   # => 49
	#   foo(100)   # => 26
	#   foo   # => 3
	def self.memoize(method, cache_to_disk = false)
		metaclass = class << self; self; end
		metaclass.send(:alias_method, "_unmemoized_#{method}", method)
		variable_name = "@@memoized_#{method}".sub(/\?/, '')
		check_variable_name = "@@has_memoized_#{method}".sub(/\?/, '')
		eval("#{variable_name} = nil")
		eval("#{check_variable_name} = false")
		source = %Q{
		   def self.#{method}(*args)                                     # def self.httpd(*args)
		      if args.empty?                                             #    if args.empty?
		         if !#{check_variable_name}                              #       if !@@has_memoized_httpd
		            cache_dir = File.join(THIS_DIR, ".cache")            #          cache_dir = File.join(THIS_DIR, ".cache")
		            cache_file = File.join(cache_dir, "#{method}")       #          cache_file = File.join(cache_dir, "httpd")
		            read_from_cache_file = false                         #          read_from_cache_file = false
		            if #{cache_to_disk} && File.exist?(cache_file)       #          if #{cache_to_disk} && File.exist?(cache_file)
		               cache_file_stat = File.stat(cache_file)           #             cache_file_stat = File.stat(cache_file)
		               read_from_cache_file =                            #             read_from_cache_file =
		                  Time.now - cache_file_stat.mtime < 3600 &&     #                Time.now - cache_file_stat.mtime < 3600 &&
		                  cache_file_stat.mtime > THIS_FILE_MTIME        #                cache_file_stat.mtime > THIS_FILE_MTIME
		            end                                                  #             end
		            if read_from_cache_file                              #          if read_from_cache_file
		               data = File.read(cache_file)                      #             data = File.read(cache_file)
		               #{variable_name} = Marshal.load(data).freeze      #             @@memoized_httpd = Marshal.load(data).freeze
		               #{check_variable_name} = true                     #             @@has_memoized_httpd = true
		            else                                                 #          else
		               #{variable_name} = _unmemoized_#{method}.freeze   #             @@memoized_httpd = _unmemoized_httpd.freeze
		               #{check_variable_name} = true                     #             @@has_memoized_httpd = true
		               if #{cache_to_disk}                               #             if #{cache_to_disk}
		                  begin                                          #                begin
		                     if !File.directory?(cache_dir)              #                   if !File.directory?(cache_dir)
		                        Dir.mkdir(cache_dir)                     #                      Dir.mkdir(cache_dir)
		                     end                                         #                   end
		                     File.open(cache_file, "wb") do |f|          #                   File.open(cache_file, "wb") do |f|
		                        f.write(Marshal.dump(#{variable_name}))  #                      f.write(Marshal.dump(@@memoized_httpd))
		                     end                                         #                   end
		                  rescue Errno::EACCES                           #                rescue Errno::EACCES
		                    # Ignore permission error.                   #                   # Ignore permission error.
		                  end                                            #                end
		               end                                               #             end
		            end                                                  #          end
		         end                                                     #       end
		         return #{variable_name}                                 #       return @@memoized_httpd
		      else                                                       #    else
		         return _unmemoized_#{method}(*args)                     #       return _unmemoized_httpd(*args)
		      end                                                        #    end
		   end                                                           # end
		}
		class_eval(source)
	end
	private_class_method :memoize
	
	# Look in the directory +dir+ and check whether there's an executable
	# whose base name is equal to one of the elements in +possible_names+.
	# If so, returns the full filename. If not, returns nil.
	def self.select_executable(dir, *possible_names)
		possible_names.each do |name|
			filename = "#{dir}/#{name}"
			if File.file?(filename) && File.executable?(filename)
				return filename
			end
		end
		return nil
	end
	private_class_method :select_executable
	
	def self.read_file(filename)
		return File.read(filename)
	rescue
		return ""
	end
	private_class_method :read_file

public
	# Check whether the specified command is in $PATH, and return its
	# absolute filename. Returns nil if the command is not found.
	#
	# This function exists because system('which') doesn't always behave
	# correctly, for some weird reason.
	def self.find_command(name)
		ENV['PATH'].split(File::PATH_SEPARATOR).detect do |directory|
			path = File.join(directory, name.to_s)
			if File.executable?(path)
				return path
			end
		end
		return nil
	end
	
	def self.env_defined?(name)
		return !ENV[name].nil? && !ENV[name].empty?
	end
end

end # module PhusionPassenger
