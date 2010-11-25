#!/usr/bin/env ruby

require 'fileutils'
require 'ftools'
require 'optparse'

CFGLIMIT=%w{fedora-{13,14} epel-5}

stage_dir='./stage'

mock_base_dir = '/var/lib/mock'
mock_repo_dir = "#{mock_base_dir}/passenger-build-repo"
mock_etc_dir='/etc/mock'
#mock_etc_dir='/tmp/mock'

# If rpmbuild-md5 is installed, use it for the SRPM, so EPEL machines can read it.
rpmbuild = '/usr/bin/rpmbuild' + (File.exist?('/usr/bin/rpmbuild-md5') ? '-md5' : '')
rpmbuilddir = `rpm -E '%_topdir'`.chomp
rpmarch = `rpm -E '%_arch'`.chomp

@verbosity = 0

@can_build   = {
  'i386'    => %w{i586 i686},
  'i686'    => %w{i586 i686},
  'ppc'     => %w{},
  'ppc64'   => %w{ppc},
  's390x'   => %w{},
  'sparc'   => %w{},
  'sparc64' => %w{sparc},
  'x86_64'  => %w{i386 i586 i686},
}

#@can_build.keys.each {|k| @can_build[k].push k}
@can_build = @can_build[rpmarch.to_s == '' ? 'x86_64' : rpmarch]
@can_build.push rpmarch

bindir=File.dirname($0)

configs = Dir["#{mock_etc_dir}/{#{CFGLIMIT.join ','}}*"].map {|f| f.gsub(%r{.*/([^.]*).cfg}, '\1')}

def limit_configs(configs, limits)
  tree = configs.inject({}) do |m,c|
    (distro,version,arch) = c.split /-/
    next m unless @can_build.include?(arch)
    [
      # Rather than construct this list programatically, just spell it out
      '',
      distro,
      "#{distro}-#{version}",
      "#{distro}-#{version}-#{arch}",
      "#{distro}--#{arch}",
      "--#{arch}",
      # doubtful these will be used, but for completeness
      "-#{version}",
      "-#{version}-#{arch}",
    ].each do |pattern|
      unless m[pattern]
        m[pattern] = []
      end
      m[pattern].push c
    end
    m
  end
  tree.default = []
  # Special case for no arguments
  limits = [nil] if limits.empty?
  # By splitting and rejoining we normalize the distro--, etc. cases.
  return limits.map do |l|
    parts = l.to_s.split(/-/).map {|v| v == '*' ? nil : v}
    if parts[2] && !@can_build.include?(parts[2])
      abort "ERROR: Cannot build '#{parts[2]}' packages on '#{rpmarch}'"
    end
    tree[parts.join '-']
  end.flatten
end

def noisy_system(*args)
  puts args.join ' ' if @verbosity > 0
  system(*args)
end


############################################################################
options = {}
OptionParser.new do |opts|
  opts.banner = "Usage: #{$0} [options] [distro-version-arch] [distro-version] [distro--arch] [*--arch]"

  opts.on("-v", "--[no-]verbose", "Run verbosely. Add more -v's to increase @verbosity") do |v|
    @verbosity += v ? 1 : -1
  end

  # Do these with options, because the order matters
  opts.on('-b', '--mock-base-dir DIR', "Mock's base directory. Default: #{mock_base_dir}") do |v|
    #mock_repo_dir = v
    options[:mock_base_dir] = v
  end

  opts.on('-r', '--mock-repo-dir DIR', "Directory for special mock yum repository. Default: #{mock_repo_dir}") do |v|
    #mock_repo_dir = v
    options[:mock_repo_dir] = v
  end

  opts.on("-c", "--mock-config-dir DIR", "Directory for mock configuration. Default: #{mock_etc_dir}") do |v|
    if File.directory?(v)
      mock_etc_dir=v
    else
      abort "No such directory: #{v}"
    end
  end

  opts.on_tail("-h", "--help", "Show this message") do
    puts opts
    exit
  end
end.parse!

if options.key?(:mock_base_dir) || options.key?(:mock_repo_dir)
  if options.key?(:mock_base_dir)
    mock_base_dir = options[:mock_base_dir]
    mock_repo_dir = "#{mock_base_dir}/passenger-build-repo"
  end
  if options.key?(:mock_repo_dir)
    mock_repo_dir = options[:mock_repo_dir]
    unless mock_repo_dir[0] == '/'[0]
      mock_repo_dir = "#{mock_base_dir}/#{mock_repo_dir}"
    end
  end
end

configs = limit_configs(configs, ARGV)

if configs.empty?
  abort "Can't find a set of configs for '#{ARGV[0]}' (hint try 'fedora' or 'fedora-14' or even 'fedora-14-x86_64')"
end

puts "BUILD:\n  " + configs.join("\n  ") if @verbosity >= 2

FileUtils.rm_rf(stage_dir, :verbose => @verbosity > 0)
FileUtils.mkdir_p(stage_dir, :verbose => @verbosity > 0)

ENV['BUILD_VERBOSITY'] = @verbosity.to_s

# Check the ages of the configs for validity
mtime = File.mtime("#{bindir}/mocksetup.sh")
if configs.any? {|c| mtime > File.mtime("#{mock_etc_dir}/passenger-#{c}.cfg") rescue true }
  unless noisy_system("#{bindir}/mocksetup.sh", mock_repo_dir, mock_etc_dir)
    abort <<EndErr
Unable to run "#{bindir}/mocksetup.sh #{mock_repo_dir}". It is likely that you
need to run this command as root the first time, but if you have already done
that, it could also be that the current user (or this shell) is not in the
'mock' group.
EndErr
  end
end

srcdir=`rpm -E '%{_sourcedir}'`.chomp

FileUtils.ln_sf(Dir["#{ENV['PWD']}/{config/,patches/,release/GPG}*"], srcdir, :verbose => @verbosity > 0)

# No dist for SRPM
noisy_system(rpmbuild, *((@verbosity > 0 ? [] : %w{--quiet}) + ['--define', 'dist %nil', '-bs', 'passenger.spec']))

# I really wish there was a way to query rpmbuild for this via the spec file,
# but rpmbuild --eval doesn't seem to work
srpm=`ls -1t $HOME/rpmbuild/SRPMS | head -1`.chomp

FileUtils.mkdir_p(stage_dir + '/SRPMS', :verbose => @verbosity > 0)

FileUtils.cp("#{rpmbuilddir}/SRPMS/#{srpm}", "#{stage_dir}/SRPMS",
:verbose => @verbosity > 0)

mockvolume = @verbosity >= 2 ? %w{-v} : @verbosity < 0 ? %w{-q} : []

configs.each do |cfg|
  puts "---------------------- Building #{cfg}" if @verbosity >= 0
  pcfg = 'passenger-' + cfg
  idir = File.join stage_dir, cfg.split(/-/)
  # Move *mockvolume to the end, since it causes Ruby to cry in the middle
  # Alt sol'n: *(foo + ['bar'] )
  if noisy_system('mock', '-r', pcfg, "#{rpmbuilddir}/SRPMS/#{srpm}", *mockvolume)
  else
    abort "Mock failed. See above for details"
  end
  FileUtils.mkdir_p(idir, :verbose => @verbosity > 0)
  FileUtils.cp(Dir["/var/lib/mock/#{pcfg}/result/*.rpm"],
  idir, :verbose => @verbosity > 0)
  FileUtils.rm_f(Dir["#{idir}/*.src.rpm"], :verbose => @verbosity > 1)
end

if File.directory?("#{stage_dir}/epel")
  FileUtils.mv "#{stage_dir}/epel", "#{stage_dir}/rhel", :verbose => @verbosity > 0
end

noisy_system('rpm', '--addsign', *Dir["#{stage_dir}/**/*.rpm"])
