# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013-2015 Phusion
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

class Preprocessor
  def initialize
    require 'erb' if !defined?(ERB)
  end

  def start(filename, output_filename, variables = {})
    if output_filename
      temp_output_filename = "#{output_filename}._new"
      output = File.open(temp_output_filename, 'w')
    else
      output = STDOUT
    end
    the_binding  = create_binding(variables)

    erb = ERB.new(File.read(filename), nil, "-")
    erb.filename = filename
    output.write(erb.result(the_binding))
  ensure
    if output_filename && output
      output.close
      stat = File.stat(filename)
      File.chmod(stat.mode, temp_output_filename)
      File.chown(stat.uid, stat.gid, temp_output_filename) rescue nil
      File.rename(temp_output_filename, output_filename)
    end
  end

private
  UBUNTU_DISTRIBUTIONS = {
    "lucid"    => "10.04",
    "maverick" => "10.10",
    "natty"    => "11.04",
    "oneiric"  => "11.10",
    "precise"  => "12.04",
    "quantal"  => "12.10",
    "raring"   => "13.04",
    "saucy"    => "13.10",
    "trusty"   => "14.04",
    "utopic"   => "14.10",
    "vivid"    => "15.05"
  }
  DEBIAN_DISTRIBUTIONS = {
    "squeeze"  => "20110206",
    "wheezy"   => "20130504",
    "jessie"   => "20140000"
  }
  REDHAT_ENTERPRISE_DISTRIBUTIONS = {
    "el6"      => "el6.0"
  }
  AMAZON_DISTRIBUTIONS = {
    "amazon"   => "amazon"
  }

  # Provides the DSL that's accessible within.
  class Evaluator
    def _infer_distro_table(name)
      if UBUNTU_DISTRIBUTIONS.has_key?(name)
        return UBUNTU_DISTRIBUTIONS
      elsif DEBIAN_DISTRIBUTIONS.has_key?(name)
        return DEBIAN_DISTRIBUTIONS
      elsif REDHAT_ENTERPRISE_DISTRIBUTIONS.has_key?(name)
        return REDHAT_ENTERPRISE_DISTRIBUTIONS
      elsif AMAZON_DISTRIBUTIONS.has_key?(name)
        return AMAZON_DISTRIBUTIONS
      end
    end

    def is_distribution?(expr)
      if @distribution.nil?
        raise "The :distribution variable must be set"
      else
        if expr =~ /^(>=|>|<=|<|==|\!=)[\s]*(.+)/
          comparator = $1
          name = $2
        else
          raise "Invalid expression #{expr.inspect}"
        end

        table1 = _infer_distro_table(@distribution)
        table2 = _infer_distro_table(name)
        raise "Distribution name #{@distribution.inspect} not recognized" if !table1
        raise "Distribution name #{name.inspect} not recognized" if !table2
        return false if table1 != table2
        v1 = table1[@distribution]
        v2 = table2[name]

        case comparator
        when ">"
          return v1 > v2
        when ">="
          return v1 >= v2
        when "<"
          return v1 < v2
        when "<="
          return v1 <= v2
        when "=="
          return v1 == v2
        when "!="
          return v1 != v2
        else
          raise "BUG"
        end
      end
    end
  end

  def create_binding(variables)
    object = Evaluator.new
    variables.each_pair do |key, val|
      object.send(:instance_variable_set, "@#{key}", val)
    end
    return object.instance_eval do
      binding
    end
  end
end
