#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010, 2011, 2012 Phusion Holding B.V.
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

desc "Generate all documentation"
task :doc => Packaging::ASCII_DOCS

Packaging::ASCII_DOCS.each do |target|
  source = target.sub(/\.html$/, '.txt')
  file target => [source] + Dir["doc/users_guide_snippets/**/*"] do
    if target =~ /apache/i
      type = "apache"
      juvia_site_key = "5jpmkyjqlml8rktsfldfpbwth8ig7w9"
    elsif target =~ /nginx/i
      type = "nginx"
      juvia_site_key = "q0ptarhn8o9xanwomq8zkgewbtwffyz"
    elsif target =~ /standalone/i
      type = "standalone"
      juvia_site_key = "amggdy0k65hb4hbjg3dh7pnb9zd8dwy"
    else
      type = nil
      juvia_site_key = nil
    end
    command = "mizuho '#{source}'"
    command << " -a #{type}" if type
    if juvia_site_key
      command << " -c juvia --juvia-url http://juvia.phusion.nl --juvia-site-key #{juvia_site_key}"
    end
    sh(command)
  end

  task :clean do
    if boolean_option('CLEAN_DOCS', true)
      sh "rm -f '#{target}'"
    end
  end
end

def create_markdown_compilation_task(target)
  source = target.sub(/\.html$/, '.txt.md')
  dependencies = [
    source,
    'doc/templates/markdown.html.erb',
    'doc/templates/bootstrap.min.css'
  ]

  file(target => dependencies) do
    sh "bluecloth -f #{source} > #{target}.tmp"
    begin
      puts "Creating #{target}"
      require 'erb'
      template = ERB.new(File.read('doc/templates/markdown.html.erb'))
      title    = File.basename(target, '.html').gsub(/([A-Z])/, ' \1').strip
      content  = File.read("#{target}.tmp")
      css      = File.read('doc/templates/bootstrap.min.css')
      data = template.result(binding)
      File.open(target, 'w') do |f|
        f.write(data)
      end
    ensure
      sh "rm -f #{target}.tmp"
    end
  end

  task :doc => target

  task :clean do
    if boolean_option('CLEAN_DOCS', true)
      sh "rm -f #{target}"
    end
  end
end

create_markdown_compilation_task('doc/Packaging.html')
create_markdown_compilation_task('doc/CloudLicensingConfiguration.html')
create_markdown_compilation_task('doc/ServerOptimizationGuide.html')

desc "Upload documentation to the Phusion web server"
task 'doc:rsync' => :doc do
  sh "cd doc && rsync -rv --progress --partial-dir=.rsync-partial --human-readable . " +
    "shell.phusion.nl:/home/phusion/websites/passenger_docs/"
end
