class UploadsController < ApplicationController
	caches_page :index
	
	def create
		headers["Content-Type"] = "text/plain"
		render :text =>
			"name 1 = " + params[:upload][:name1] + "\n" +
			"name 2 = " + params[:upload][:name2] + "\n" +
			"data = " + params[:upload][:data].read
	end
end
