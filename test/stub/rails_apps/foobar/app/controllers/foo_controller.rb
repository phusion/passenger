class FooController < ActionController::Base
	def new
		render :text => 'hello world'
	end
	
	def rails_env
		render :text => RAILS_ENV
	end
	
	def backtrace
		render :text => caller.join("\n")
	end
end
