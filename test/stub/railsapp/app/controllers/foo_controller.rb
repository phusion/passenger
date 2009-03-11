class FooController < ActionController::Base
	def new
		render :text => 'hello world'
	end

	def pid
		render :text => Process.pid.to_s
	end
end
