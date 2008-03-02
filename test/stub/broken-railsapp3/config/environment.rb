RAILS_GEM_VERSION = '~> 2.0.0'
class MyError < StandardError
end
raise MyError, "This is a custom exception."
