source 'https://rubygems.org/'

ruby '>= 2.5'

group :development do
  gem 'json'
  gem 'mime-types', '~> 3.5.1'
  gem 'drake'
  gem 'rack'
  gem 'rackup', '>= 2.1'
  gem 'rake'
  gem 'rspec', '~> 3.12.0'
  gem 'rspec-collection_matchers'
  gem 'webrick', '~> 1.8.1'
end

if ENV['USER'] == 'camdennarzt'
  group :development do
    gem 'solargraph'
    gem 'gpgme'
  end
end
