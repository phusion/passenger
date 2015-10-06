define 'fastthread' do
  name 'fastthread'
  define_checker do
    check_for_ruby_library('fastthread')
  end
  gem_install 'fastthread'
end

define 'rack' do
  name 'rack'
  define_checker do
    check_for_ruby_library('rack')
  end
  gem_install 'rack'
end
