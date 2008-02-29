class WelcomeController < ApplicationController
	caches_page :cached
	
	def parameters_test
		headers["Content-Type"] = "text/plain"
		render :text => params.to_xml
	end
	
	def headers_test
		headers["X-Foo"] = "Bar"
		render :nothing => true
	end
end
