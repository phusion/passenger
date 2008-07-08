#!/usr/bin/env ruby
# This script runs all RSpec tests. It only exists because RubyGems
# requires executable Ruby scripts as unit tests, and RSpec tests
# must be run via the 'spec' tool.
require 'rubygems'
gem 'rspec', '>= 1.1.2'
Dir.chdir("#{File.dirname(__FILE__)}/..")
Object.__send__(:remove_const, 'ARGV')
ARGV = Dir['*_spec.rb']
load 'spec'
