#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2012-2015 Phusion Holding B.V.
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

# This file lists all the Phusion Passenger C++ support library files and
# contains code for calculating how to compile and how to link them into
# executables. It's used by the build system (build/*.rb) and by
# src/ruby_supportlib/phusion_passenger/config/nginx_engine_compiler.rb

PhusionPassenger.require_passenger_lib 'platform_info/crypto'

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
    @all_components[object_name] = options
    @all_ordered_components << object_name
    @selected_components[object_name] = options
  end

  def only(*selector)
    dup.send(:only!, *selector)
  end

  def exclude(*selector)
    dup.send(:exclude!, *selector)
  end

  def set_namespace(namespace)
    dup.send(:set_namespace!, namespace)
  end

  def set_output_dir(dir)
    dup.send(:set_output_dir!, dir)
  end

  def link_objects
    result = []

    selected_categories.each do |category|
      object_names = selected_objects_belonging_to_category(category)
      result.concat(object_filenames_for(object_names))
    end

    result
  end

  def link_objects_as_string
    link_objects.join(' ')
  end

  def enable_optimizations!(lto = false)
    @default_optimization_level = "-O"
    if lto
      @default_extra_optimization_flags = "-flto"
    end
  end

  def define_tasks(extra_compiler_flags = nil)
    group_all_components_by_category.each_pair do |category, object_names|
      define_category_tasks(category, object_names, extra_compiler_flags)
    end

    task("#{@namespace}:clean") do
      sh "rm -rf #{@output_dir}"
    end

    self
  end

private
  def define_category_tasks(category, object_names, extra_compiler_flags)
    object_names.each do |object_name|
      define_object_compilation_task(object_name, extra_compiler_flags)
    end

    object_filenames = object_filenames_for(object_names)
    task "#{@namespace}:clean" do
      sh "rm -f #{object_filenames.join(' ')}"
    end
  end

  def define_object_compilation_task(object_name, extra_compiler_flags)
    options     = @all_components[object_name]
    source_file = locate_source_file(options[:source])
    object_file = "#{@output_dir}/#{object_name}"

    case options[:optimize]
    when :light
      optimization_level = "-O"
    when true, :heavy
      optimization_level = "-O2"
    when :very_heavy
      optimization_level = "-O3"
    when nil
      optimization_level = @default_optimization_level
    else
      raise "Unknown optimization level #{options[:optimize]}"
    end

    optimize = "#{optimization_level} #{@default_extra_optimization_flags}".strip

    if options[:strict_aliasing] == false # and not nil
      optimize = "#{optimize} -fno-strict-aliasing"
      # Disable link-time optimization so that we can no-strict-aliasing
      # works: http://stackoverflow.com/a/25765338/20816
      optimize.sub!(/-flto/, "")
    end

    extra_compiler_flags = "#{extra_compiler_flags} #{options[:cflags]}".strip

    define_c_or_cxx_object_compilation_task(
      object_file,
      source_file,
      :include_paths => CXX_SUPPORTLIB_INCLUDE_PATHS,
      :flags => [
        LIBEV_CFLAGS,
        LIBUV_CFLAGS,
        optimize,
        extra_compiler_flags
      ]
    )
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

  def selected_objects_belonging_to_category(category)
    result = []
    @selected_components.each_pair do |object_name, options|
      if options[:category] == category
        result << object_name
      end
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

  def locate_source_file(path)
    "src/cxx_supportlib/#{path}"
  end
end


COMMON_LIBRARY = CommonLibraryBuilder.new do
  define_component 'Logging.o',
    :source   => 'Logging.cpp',
    :category => :base,
    :optimize => :light
  define_component 'Exceptions.o',
    :source   => 'Exceptions.cpp',
    :category => :base
  define_component 'Utils/SystemTime.o',
    :source   => 'Utils/SystemTime.cpp',
    :category => :base
  define_component 'Utils/StrIntUtils.o',
    :source   => 'Utils/StrIntUtils.cpp',
    :category => :base,
    :optimize => :very_heavy
  define_component 'Utils/StrIntUtilsNoStrictAliasing.o',
    :source   => 'Utils/StrIntUtilsNoStrictAliasing.cpp',
    :category => :base,
    # Compiling with -O3 causes segfaults on RHEL 6
    :optimize => :heavy,
    :strict_aliasing => false
  define_component 'Utils/IOUtils.o',
    :source   => 'Utils/IOUtils.cpp',
    :optimize => :light,
    :category => :base
  define_component 'Utils.o',
    :source   => 'Utils.cpp',
    :category => :base
  define_component 'Crypto.o',
    :source   => 'Crypto.cpp',
    :category => :other,
    :cflags   => PhusionPassenger::PlatformInfo.crypto_extra_cflags
  define_component 'Utils/CachedFileStat.o',
    :source   => 'Utils/CachedFileStat.cpp',
    :category => :other
  define_component 'Utils/LargeFiles.o',
    :source   => 'Utils/LargeFiles.cpp',
    :category => :other
  define_component 'WatchdogLauncher.o',
    :source   => 'WatchdogLauncher.cpp',
    :category => :other
  define_component 'MemoryKit/mbuf.o',
    :source   => 'MemoryKit/mbuf.cpp',
    :category => :other,
    :optimize => true
  define_component 'MemoryKit/palloc.o',
    :source   => 'MemoryKit/palloc.cpp',
    :category => :other,
    :optimize => true
  define_component 'ServerKit/http_parser.o',
    :source   => 'ServerKit/http_parser.cpp',
    :category => :other,
    :optimize => :very_heavy
  define_component 'ServerKit/Implementation.o',
    :source   => 'ServerKit/Implementation.cpp',
    :category => :other,
    :optimize => true
  define_component 'DataStructures/LString.o',
    :source   => 'DataStructures/LString.cpp',
    :category => :other

  define_component 'Utils/Hasher.o',
    :source   => 'Utils/Hasher.cpp',
    :category => :other,
    :optimize => :very_heavy
  define_component 'AppTypes.o',
    :source   => 'AppTypes.cpp',
    :category => :other

  define_component 'jsoncpp.o',
    :source   => 'vendor-modified/jsoncpp/jsoncpp.cpp',
    :category => :json,
    :optimize => true
  define_component 'vendor-modified/modp_b64.o',
    :source   => 'vendor-modified/modp_b64.cpp',
    :category => :bas64,
    :optimize => true,
    :strict_aliasing => false
  define_component 'vendor-modified/modp_b64_strict_aliasing.o',
    :source   => 'vendor-modified/modp_b64_strict_aliasing.cpp',
    :category => :bas64,
    :optimize => true
  define_component 'UnionStationFilterSupport.o',
    :source   => 'UnionStationFilterSupport.cpp',
    :category => :union_station_filter
end

# A subset of the objects are linked to the Nginx binary. This defines
# what those objects are.
NGINX_LIBS_SELECTOR = [:base, 'WatchdogLauncher.o', 'AppTypes.o',
  'Utils/CachedFileStat.o', 'UnionStationFilterSupport.o']
