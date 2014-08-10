#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2012 Phusion
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

# This file lists all the Phusion Passenger C++ library files and contains
# code for calculating how to compile and how to link them into executables.
# It's used by the build system (build/*.rb) and
# lib/phusion_passenger/standalone/runtime_installer.rb.

class CommonLibraryBuilder
	include Rake::DSL if defined?(Rake::DSL)

	attr_reader :all_components, :selected_components, :output_dir

	def initialize(&block)
		@all_components = {}
		@all_ordered_components = []
		@selected_components = {}
		@namespace = "common"
		if defined?(COMMON_OUTPUT_DIR)
			@output_dir = COMMON_OUTPUT_DIR + "libpassenger_common"
		else
			@output_dir = "."
		end
		instance_eval(&block) if block
	end

	def initialize_copy(other)
		[:all_components, :all_ordered_components, :selected_components, :namespace, :output_dir].each do |name|
			var_name = "@#{name}"
			instance_variable_set(var_name, other.instance_variable_get(var_name).dup)
		end
	end

	def define_component(object_name, options)
		options[:deps] ||= []
		@all_components[object_name] = options
		@all_ordered_components << object_name
		@selected_components[object_name] = options
	end

	def only(*selector)
		return dup.send(:only!, *selector)
	end

	def exclude(*selector)
		return dup.send(:exclude!, *selector)
	end

	def set_namespace(namespace)
		return dup.send(:set_namespace!, namespace)
	end

	def set_output_dir(dir)
		return dup.send(:set_output_dir!, dir)
	end

	def link_objects
		result = []

		selected_categories.each do |category|
			if category_complete?(category) && false
				# Feature disabled: we don't want to waste too much space when
				# packaging the runtime ('passenger package-runtime') so we
				# never generate static libraries.
				if aggregate_sources?
					result << "#{@output_dir}/#{category}.o"
				else
					result << "#{@output_dir}/#{category}.a"
				end
			else
				object_names = selected_objects_beloging_to_category(category)
				result.concat(object_filenames_for(object_names))
			end
		end

		return result
	end

	def link_objects_as_string
		return link_objects.join(' ')
	end

	def define_tasks(extra_compiler_flags = nil)
		flags =  "-Iext -Iext/common #{LIBEV_CFLAGS} #{extra_compiler_flags} "
		cflags = (flags + EXTRA_CFLAGS).strip
		cxxflags = (flags + EXTRA_CXXFLAGS).strip

		group_all_components_by_category.each_pair do |category, object_names|
			define_category_tasks(category, object_names, cflags, cxxflags)
		end

		task("#{@namespace}:clean") do
			sh "rm -rf #{@output_dir}"
		end

		return self
	end

private
	def define_category_tasks(category, object_names, cflags, cxxflags)
		object_filenames = object_filenames_for(object_names)

		object_names.each do |object_name|
			options     = @all_components[object_name]
			source_file = "ext/common/#{options[:source]}"
			object_file = "#{@output_dir}/#{object_name}"

			file(object_file => dependencies_for(options)) do
				ensure_directory_exists(File.dirname(object_file))
				if source_file =~ /\.c$/
					compile_c(source_file, "#{cflags} -o #{object_file}")
				else
					compile_cxx(source_file, "#{cxxflags} -o #{object_file}")
				end
			end
		end

		task "#{@namespace}:clean" do
			sh "rm -f #{object_filenames.join(' ')}"
		end

		if aggregate_sources?
			aggregate_source = "#{@output_dir}/#{category}.cpp"
			aggregate_object = "#{@output_dir}/#{category}.o"

			file(aggregate_object => dependencies_for(object_names)) do
				ensure_directory_exists(File.dirname(aggregate_source))
				ensure_directory_exists(File.dirname(aggregate_object))

				File.open(aggregate_source, "w") do |f|
					f.puts %q{
						#ifndef _GNU_SOURCE
							#define _GNU_SOURCE
						#endif
					}
					object_names.each do |object_name|
						options = @all_components[object_name]
						source_file = options[:source].sub(%r(^ext/common), '')
						f.puts "#include \"#{source_file}\""
					end
				end

				compile_cxx(aggregate_source, "#{flags} -o #{aggregate_object}")
			end

			task "#{@namespace}:clean" do
				sh "rm -f #{aggregate_source} #{aggregate_object}"
			end
		elsif false
			# Feature disabled: we don't want to waste too much space when
			# packaging the runtime ('passenger package-runtime') so we
			# never generate static libraries.
			library = "#{@output_dir}/#{category}.a"
			
			file(library => object_filenames) do
				create_static_library(library, object_filenames.join(' '))
			end

			task "#{@namespace}:clean" do
				sh "rm -f #{library}"
			end
		end
	end

	def set_namespace!(namespace)
		@namespace = namespace
		return self
	end

	def set_output_dir!(dir)
		@output_dir = dir
		return self
	end

	def only!(*selector)
		new_components = apply_selector(*selector)
		@selected_components = new_components
		return self
	end

	def exclude!(*selector)
		apply_selector(*selector).each_key do |object_name|
			@selected_components.delete(object_name)
		end
		return self
	end

	def apply_selector(*selector)
		result = {}
		selector = [selector].flatten
		selector.each do |condition|
			@selected_components.each do |object_name, options|
				if component_satisfies_condition?(object_name, options, condition)
					result[object_name] = options
				end
			end
		end
		return result
	end

	def component_satisfies_condition?(object_name, options, condition)
		case condition
		when Symbol
			return condition == :all || options[:category] == condition
		when String
			return object_name == condition
		else
			raise ArgumentError, "Invalid condition #{condition.inspect}"
		end
	end

	def ensure_directory_exists(dir)
		sh("mkdir -p #{dir}") if !File.directory?(dir)
	end

	def selected_categories
		categories = {}
		@selected_components.each_value do |options|
			categories[options[:category]] = true
		end
		return categories.keys
	end

	def category_complete?(category)
		expected = 0
		actual   = 0
		@all_components.each_value do |options|
			if options[:category] == category
				expected += 1
			end
		end
		@selected_components.each_value do |options|
			if options[:category] == category
				actual += 1
			end
		end
		return expected == actual
	end

	def selected_objects_beloging_to_category(category)
		result = []
		@selected_components.each_pair do |object_name, options|
			if options[:category] == category
				result << object_name
			end
		end
		return result
	end

	def dependencies_for(component_options_or_object_names)
		result = nil
		case component_options_or_object_names
		when Hash
			component_options = component_options_or_object_names
			result = ["ext/common/#{component_options[:source]}"]
			component_options[:deps].each do |dependency|
				result << "ext/common/#{dependency}"
			end
		when Array
			result = []
			object_names = component_options_or_object_names
			object_names.each do |object_name|
				options = @all_components[object_name]
				result.concat(dependencies_for(options))
			end
			result.uniq!
		end
		return result
	end

	def object_filenames_for(object_names)
		return object_names.map { |name| "#{@output_dir}/#{name}" }
	end

	def group_all_components_by_category
		categories = {}
		@all_ordered_components.each do |object_name|
			options  = @all_components[object_name]
			category = options[:category]
			categories[category] ||= []
			categories[category] << object_name
		end
		return categories
	end

	def aggregate_sources?
		# Feature disabled: it's too hard to make it work because
		# lots of executables have to be linked to individual objects
		# anyway.
		return false
	end
end


COMMON_LIBRARY = CommonLibraryBuilder.new do
	define_component 'Logging.o',
		:source   => 'Logging.cpp',
		:category => :base,
		:deps     => %w(
			Logging.cpp
			Logging.h
		)
	define_component 'Exceptions.o',
		:source   => 'Exceptions.cpp',
		:category => :base,
		:deps     => %w(
			Exceptions.h
		)
	define_component 'Utils/SystemTime.o',
		:source   => 'Utils/SystemTime.cpp',
		:category => :base,
		:deps     => %w(
			Utils/SystemTime.h
		)
	define_component 'Utils/StrIntUtils.o',
		:source   => 'Utils/StrIntUtils.cpp',
		:category => :base,
		:deps     => %w(
			Utils/StrIntUtils.h
		)
	define_component 'Utils/IOUtils.o',
		:source   => 'Utils/IOUtils.cpp',
		:category => :base,
		:deps     => %w(
			Utils/IOUtils.h
		)
	define_component 'Utils.o',
		:source   => 'Utils.cpp',
		:category => :base,
		:deps     => %w(
			Utils.h
			Utils/Base64.h
			Utils/StrIntUtils.h
			ResourceLocator.h
		)

	define_component 'Utils/Base64.o',
		:source   => 'Utils/Base64.cpp',
		:category => :other,
		:deps     => %w(
			Utils/Base64.h
		)
	define_component 'Utils/CachedFileStat.o',
		:source   => 'Utils/CachedFileStat.cpp',
		:category => :other,
		:deps     => %w(
			Utils/CachedFileStat.h
			Utils/CachedFileStat.hpp
		)
	define_component 'Utils/LargeFiles.o',
		:source   => 'Utils/LargeFiles.cpp',
		:category => :other,
		:deps     => %w(
			Utils/LargeFiles.h
		)
	define_component 'ApplicationPool2/Implementation.o',
		:source   => 'ApplicationPool2/Implementation.cpp',
		:category => :other,
		:deps     => %w(
			ApplicationPool2/Spawner.h
			ApplicationPool2/Common.h
			ApplicationPool2/Pool.h
			ApplicationPool2/SuperGroup.h
			ApplicationPool2/Group.h
			ApplicationPool2/Process.h
			ApplicationPool2/Socket.h
			ApplicationPool2/Session.h
			ApplicationPool2/Options.h
			ApplicationPool2/PipeWatcher.h
			ApplicationPool2/AppTypes.h
			ApplicationPool2/Spawner.h
			ApplicationPool2/SpawnerFactory.h
			ApplicationPool2/SmartSpawner.h
			ApplicationPool2/DirectSpawner.h
			ApplicationPool2/DummySpawner.h
		)
	define_component 'ApplicationPool2/AppTypes.o',
		:source   => 'ApplicationPool2/AppTypes.cpp',
		:category => :other,
		:deps     => %w(
			ApplicationPool2/AppTypes.h
			Utils/StrIntUtils.h
			Utils/CachedFileStat.h
		)
	define_component 'AgentsStarter.o',
		:source   => 'AgentsStarter.cpp',
		:category => :other,
		:deps     => %w(
			AgentsStarter.h
			ResourceLocator.h
			MessageClient.h
			ServerInstanceDir.h
			Utils/IniFile.h
			Utils/VariantMap.h
		)
	define_component 'AgentsBase.o',
		:source   => 'agents/Base.cpp',
		:category => :other,
		:deps     => %w(
			agents/Base.h
			Utils/VariantMap.h
		)
	define_component 'agents/LoggingAgent/FilterSupport.o',
		:source   => 'agents/LoggingAgent/FilterSupport.cpp',
		:category => :logging_agent,
		:deps     => %w(
			agents/LoggingAgent/FilterSupport.h
		)
	define_component 'Utils/MD5.o',
		:source   => 'Utils/MD5.cpp',
		:category => :other,
		:deps     => %w(
			Utils/MD5.h
		)
	define_component 'Utils/fib.o',
		:source   => 'Utils/fib.c',
		:category => :other,
		:deps     => %w(
			Utils/fib.h
			Utils/fibpriv.h
		)
	define_component 'Utils/jsoncpp.o',
		:source   => 'Utils/jsoncpp.cpp',
		:category => :other,
		:deps     => %w(
			Utils/json.h
			Utils/json-forwards.h
		)

	#'BCrypt.o' => %w(
	#	BCrypt.cpp
	#	BCrypt.h
	#	Blowfish.h
	#	Blowfish.c)
end

# Objects that must be linked into the Nginx binary.
NGINX_LIBS_SELECTOR = [:base, 'AgentsStarter.o', 'ApplicationPool2/AppTypes.o',
	'Utils/CachedFileStat.o', 'Utils/Base64.o', 'agents/LoggingAgent/FilterSupport.o']
