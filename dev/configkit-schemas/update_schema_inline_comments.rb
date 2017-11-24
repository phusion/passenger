#!/usr/bin/env ruby
require_relative '../../src/ruby_supportlib/phusion_passenger/utils/json'

SimpleJSON = PhusionPassenger::Utils::JSON

FILES = Dir['src/**/*.{h,cpp}']
SCHEMAS = SimpleJSON.parse(File.read('dev/configkit-schemas/index.json'))

def main
  FILES.each do |path|
    update_file(path)
  end
end

def update_file(path)
  content = File.open(path, 'r:utf-8') do |f|
    f.read
  end
  orig_content = content.dup

  pos = 0

  while match_begin = /^(.*?)BEGIN ConfigKit schema: (.+)/.match(content, pos)
    if match_end = /END/.match(content, match_begin.end(0))
      prefix = match_begin.captures[0]
      class_name = match_begin.captures[1]
      replacement = format_inline_comment_for_schema(prefix, class_name)

      content[match_begin.begin(0) .. match_end.end(0) - 1] = replacement
      pos = match_begin.begin(0) + replacement.size
    else
      break
    end
  end

  if content != orig_content
    puts "Updating #{path}"
    File.open(path, 'w:utf-8') do |f|
      f.write(content)
    end
  end
end

def format_inline_comment_for_schema(prefix, class_name)
  lines = [
    "BEGIN ConfigKit schema: #{class_name}",
    "(do not edit: following text is automatically generated",
    "by 'rake configkit_schemas_inline_comments')",
    ""
  ]

  if schema = SCHEMAS[class_name]
    table = []
    schema.each_pair do |key, info|
      table << [
        key,
        info['type'],
        info['required'] ? 'required' : '-',
        format_option_names(info)
      ]
    end

    column_lengths = find_max_column_lengths(table)
    table.each do |row|
      fmt = "  %-#{column_lengths[0]}s   %-#{column_lengths[1]}s   %-#{column_lengths[2]}s   %-#{column_lengths[3]}s"
      lines << sprintf(fmt, *row)
    end
  else
    lines << "(unknown: #{class_name} not in dev/configkit-schemas/index.json"
    lines << " Please run:"
    lines << "   touch src/schema_printer/SchemaPrinterMain.cpp.cxxcodebuilder"
    lines << "   rake configkit_schemas_inline_comments"
    lines << ")"
  end

  lines << ""
  lines << "END"
  lines.map{ |x| "#{prefix}#{x}".rstrip }.join("\n")
end

def format_option_names(schema_entry)
  options = []
  if schema_entry['has_default_value']
    if schema_entry['has_default_value'] == 'static'
      desc = format_default_value_desc(schema_entry['default_value'])
      options << "default(#{desc})"
    else
      options << 'default'
    end
  end
  options << 'secret' if schema_entry['secret']
  options << 'read_only' if schema_entry['read_only']
  result = options.join(',')
  result = '-' if result.empty?
  result
end

def format_default_value_desc(value)
  if value.is_a?(Array) || value.is_a?(Hash)
    SimpleJSON.generate(value)
  else
    SimpleJSON.generate([value]).sub(/\A\[/, '').sub(/\]\Z/, '')
  end
end

def find_max_column_lengths(table)
  lengths = []
  table.each do |row|
    row.each_with_index do |col, i|
      if lengths[i].nil? || lengths[i] < col.size
        lengths[i] = col.size
      end
    end
  end
  lengths
end

main
