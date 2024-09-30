# encoding: utf-8
#
## Stupid small pure Ruby JSON parser & generator.
#
# Copyright © 2013 Mislav Marohnić
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of this
# software and associated documentation files (the “Software”), to deal in the Software
# without restriction, including without limitation the rights to use, copy, modify,
# merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to the following
# conditions:
#
# The above copyright notice and this permission notice shall be included in all copies or
# substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
# OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

# We use this in Phusion Passenger at places where we cannot depend on the JSON
# gem being available, for example in 'passenger start' before the RuntimeInstaller
# has run.

PhusionPassenger.require_passenger_lib 'utils/strscan'
require 'forwardable'

module PhusionPassenger
module Utils

# Usage:
#
#   JSON.parse(json_string) => Array/Hash
#   JSON.generate(object)   => json string
#
# Run tests by executing this file directly. Pipe standard input to the script to have it
# parsed as JSON and to display the result in Ruby.
#
class JSON
  def self.parse(data) new(data).parse end

  WSP = /(\s|\/\/.*?\n|\/\*.*?\*\/)+/m
  OBJ = /[{\[]/;    HEN = /\}/;  AEN = /\]/
  COL = /\s*:\s*/;  KEY = /\s*,\s*/
  NUM = /-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?/
  BOL = /true|false/;  NUL = /null/

  extend Forwardable

  attr_reader :scanner
  alias_method :s, :scanner
  def_delegators :scanner, :scan, :matched
  private :s, :scan, :matched

  def initialize data
    @scanner = PhusionPassenger::Utils::StringScanner.new data.to_s
  end

  def parse
    object
  end

  private

  def space() scan WSP end

  def endkey() scan(KEY) or space end

  def object
    matched == '{' ? hash : array if scan(OBJ)
  end

  def value
    object or string or
      scan(NUL) ? nil :
      scan(BOL) ? matched.size == 4:
      scan(NUM) ? eval(matched) :
      error
  end

  def hash
    obj = {}
    space
    repeat_until(HEN) do
      space
      k = string
      scan(COL)
      obj[k] = value
      endkey
    end
    obj
  end

  def array
    ary = []
    space
    repeat_until(AEN) do
      space
      ary << value
      endkey
    end
    ary
  end

  SPEC = {'b' => "\b", 'f' => "\f", 'n' => "\n", 'r' => "\r", 't' => "\t"}
  UNI = 'u'; CODE = /[a-fA-F0-9]{4}/
  STR = /"/; STE = '"'
  ESC = '\\'

  def string
    if scan(STR)
      str, esc = '', false
      while c = s.getch
        if esc
          str << (c == UNI ? (s.scan(CODE) || error).to_i(16).chr : SPEC[c] || c)
          esc = false
        else
          case c
          when ESC then esc = true
          when STE then break
          else str << c
          end
        end
      end
      str
    end
  end

  def error
    raise "parse error at: #{scan(/.{1,20}/m).inspect}"
  end

  def repeat_until reg
    until scan(reg)
      pos = s.pos
      yield
      error unless s.pos > pos
    end
  end

  module Generator
    def generate(obj)
      raise ArgumentError unless obj.is_a? Array or obj.is_a? Hash
      generate_type(obj)
    end
    alias dump generate

    private

    def generate_type(obj)
      type = obj.is_a?(Numeric) ? :Numeric : obj.class.name.split('::').last
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

    def generate_VersionComparer(vc)
      generate_String(vc)
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
  end

  extend Generator
end

end # module Utils
end # module PhusionPassenger
