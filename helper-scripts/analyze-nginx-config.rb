#!/usr/bin/env ruby
# Parses an Nginx config file, infers a list of web apps from it,
# and print a representation of that in JSON to an output file.

if ARGV.size != 4
	STDERR.puts "Usage: analyze-nginx-config.rb <RUBY_LIBDIR> <PASSENGER_ROOT> <NGINX_CONFIG_FILE> <OUTPUT>.json"
	exit 1
end

ruby_libdir, passenger_root, config_file, output_file = ARGV

$LOAD_PATH.unshift(ruby_libdir)
require 'phusion_passenger'
PhusionPassenger.locate_directories(passenger_root)

require 'phusion_passenger/treetop/runtime'
require 'phusion_passenger/nginx/nginx_config_file_parser'
require 'phusion_passenger/nginx/nginx_config_reader'
require 'phusion_passenger/nginx/config_options'
require 'phusion_passenger/nginx/app_list_inferer'

# From https://gist.github.com/mislav/1505877
module SimpleJsonGenerator
    def generate(obj)
      raise ArgumentError unless obj.is_a? Array or obj.is_a? Hash
      generate_type(obj)
    end
    alias dump generate
 
    private
 
    def generate_type(obj)
      type = obj.is_a?(Numeric) ? :Numeric : obj.class.name
      begin send(:"generate_#{type}", obj)
      rescue NoMethodError; raise ArgumentError, "can't serialize #{type}"
      end
    end
 
    ESC_MAP = Hash.new {|h,k| k }.update \
      "\r" => 'r',
      "\n" => 'n',
      "\f" => 'f',
      "\t" => 't',
      "\b" => 'b'
 
    def quote(str) %("#{str}") end
 
    def generate_String(str)
      quote str.gsub(/[\r\n\f\t\b"\\]/) { "\\#{ESC_MAP[$&]}"}
    end
 
    def generate_simple(obj) obj.inspect end
    alias generate_Numeric generate_simple
    alias generate_TrueClass generate_simple
    alias generate_FalseClass generate_simple
 
    def generate_Symbol(sym) generate_String(sym.to_s) end
 
    def generate_Time(time)
      quote time.strftime(time.utc? ? "%F %T UTC" : "%F %T %z")
    end
    def generate_Date(date) quote date.to_s end
 
    def generate_NilClass(*) 'null' end
 
    def generate_Array(ary) '[%s]' % ary.map {|o| generate_type(o) }.join(', ') end
 
    def generate_Hash(hash)
      '{%s}' % hash.map { |key, value|
        "#{generate_String(key.to_s)}: #{generate_type(value)}"
      }.join(', ')
    end

    extend self
end

result, aux = NginxConfigReader.new(config_file).start
case result
when :ok
	data = aux
	data = SimpleJsonGenerator.generate(AppListInferer.new(data).start)
	File.open(output_file, "w") do |f|
		f.write(data)
	end
when :parse_error
	abort aux.failure_reason
when :load_error
	abort aux.to_s
end