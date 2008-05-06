require 'fileutils'

module TestHelper
	STUB_TEMP_DIR = 'tmp.stub'

	class Stub
		attr_reader :app_root
		
		def initialize(name, app_root)
			@name = name
			@app_root = app_root
		end
		
		def environment_rb
			return "#{@app_root}/config/environment.rb"
		end
		
		def use_vendor_rails(name)
			FileUtils.cp_r("stub/vendor_rails/#{name}", "#{@app_root}/vendor/rails")
		end
		
		def dont_use_vendor_rails
			FileUtils.rm_rf("#{@app_root}/vendor/rails")
		end
		
		def destroy
			FileUtils.rm_rf(@app_root)
		end
	end
	
	def setup_rails_stub(name, dir = STUB_TEMP_DIR)
		FileUtils.rm_rf(dir)
		FileUtils.mkdir_p(dir)
		FileUtils.cp_r("stub/rails_apps/#{name}/.", dir)
		FileUtils.mkdir_p("#{dir}/log")
		system("chmod", "-R", "a+rw", dir)
		return Stub.new(name, dir)
	end
	
	def teardown_rails_stub
		FileUtils.rm_rf(STUB_TEMP_DIR)
	end
	
	def use_rails_stub(name, dir = STUB_TEMP_DIR)
		stub = setup_rails_stub(name, dir)
		yield stub
	ensure
		stub.destroy
	end
end

File.class_eval do
	def self.prepend(filename, data)
		original_content = File.read(filename)
		File.open(filename, 'w') do |f|
			f.write(data)
			f.write(original_content)
		end
	end
	
	def self.append(filename, data)
		File.open(filename, 'a') do |f|
			f.write(data)
		end
	end

	def self.write(filename, content = nil)
		if block_given?
			content = yield File.read(filename)
		end
		File.open(filename, 'w') do |f|
			f.write(content)
		end
	end
end

