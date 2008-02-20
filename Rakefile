$LOAD_PATH << "#{File.dirname(__FILE__)}/lib"
require 'rake/rdoctask'

Rake::RDocTask.new do |rd|
	rd.rdoc_dir = "doc/rdoc"
	rd.rdoc_files.include("lib/mod_rails/*.rb")
	rd.template = "jamis"
	rd.title = "Passenger Ruby API"
	rd.options << "-S"
	rd.options << "-N"
end
