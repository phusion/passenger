require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'date'
PhusionPassenger.require_passenger_lib 'utils/json'

module PhusionPassenger

describe Utils::JSON do
  DATA = %q{{
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
}}
  PARSED = Utils::JSON.parse(DATA)

  def parse_string(str)
    Utils::JSON.parse(%(["#{str}"]).gsub('\\\\', '\\')).first
  end

  specify 'full test' do
    expect(PARSED).to eq(
      "head" => {
        "ref" => "master",
        "repository" => {
          "forks" => 0,
          "integrate_branch" => "rails3",
          "watchers" => 1,
          "language" => "Ruby",
          "description" => "Pagination library for \"Rails 3\", Sinatra, Merb, DataMapper, and more",
          "has_downloads" => true,
          "fork" => true,
          "created_at" => "2011/10/24 03:20:48 -0700",
          "homepage" => "http://github.com/mislav/will_paginate/wikis",
          "size" => 124.3e2,
          "private" => false,
          "has_wiki" => true,
          "name" => "will_paginate",
          "owner" => "dbackeus",
          "url" => "https://github.com/dbackeus/will_paginate",
          "has_issues" => false,
          "open_issues" => 0,
          "pushed_at" => "2011/10/25 05:44:05 -0700"
        },
        "label" => "dbackeus:master",
        "sha" => ["4438f", { "a"  => "b" }],
        "user" => {
          "name" => "David Backeus",
          "company" => nil,
          "gravatar_id" => "ebe96524f5db9e92188f0542dc9d1d1a",
          "location" => "Stockholm (Sweden)",
          "type" => "User",
          "login" => "dbackeus"
        }
      }
    )
  end

  specify 'string' do
    expect(PARSED['head']['repository']['description']).to eq("Pagination library for \"Rails 3\", Sinatra, Merb, DataMapper, and more")
  end

  specify 'string specials' do
    expect(parse_string('\r\n\t\f\b')).to eq("\r\n\t\f\b")
    expect(parse_string('\u0061\u0041')).to eq("aA")
    expect(parse_string('\u001B')).to eq("\e")
    expect(parse_string('\x\y\z')).to eq("xyz")
    expect(parse_string('\"\\\\\\/')).to eq('"\\/')
    expect(parse_string('no #{interpolation}')).to eq('no #{interpolation}')
  end

  specify 'hash' do
    expect(PARSED['head'].keys.sort). to eq(%w[label ref repository sha user])
  end

  specify 'number' do
    expect(PARSED['head']['repository']['size']).to eq(124.3e2)
  end

  specify 'bool' do
    expect(PARSED['head']['repository']['fork']).to be(true)
    expect(PARSED['head']['repository']['private']).to be(false)
  end

  specify 'nil' do
    expect(PARSED['head']['user']['company']).to be_nil
  end

  specify 'array' do
    expect(PARSED['head']['sha']).to eq(["4438f", {"a" => "b"}])
  end

  specify 'invalid' do
    expect { Utils::JSON.parse %({) }.to raise_error(RuntimeError)
    expect { Utils::JSON.parse %({ "foo": }) }.to raise_error(RuntimeError)
    expect { Utils::JSON.parse %([ "foo": "bar" ]) }.to raise_error(RuntimeError)
    expect { Utils::JSON.parse %([ ~"foo" ]) }.to raise_error(RuntimeError)
    expect { Utils::JSON.parse %([ "foo ]) }.to raise_error(RuntimeError)
    expect { Utils::JSON.parse %([ "foo\\" ]) }.to raise_error(RuntimeError)
    expect { Utils::JSON.parse %([ "foo\\uabGd" ]) }.to raise_error(RuntimeError)
  end

  # specify 'single line comments' do
  #   source = %Q{
  #     // comment before document
  #     {
  #       // comment
  #       "foo": "1",
  #       "bar": "2",
  #       // another comment
  #       "baz": "3",
  #       "array": [
  #         // comment inside array
  #         1, 2, 3
  #         // comment at end of array
  #       ]
  #       // comment at end of hash
  #     }
  #     // comment after document
  #   }
  #   doc = { "foo" => "1", "bar" => "2", "baz" => "3", "array" => [1, 2, 3] }
  #   expect(Utils::JSON.parse(source)).to eq(doc)
  # end

  # specify 'multi-line comments' do
  #   source = %Q{
  #     /* comment before
  #      * document */
  #     {
  #       /* comment */
  #       "foo": "1",
  #       "bar": "2",
  #       /* another
  #          comment
  #        */
  #       "baz": "3",
  #       "array": [
  #         /* comment inside array */
  #         1, 2, 3,
  #         4, /* comment inside an array */ 5,
  #         /*
  #            // "nested" comments
  #            { "faux json": "inside comment" }
  #          */
  #         6, 7
  #         /**
  #          * comment at end of array
  #          */
  #       ]
  #       /**************************
  #        comment at end of hash
  #        **************************/
  #     }
  #     /* comment after
  #        document */
  #   }
  #   doc = { "foo" => "1", "bar" => "2", "baz" => "3", "array" => [1, 2, 3, 4, 5, 6, 7] }
  #   expect(Utils::JSON.parse(source)).to eq(doc)
  # end
end

describe Utils::JSON::Generator do
  def generate(obj)
    Utils::JSON.generate(obj)
  end

  specify 'array' do
    expect(generate([1, 2, 3])).to eq(%([1, 2, 3]))
  end

  specify 'bool' do
    expect(generate([true, false])). to eq(%([true, false]))
  end

  specify 'null' do
    expect(generate([nil])).to eq(%([null]))
  end

  specify 'string' do
    expect(generate(["abc\n123"])).to eq(%(["abc\\n123"]))
  end

  specify 'string_unicode' do
    expect(generate(["ć\"č\nž\tš\\đ"])).to eq(%(["ć\\"č\\nž\\tš\\\\đ"]))
  end

  specify 'time' do
    time = Time.utc(2012, 04, 19, 1, 2, 3)
    expect(generate([time])).to eq(%(["2012-04-19 01:02:03 UTC"]))
  end

  specify 'date' do
    time = Date.new(2012, 04, 19)
    expect(generate([time])).to eq(%(["2012-04-19"]))
  end

  specify 'symbol' do
    expect(generate([:abc])).to eq(%(["abc"]))
  end

  specify 'hash' do
    json = generate(:abc => 123, 123 => 'abc')
    expect(json).to match(/^\{/)
    expect(json).to match(/\}$/)
    expect(json[1...-1].split(', ').sort).to eq([%("123": "abc"), %("abc": 123)])
  end

  specify 'nested_structure' do
    json = generate(:hash => {1=>2}, :array => [1,2])
    expect(json).to include(%("hash": {"1": 2}))
    expect(json).to include(%("array": [1, 2]))
  end

  specify 'invalid_json' do
    expect { generate("abc") }.to raise_error(ArgumentError)
  end

  specify 'invalid_object' do
    expect { generate("a" => Object.new) }.to raise_error(ArgumentError, /can't serialize Object/)
  end
end

end
