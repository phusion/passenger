define 'download-tool' do
  name "A download tool like 'wget' or 'curl'"
  define_checker do
    check_for_command('wget') || check_for_command('curl')
  end
  on :debian do
    apt_get_install "wget curl"
  end
  on :redhat do
    yum_install "wget curl"
  end
  on :other_platforms do
    install_instructions "Please install either wget (http://www.gnu.org/software/wget/) or curl (http://curl.haxx.se/)."
  end
end