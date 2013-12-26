# Be sure to restart your server when you modify this file.

# Your secret key for verifying cookie session data integrity.
# If you change this key, all old sessions will become invalid!
# Make sure the secret is at least 30 characters and all random, 
# no regular words or you'll be exposed to dictionary attacks.
ActionController::Base.session = {
  :key         => '_empty_session',
  :secret      => 'f7cda47aa0729bbd117641611432893613c55fb2bee99bd4f324cf9ad04ad555504d935ec1424da0f85c81d11c3972aa840eeb2e171d04696c7e9f70ce2a344d'
}

# Use the database for sessions instead of the cookie-based default,
# which shouldn't be used to store highly confidential information
# (create the session table with "rake db:sessions:create")
# ActionController::Base.session_store = :active_record_store
