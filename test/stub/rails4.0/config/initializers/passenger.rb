event_preprocessor = lambda { |e| e.payload[:sql].gsub!("secret","PASSWORD") if e.payload[:sql] }
PhusionPassenger.install_framework_extensions!(:event_preprocessor => event_preprocessor ) if defined?(PhusionPassenger)
