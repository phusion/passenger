module PhusionPassenger
module AdminTools
class Color
  class << self
    # ANSI color codes
    { :reset    => "\e[0m",
      :bold     => "\e[1m",
      :yellow   => "\e[33m",
      :white    => "\e[37m",
      :black_bg => "\e[40m",
      :blue_bg  => "\e[44m"
    }.each do |k, v|
      if STDOUT.tty?
        define_method(k){v}
      elsif
        define_method(k){""}
      end
    end
  end
end
end
end
