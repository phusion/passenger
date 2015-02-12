# Polyfill for the shellwords library on Ruby 1.8.5.

require 'shellwords'

if !Shellwords.respond_to?(:escape)
  Shellwords.class_eval do
    def self.escape(str)
      # An empty argument will be skipped, so return empty quotes.
      return "''" if str.empty?
      str = str.dup
      # Treat multibyte characters as is.  It is caller's responsibility
      # to encode the string in the right encoding for the shell
      # environment.
      str.gsub!(/([^A-Za-z0-9_\-.,:\/@\n])/, "\\\\\\1")
      # A LF cannot be escaped with a backslash because a backslash + LF
      # combo is regarded as line continuation and simply ignored.
      str.gsub!(/\n/, "'\n'")
      return str
    end
  end
end

if !Shellwords.respond_to?(:join)
  Shellwords.class_eval do
    def self.join(array)
      array.map { |arg| escape(arg) }.join(' ')
    end
  end
end
