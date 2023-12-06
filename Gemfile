source 'https://rubygems.org/'
require 'rubygems/version.rb'

# Make Bundler handle Ruby compat: https://github.com/rubygems/bundler-features/issues/120
ruby RUBY_VERSION

group :base do
  gem 'json'
  gem 'mime-types', '~> 3.5.1'
  gem 'rack'
  gem 'rake'
  gem 'rspec', '~> 3.12.0'
  gem 'rspec-collection_matchers'
end

group :future do
  gem 'webrick', '~> 1.8.1'
end

if ENV['USER'] == 'camdennarzt'
group :development do
  gem 'solargraph'
end
end
