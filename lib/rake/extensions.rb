require 'pathname'

# Provides useful extensions for Rake.
module RakeExtensions

# Allows one to define Rake rules in the context of the given
# subdirectory. For example,
#
#  subdir 'foo' do
#     file 'libfoo.so' => ['foo.c'] do
#        sh 'gcc foo.c -shared -fPIC -o libfoo.so'
#     end
#  end
#  
#  subdir 'bar' do
#     file 'bar' => ['bar.c', '../foo/libfoo.so'] do
#         sh 'gcc bar.c -o bar -L../foo -lfoo'
#     end
#  end
#
# is equivalent to:
#
#  file 'foo/libfoo.so' => ['foo/foo.c'] do
#     Dir.chdir('foo') do
#        sh 'gcc foo.c -shared -fPIC -o libfoo.so'
#     end
#  end
#  
#  file 'bar/bar' => ['bar/bar.c', 'foo/libfoo.so'] do
#      Dir.chdir('bar') do
#         sh 'gcc bar.c -o bar -L../foo -lfoo'
#      end
#  end
#
# <b>Note:</b> Only the <tt>file</tt> and <tt>target</tt> Rake
# commands are supported.
def subdir(dir, &block)
	subdir = Subdir.new(dir)
	Dir.chdir(dir) do
		subdir.instance_eval(&block)
	end
end

class Subdir # :nodoc:
	def initialize(dir)
		@dir = dir
		@toplevel_dir = Pathname.getwd
	end
	
	def file(args, &block)
		case args
		when String
			args = mangle_path(args)
		when Hash
			target = mangle_path(args.keys[0])
			sources = mangle_path_or_path_array(args.values[0])
			args = { target => sources }
		end
		Rake::FileTask.define_task(args) do
			puts "### In #{@dir}:"
			Dir.chdir(@dir) do
				Object.class_eval(&block)
			end
			puts ""
		end
	end
	
	def task(*args, &block)
		if !args.empty? && args[0].is_a?(Hash)
			target = args[0].keys[0]
			sources = mangle_path_or_path_array(args[0].values[0])
			args[0] = { target => sources }
		end
		if block_given?
			Rake::Task.define_task(*args) do
				puts "### In #{@dir}:"
				Dir.chdir(@dir) do
					Object.class_eval(&block)
				end
				puts ""
			end
		else
			Rake::Task.define_task(*args)
		end
	end

private
	def mangle_path(path)
		path = File.expand_path(path)
		return Pathname.new(path).relative_path_from(@toplevel_dir).to_s
	end
	
	def mangle_path_array(array)
		array = array.dup
		array.each_with_index do |item, i|
			if item.is_a?(String)
				array[i] = mangle_path(item)
			end
		end
		return array
	end
	
	def mangle_path_or_path_array(item)
		case item
		when String
			return mangle_path(item)
		when Array
			return mangle_path_array(item)
		else
			return item
		end
	end
end

end # module RakeExtensions

Object.class_eval do
	include RakeExtensions
end

