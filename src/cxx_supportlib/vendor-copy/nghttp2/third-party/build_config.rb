MRuby::Build.new do |conf|
  # TODO use same compilers configured in configure script
  toolchain :clang

  # C++ project needs this.  Without this, mruby exception does not
  # properly destory C++ object allocated on stack.
  conf.enable_cxx_abi

  conf.build_dir = ENV['BUILD_DIR']

  # include the default GEMs
  conf.gembox 'default'
  conf.gem :core => 'mruby-eval'
end
