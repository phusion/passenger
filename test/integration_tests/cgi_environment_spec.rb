shared_examples_for "CGI environment variables compliance" do
	specify "REQUEST_URI contains the request URI including query string" do
		cgi_envs = get('/welcome/cgi_environment?foo=escaped%20string')
		cgi_envs.should include("REQUEST_URI = #{@base_uri}/welcome/cgi_environment?foo=escaped%20string\n")
	end
	
	specify "REQUEST_URI contains the original escaped URI" do
		cgi_envs = get('/welcome/cgi_environment/%C3%BC?foo=escaped%20string')
		cgi_envs.downcase.should include("request_uri = #{@base_uri}/welcome/cgi_environment/%c3%bc?foo=escaped%20string\n")
	end
	
	specify "PATH_INFO contains the request URI without the base URI and without the query string" do
		cgi_envs = get('/welcome/cgi_environment?foo=escaped%20string')
		cgi_envs.should include("PATH_INFO = /welcome/cgi_environment\n")
	end
	
	specify "PATH_INFO contains the original escaped URI" do
		cgi_envs = get('/welcome/cgi_environment/%C3%BC')
		cgi_envs.downcase.should include("path_info = /welcome/cgi_environment/%c3%bc\n")
	end
	
	specify "QUERY_STRING contains the query string" do
		cgi_envs = get('/welcome/cgi_environment?foo=escaped%20string')
		cgi_envs.should include("QUERY_STRING = foo=escaped%20string\n")
	end
	
	specify "QUERY_STRING must be present even when there's no query string" do
		cgi_envs = get('/welcome/cgi_environment')
		cgi_envs.should include("QUERY_STRING = \n")
	end
	
	specify "SCRIPT_NAME contains the base URI, or the empty string if the app is deployed on the root URI" do
		cgi_envs = get('/welcome/cgi_environment')
		cgi_envs.should include("SCRIPT_NAME = #{@base_uri}\n")
	end
end
