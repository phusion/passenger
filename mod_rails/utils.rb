require 'pathname'
require 'etc'
module ModRails # :nodoc:

module Utils
private
	# Return the absolute version of _path_. This path is guaranteed to
	# to be "normal", i.e. it doesn't contain stuff like ".." or "/",
	# and it correctly respects symbolic links.
	# Raises Errno::ENOENT if _path_ doesn't exist.
	def normalize_path(path)
		return Pathname.new(path).realpath.to_s
	end
	
	def assert_valid_app_root(app_root)
		assert_valid_directory(app_root)
		assert_valid_directory("#{app_root}/app")
		assert_valid_directory("#{app_root}/public")
	end
	
	# Assert that _path_ is a directory.
	def assert_valid_directory(path)
		if !File.directory?(path)
			raise ArgumentError, "'#{path}' is not a valid directory."
		end
	end
	
	def assert_valid_username(username)
		# If username does not exist then getpwnam() will throw an ArgumentError.
		username.nil? || !Etc.getpwnam(username).nil?
	end
	
	def print_exception(current_location, exception)
		STDERR.puts("** Exception #{exception.class} in #{current_location} " <<
			"(#{exception}):\n" <<
			"\tfrom " <<
			exception.backtrace.join("\n\tfrom "))
	end
end

end # module ModRails