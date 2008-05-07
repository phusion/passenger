class FooController < ActionController::Base
	def new
		render :text => 'hello world'
	end
	
	def rails_env
		render :text => RAILS_ENV
	end
end
