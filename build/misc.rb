# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
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

desc "Run 'sloccount' to see how much code Passenger has"
task :sloccount do
  ENV['LC_ALL'] = 'C'
  begin
    # sloccount doesn't recognize the scripts in
    # bin/ as Ruby, so we make symlinks with proper
    # extensions.
    tmpdir = ".sloccount"
    system "rm -rf #{tmpdir}"
    mkdir tmpdir
    Dir['bin/*'].each do |file|
      safe_ln file, "#{tmpdir}/#{File.basename(file)}.rb"
    end
    sh "sloccount", *Dir[
      "#{tmpdir}/*",
      "src"
    ]
  ensure
    system "rm -rf #{tmpdir}"
  end
end

def extract_latest_news_contents_and_items
  # The text is in the following format:
  #
  #   Release x.x.x
  #   -------------
  #
  #    * Text.
  #    * More text.
  #    * A header.
  #      With yet more text.
  #
  #   Release y.y.y
  #   -------------
  #   .....
  contents = File.read("CHANGELOG")

  # We're only interested in the latest release, so extract the text for that.
  contents =~ /\A(Release.*?)^(Release|Older releases)/m
  contents = $1
  contents.sub!(/\A.*?\n-+\n+/m, '')
  contents.sub!(/\n+\Z/, '')

  # Now split the text into individual items.
  items = contents.split(/^ \* /)
  items.shift while items.first == ""

  return [contents, items]
end

desc "Convert the Changelog items for the latest release to HTML"
task :changelog_as_html do
  require 'cgi'
  _, items = extract_latest_news_contents_and_items

  puts "<ul>"
  items.each do |item|
    def format_paragraph(text)
      # Get rid of newlines: convert them into spaces.
      text.gsub!("\n", ' ')
      while text.index('  ')
        text.gsub!('  ', ' ')
      end

      # Auto-link to issue tracker.
      text.gsub!(/(bug #|issue #|GH-)(\d+)/i) do
        url = "https://github.com/phusion/passenger/issues/#{$2}"
        %Q(<{a href="#{url}"}>#{$1}#{$2}<{/a}>)
      end

      text.strip!
      text = CGI.escapeHTML(text)
      text.gsub!(%r(&lt;\{(.*?)\}&gt;(.*?)&lt;\{/(.*?)\}&gt;)) do
        "<#{CGI.unescapeHTML $1}>#{$2}</#{CGI.unescapeHTML $3}>"
      end
      text
    end

    puts "<li>" + format_paragraph(item.strip) + "</li>"
  end
  puts "</ul>"
end

desc "Convert the Changelog items for the latest release to Markdown"
task :changelog_as_markdown do
  contents, _ = extract_latest_news_contents_and_items

  # Auto-link to issue tracker.
  contents.gsub!(/(bug #|issue #|GH-)(\d+)/i) do
    url = "https://github.com/phusion/passenger/issues/#{$2}"
    %Q([#{$1}#{$2}](#{url}))
  end

  puts contents
end

desc "Update CONTRIBUTORS file"
task :contributors do
  entries = `git log --format='%aN' | sort -u`.split("\n")

  # Phusion employees
  entries.delete "Hongli Lai"
  entries.delete "Hongli Lai (Phusion"
  entries.delete "Ninh Bui"
  entries.push "Ninh Bui (Phusion)"
  entries.delete "Phusion Dev"
  entries.delete "Tinco Andringa"
  entries.push "Tinco Andringa (Phusion)"
  entries.delete "Goffert van Gool"
  entries.push "Goffert van Gool (Phusion)"
  entries.delete "OnixGH"
  entries.delete "onix"
  entries.delete "Camden Narzt"
  entries.push "Camden Narzt (Phusion)"
  entries.delete "FloorD"
  entries.delete "Floor Drees"
  entries.push "Floor Drees (Phusion)"
  entries.delete "Luuk Hafkamp"
  entries.push "Luuk Hafkamp (Phusion)"

  # Non-employee contributors
  entries.push "Ruslan Ermilov (NGINX Inc)"
  entries.delete "Gokulnath"
  entries.push "Gokulnath Manakkattil"
  entries.push "Sean Wilkinson"
  entries.push "Yichun Zhang"

  File.open("CONTRIBUTORS", "w") do |f|
    f.puts(entries.sort{ |a, b| a.downcase <=> b.downcase }.join("\n"))
  end
  puts "Updated CONTRIBUTORS"
end

desc "Update the C++ dependency map"
task :dependency_map do
  sh "./dev/index_cxx_dependencies.rb > build/support/cxx_dependency_map.rb"
end

dependencies = [
  COMMON_LIBRARY.link_objects,
  LIBBOOST_OXT,
  LIBEV_TARGET,
  LIBUV_TARGET
].flatten.compact
task :compile_app => dependencies do
  source = ENV['SOURCE'] || ENV['FILE'] || ENV['F']
  if !source
    STDERR.puts "Please specify the source filename with SOURCE=(...)"
    exit 1
  end
  if source =~ /\.h/
    File.open('_source.cpp', 'w') do |f|
      f.puts "#include \"#{source}\""
    end
    source = '_source.cpp'
  end
  object = source.sub(/\.cpp$/, '.o')
  exe = source.sub(/\.cpp$/, '')
  begin
    compile_cxx(object,
      source,
      :include_paths => [
        "src/agent",
        *CXX_SUPPORTLIB_INCLUDE_PATHS
      ],
      :flags => [
        OPTIMIZE ? "-O" : nil,
        "-DSTANDALONE",
        libev_cflags,
        libuv_cflags,
        websocketpp_cflags
      ].compact
    )
    create_cxx_executable(exe,
      object,
      :flags => [
        "-DSTANDALONE",
        libev_cflags,
        libuv_cflags,
        COMMON_LIBRARY.link_objects_as_string,
        LIBBOOST_OXT_LINKARG,
        libev_libs,
        libuv_libs,
        websocketpp_libs,
        PlatformInfo.curl_libs,
        PlatformInfo.zlib_libs,
        PlatformInfo.crypto_libs,
        PlatformInfo.portability_cxx_ldflags
      ]
    )
  ensure
    File.unlink('_source.cpp') rescue nil
  end
end
