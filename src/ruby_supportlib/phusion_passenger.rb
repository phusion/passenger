# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2018 Phusion Holding B.V.
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

module PhusionPassenger
  FILE_LOCATION = File.expand_path(__FILE__)


  ###### Names and version numbers ######

  PACKAGE_NAME = 'passenger'
  # Run 'rake src/cxx_supportlib/Constants.h configkit_schemas_inline_comments' after changing this number.
  VERSION_STRING = '5.3.7'

  PREFERRED_NGINX_VERSION = '1.14.0'
  NGINX_SHA256_CHECKSUM = '5d15becbf69aba1fe33f8d416d97edd95ea8919ea9ac519eff9bafebb6022cb5'

  # Packaging may be locked to an older version due to the specific module configuration being
  # incompatible with the version we prefer (latest stable).
  PACKAGING_PREFERRED_NGINX_VERSION = '1.14.0'
  PACKAGING_NGINX_SHA256_CHECKSUM = '5d15becbf69aba1fe33f8d416d97edd95ea8919ea9ac519eff9bafebb6022cb5'

  # sha256sum of the .tar.gz
  PREFERRED_PCRE_VERSION  = '8.42'
  PCRE_SHA256_CHECKSUM = '69acbc2fbdefb955d42a4c606dfde800c2885711d2979e356c0636efde9ec3b5'

  STANDALONE_INTERFACE_VERSION  = 1


  ###### Directories ######

  GLOBAL_NAMESPACE_DIRNAME_           = "passenger"
  # Subdirectory under $HOME to use for storing stuff.
  USER_NAMESPACE_DIRNAME_             = ".passenger"
  # The name for the /etc/apache2/mods-available/*.{load,conf} file.
  APACHE2_MODULE_CONF_NAME            = "passenger"

  # Directories in which to look for plugins.
  PLUGIN_DIRS = [
    "/usr/share/#{GLOBAL_NAMESPACE_DIRNAME_}/plugins",
    "/usr/local/share/#{GLOBAL_NAMESPACE_DIRNAME_}/plugins",
    "~/#{USER_NAMESPACE_DIRNAME_}/plugins"
  ]

  REQUIRED_LOCATIONS_INI_FIELDS = [
    # User-invoked commands
    :bin_dir,
    # Support binaries
    :support_binaries_dir,
    # Library files like libboost_oxt.a and various .o files
    :lib_dir,
    # Scripts not directly invoked by users
    :helper_scripts_dir,
    # Various non-executable resources
    :resources_dir,
    # C header files, necessary for compiling Nginx
    :include_dir,
    # Documentation
    :doc_dir,
    # Ruby support libraries
    :ruby_libdir,
    # Node.js support libraries
    :node_libdir,
    # Path to the compiled Apache module
    :apache2_module_path,
    # Directory containing the source code of our Ruby extension
    :ruby_extension_source_dir,
    # Directory containing the source code of our Nginx module
    :nginx_module_source_dir
  ].freeze
  OPTIONAL_LOCATIONS_INI_FIELDS = [
    # Directory which contains the main Phusion Passenger Rakefile. Only
    # available when originally packaged,
    :build_system_dir,
    # Directory in which downloaded Phusion Passenger binaries are cached.
    :download_cache_dir
  ].freeze

  # Follows the logic of src/cxx_supportlib/ResourceLocator.h, so don't forget to modify that too.
  def self.locate_directories(install_spec = nil)
    @install_spec = install_spec || infer_install_spec
    if @install_spec && File.file?(@install_spec)
      filename = @install_spec
      options  = parse_ini_file(filename)

      @custom_packaged = true
      @packaging_method = get_option(filename, options, 'packaging_method')
      REQUIRED_LOCATIONS_INI_FIELDS.each do |field|
        value = get_option(filename, options, field.to_s)
        value.freeze unless value.nil?
        instance_variable_set("@#{field}", value)
      end
      OPTIONAL_LOCATIONS_INI_FIELDS.each do |field|
        value = get_option(filename, options, field.to_s, false)
        value.freeze unless value.nil?
        instance_variable_set("@#{field}", value)
      end
    else
      source_root            = File.dirname(File.dirname(File.dirname(FILE_LOCATION)))
      @install_spec          = source_root
      @custom_packaged       = false
      @bin_dir               = "#{source_root}/bin".freeze
      @support_binaries_dir  = "#{source_root}/buildout/support-binaries".freeze
      @lib_dir               = "#{source_root}/buildout".freeze
      @helper_scripts_dir    = "#{source_root}/src/helper-scripts".freeze
      @resources_dir         = "#{source_root}/resources".freeze
      @include_dir           = "#{source_root}/src".freeze
      @doc_dir               = "#{source_root}/doc".freeze
      @ruby_libdir           = File.dirname(FILE_LOCATION).freeze
      @node_libdir           = "#{source_root}/src/nodejs_supportlib".freeze
      @apache2_module_path   = "#{source_root}/buildout/apache2/mod_passenger.so".freeze
      @ruby_extension_source_dir = "#{source_root}/src/ruby_native_extension".freeze
      @nginx_module_source_dir   = "#{source_root}/src/nginx_module".freeze
      @download_cache_dir        = "#{source_root}/download_cache".freeze
      @build_system_dir          = source_root.dup.freeze
      REQUIRED_LOCATIONS_INI_FIELDS.each do |field|
        if instance_variable_get("@#{field}").nil?
          raise "BUG: @#{field} not set"
        end
      end
    end
  end

  # Returns whether this Phusion Passenger installation is in the 'originally packaged'
  # configuration (as opposed to the 'custom packaged' configuration.
  def self.originally_packaged?
    return !@custom_packaged
  end

  def self.custom_packaged?
    return @custom_packaged
  end

  # If Phusion Passenger is custom packaged, returns which packaging
  # method was used. Can be 'deb', 'rpm', 'homebrew', 'test'
  # or 'unknown'.
  def self.packaging_method
    return @packaging_method
  end

  def self.packaging_method_description
    case packaging_method
    when "deb"
      "Debian packages"
    when "rpm"
      "RPM packages"
    when "homebrew"
      "Homebrew"
    else
      "gem or tarball"
    end
  end

  # Whether the current Phusion Passenger installation is installed
  # from a release package, e.g. an official gem or official tarball.
  # Returns false if e.g. the gem was built by the user, or if this
  # install is from a git repository.
  def self.installed_from_release_package?
    File.exist?("#{resources_dir}/release.txt")
  end

  # The installation specification string, as passed to #locate_directories.
  def self.install_spec
    return @install_spec
  end

  # Generate getters for the directory types in locations.ini.
  getters_code = ""
  (REQUIRED_LOCATIONS_INI_FIELDS + OPTIONAL_LOCATIONS_INI_FIELDS).each do |field|
    getters_code << %Q{
      def self.#{field}
        return @#{field}
      end
    }
  end
  eval(getters_code, binding, __FILE__, __LINE__)

  def self.user_support_binaries_dir
    return "#{home_dir}/#{USER_NAMESPACE_DIRNAME_}/support-binaries/#{VERSION_STRING}"
  end

  def self.find_support_binary(name)
    all_support_binary_dirs = [
      support_binaries_dir,
      user_support_binaries_dir
    ]
    all_support_binary_dirs.each do |dir|
      result = "#{dir}/#{name}"
      if File.exist?(result)
        return result
      end
    end
    return nil
  end


  ###### Other resource locations ######

  def self.binaries_sites
    [
      { :url => "https://github.com/phusion/passenger/releases/download/release-{{VERSION}}".freeze },
      { :url => "https://oss-binaries.phusionpassenger.com/binaries/passenger/by_release".freeze },
      { :url => "https://s3.amazonaws.com/phusion-passenger/binaries/passenger/by_release".freeze }
    ]
  end


  # Instead of calling `require 'phusion_passenger/foo'`, you should call
  # `PhusionPassenger.require_passenger_lib 'foo'`. This is because when Phusion
  # Passenger is natively packaged, it may still be run with arbitrary Ruby
  # interpreters. Adding ruby_libdir to $LOAD_PATH is then dangerous because ruby_libdir
  # may be the distribution's Ruby's vendor_ruby directory, which may be incompatible
  # with the active Ruby interpreter. This method looks up the exact filename directly.
  #
  # Using this method also has two more advantages:
  #
  #  1. It is immune to Bundler's load path mangling code.
  #  2. It is faster than plan require() because it doesn't need to
  #     scan the entire load path.
  def self.require_passenger_lib(name)
    require("#{ruby_libdir}/phusion_passenger/#{name}")
  end

  # Forward define public_api.rb methods for code that tries to hook
  # Passenger events before an app is spawned (such as mongoid)
  def self.on_event(name, &block)
    # The definition in `public_api.rb` will override this implementation when
    # a Ruby application is spawned.
    nil
  end

  def self.call_event(name, *args)
    # The definition in `public_api.rb` will override this implementation when
    # a Ruby application is spawned.
    nil
  end


private
  def self.infer_install_spec
    filename = ENV['PASSENGER_LOCATION_CONFIGURATION_FILE']
    return filename if filename && !filename.empty?

    filename = File.dirname(FILE_LOCATION) + "/phusion_passenger/locations.ini"
    return filename if filename && File.exist?(filename)

    require 'etc' if !defined?(Etc)
    begin
      home_dir = Etc.getpwuid(Process.uid).dir
    rescue ArgumentError
      # Unknown user.
      home_dir = ENV['HOME']
    end
    if home_dir && !home_dir.empty?
      filename = "#{home_dir}/#{USER_NAMESPACE_DIRNAME_}/locations.ini"
      return filename if File.exist?(filename)
    end

    filename = "/etc/#{GLOBAL_NAMESPACE_DIRNAME_}/locations.ini"
    return filename if File.exist?(filename)

    return nil
  end

  def self.parse_ini_file(filename)
    options  = {}
    in_locations_section = false
    File.open(filename, 'r') do |f|
      while !f.eof?
        line = f.readline
        line.strip!
        next if line.empty?
        if line =~ /\A\[(.+)\]\Z/
          in_locations_section = $1 == 'locations'
        elsif in_locations_section && line =~ /=/
          key, value = line.split(/ *= */, 2)
          options[key.freeze] = value.freeze
        end
      end
    end
    return options
  end

  def self.get_option(filename, options, key, required = true)
    value = options[key]
    if value
      return value
    elsif required
      raise "Option '#{key}' missing in file '#{filename}'"
    else
      return nil
    end
  end

  def self.get_bool_option(filename, options, key)
    value = get_option(filename, options, key)
    return value == 'yes' || value == 'true' || value == 'on' || value == '1'
  end

  # The HOME environment variable is often unreliable, because for
  # example `sudo` preserves it. That's why we don't respect it by
  # default.
  def self.home_dir(respect_home_env = false)
    if respect_home_env
      home = ENV['HOME'].to_s
    end
    if home.nil? || home.empty?
      require 'etc' if !defined?(Etc)
      begin
        home = Etc.getpwuid(Process.uid).dir
      rescue ArgumentError
        # Unknown user.
        home = ENV['HOME']
      end
    end
    return home
  end
end if !defined?(PhusionPassenger::VERSION_STRING)
