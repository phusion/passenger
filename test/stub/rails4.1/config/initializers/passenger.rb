if defined?(PhusionPassenger)
	PhusionPassenger.install_framework_extensions!(
		event_preprocessor: ->(e) { e.payload[:sql].gsub!("secret","PASSWORD") if e.payload[:sql] }
	)
end