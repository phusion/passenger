#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008, 2009  Phusion
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

ASCIIDOC_FLAGS = "-a toc -a numbered -a toclevels=3 -a icons"
DOXYGEN = 'doxygen'

desc "Generate all documentation"
task :doc => [:rdoc]

if PlatformInfo.find_command(DOXYGEN)
	task :doc => :doxygen
end

task :doc => Packaging::ASCII_DOCS

Packaging::ASCII_DOCS.each do |target|
	source = target.sub(/\.html$/, '.txt')
	file target => [source] + Dir["doc/users_guide_snippets/**/*"] do
		if PlatformInfo.asciidoc
			if target =~ /apache/i
				type = "-a apache"
			elsif target =~ /nginx/i
				type = "-a nginx"
			else
				type = nil
			end
	  		sh "#{PlatformInfo.asciidoc} #{ASCIIDOC_FLAGS} #{type} '#{source}'"
		else
			sh "echo 'asciidoc required to build docs' > '#{target}'"
		end
	end
end

task :clobber => [:'doxygen:clobber'] do
	sh "rm -f *.html"
end

desc "Generate Doxygen C++ API documentation if necessary"
task :doxygen => ['doc/cxxapi']
file 'doc/cxxapi' => Dir['ext/apache2/*.{h,c,cpp}'] do
	sh "cd doc && doxygen"
end

desc "Force generation of Doxygen C++ API documentation"
task :'doxygen:force' do
	sh "cd doc && doxygen"
end

desc "Remove generated Doxygen C++ API documentation"
task :'doxygen:clobber' do
	sh "rm -rf doc/cxxapi"
end

Rake::RDocTask.new(:clobber_rdoc => "rdoc:clobber", :rerdoc => "rdoc:force") do |rd|
	rd.main = "README"
	rd.rdoc_dir = "doc/rdoc"
	rd.rdoc_files.include("README", "DEVELOPERS.TXT",
		"lib/phusion_passenger/*.rb",
		"lib/phusion_passenger/*/*.rb",
		"ext/phusion_passenger/*.c")
	rd.template = "./doc/template/horo"
	rd.title = "Passenger Ruby API"
	rd.options << "-S" << "-N" << "-p" << "-H"
end
