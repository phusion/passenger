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

require 'strscan'
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
    @scanner = StringScanner.new data.to_s
  end

  def parse
    space
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
  end

  extend Generator
end

if __FILE__ == $0
if !$stdin.tty?
  data = JSON.parse $stdin.read
  require 'pp'
  pp data
else
  require 'test/unit'
  require 'date'
  class ParserTest < Test::Unit::TestCase
    PARSED = JSON.parse DATA.read
    def parsed() PARSED end
    def parse_string(str) JSON.parse(%(["#{str}"]).gsub('\\\\', '\\')).first end
    def test_string
      assert_equal "Pagination library for \"Rails 3\", Sinatra, Merb, DataMapper, and more",
        parsed['head']['repository']['description']
    end
    def test_string_specials
      assert_equal "\r\n\t\f\b", parse_string('\r\n\t\f\b')
      assert_equal "aA", parse_string('\u0061\u0041')
      assert_equal "\e", parse_string('\u001B')
      assert_equal "xyz", parse_string('\x\y\z')
      assert_equal '"\\/', parse_string('\"\\\\\\/')
      assert_equal 'no #{interpolation}', parse_string('no #{interpolation}')
    end
    def test_hash
      assert_equal %w[label ref repository sha user], parsed['head'].keys.sort
    end
    def test_number
      assert_equal 124.3e2, parsed['head']['repository']['size']
    end
    def test_bool
      assert_equal true, parsed['head']['repository']['fork']
      assert_equal false, parsed['head']['repository']['private']
    end
    def test_nil
      assert_nil parsed['head']['user']['company']
    end
    def test_array
      assert_equal ["4438f", {"a" => "b"}], parsed['head']['sha']
    end
    def test_invalid
      assert_raises(RuntimeError) { JSON.parse %({) }
      assert_raises(RuntimeError) { JSON.parse %({ "foo": }) }
      assert_raises(RuntimeError) { JSON.parse %([ "foo": "bar" ]) }
      assert_raises(RuntimeError) { JSON.parse %([ ~"foo" ]) }
      assert_raises(RuntimeError) { JSON.parse %([ "foo ]) }
      assert_raises(RuntimeError) { JSON.parse %([ "foo\\" ]) }
      assert_raises(RuntimeError) { JSON.parse %([ "foo\\uabGd" ]) }
    end
    def test_single_line_comments
      source = %Q{
        // comment before document
        {
          // comment
          "foo": "1",
          "bar": "2",
          // another comment
          "baz": "3",
          "array": [
            // comment inside array
            1, 2, 3
            // comment at end of array
          ]
          // comment at end of hash
        }
        // comment after document
      }
      doc = { "foo" => "1", "bar" => "2", "baz" => "3", "array" => [1, 2, 3] }
      assert_equal(doc, JSON.parse(source))
    end
    def test_multi_line_comments
      source = %Q{
        /* comment before
         * document */
        {
          /* comment */
          "foo": "1",
          "bar": "2",
          /* another
             comment
           */
          "baz": "3",
          "array": [
            /* comment inside array */
            1, 2, 3,
            4, /* comment inside an array */ 5,
            /*
               // "nested" comments
               { "faux json": "inside comment" }
             */
            6, 7
            /**
             * comment at end of array
             */
          ]
          /**************************
           comment at end of hash
           **************************/
        }
        /* comment after
           document */
      }
      doc = { "foo" => "1", "bar" => "2", "baz" => "3", "array" => [1, 2, 3, 4, 5, 6, 7] }
      assert_equal(doc, JSON.parse(source))
    end
  end

  class GeneratorTest < Test::Unit::TestCase
    def generate(obj) JSON.generate(obj) end
    def test_array
      assert_equal %([1, 2, 3]), generate([1, 2, 3])
    end
    def test_bool
      assert_equal %([true, false]), generate([true, false])
    end
    def test_null
      assert_equal %([null]), generate([nil])
    end
    def test_string
      assert_equal %(["abc\\n123"]), generate(["abc\n123"])
    end
    def test_string_unicode
      assert_equal %(["ć\\"č\\nž\\tš\\\\đ"]), generate(["ć\"č\nž\tš\\đ"])
    end
    def test_time
      time = Time.utc(2012, 04, 19, 1, 2, 3)
      assert_equal %(["2012-04-19 01:02:03 UTC"]), generate([time])
    end
    def test_date
      time = Date.new(2012, 04, 19)
      assert_equal %(["2012-04-19"]), generate([time])
    end
    def test_symbol
      assert_equal %(["abc"]), generate([:abc])
    end
    def test_hash
      json = generate(:abc => 123, 123 => 'abc')
      assert_match /^\{/, json
      assert_match /\}$/, json
      assert_equal [%("123": "abc"), %("abc": 123)], json[1...-1].split(', ').sort
    end
    def test_nested_structure
      json = generate(:hash => {1=>2}, :array => [1,2])
      assert json.include?(%("hash": {"1": 2}))
      assert json.include?(%("array": [1, 2]))
    end
    def test_invalid_json
      assert_raises(ArgumentError) { generate("abc") }
    end
    def test_invalid_object
      err = assert_raises(ArgumentError) { generate("a" => Object.new) }
      assert_equal "can't serialize Object", err.message
    end
  end
end
end

end # module Utils
end # module PhusionPassenger

__END__
{
  "head": {
    "ref": "master",
    "repository": {
      "forks": 0,
      "integrate_branch": "rails3",
      "watchers": 1,
      "language": "Ruby",
      "description": "Pagination library for \"Rails 3\", Sinatra, Merb, DataMapper, and more",
      "has_downloads": true,
      "fork": true,
      "created_at": "2011/10/24 03:20:48 -0700",
      "homepage": "http://github.com/mislav/will_paginate/wikis",
      "size": 124.3e2,
      "private": false,
      "has_wiki": true,
      "name": "will_paginate",
      "owner": "dbackeus",
      "url": "https://github.com/dbackeus/will_paginate",
      "has_issues": false,
      "open_issues": 0,
      "pushed_at": "2011/10/25 05:44:05 -0700"
    },
    "label": "dbackeus:master",
    "sha": ["4438f", { "a" : "b" }],
    "user": {
      "name": "David Backeus",
      "company": null,
      "gravatar_id": "ebe96524f5db9e92188f0542dc9d1d1a",
      "location": "Stockholm (Sweden)",
      "type": "User",
      "login": "dbackeus"
    }
  }
}