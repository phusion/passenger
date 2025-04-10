# Specifies Passenger *development* dependencies.
# See also: doc/DesignAspects/LimitedGemDependencies.md

source 'https://rubygems.org/'

ruby '>= 2.5'

gemspec

group :development do
  gem 'json'
  gem 'mime-types', '~> 3.5.1'
  gem 'drake'
  gem 'rspec', '~> 3.12.0'
  gem 'rspec-collection_matchers'
  gem 'webrick', '~> 1.8.1'
  gem 'gpgme' if ENV['USER'] == 'camdennarzt'
end
