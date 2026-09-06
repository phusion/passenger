# Specifies Passenger *development* dependencies.
# See also: doc/DesignAspects/LimitedGemDependencies.md

source 'https://rubygems.org/'

ruby '>= 3.0'

gemspec

group :development do
  gem 'cgi'
  gem 'logger'
  gem 'json'
  gem 'mime-types', '~> 3.7.0'
  gem 'rspec', '~> 3.13.2'
  gem 'rspec-collection_matchers'
  gem 'webrick', '~> 1.9.2'
  gem 'rubocop'
  gem 'rubocop-rails-omakase'
  gem 'gpgme', install_if: ENV['USER'] == 'camdennarzt'
end
