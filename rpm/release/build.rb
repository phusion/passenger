#!/usr/bin/ruby

require 'fileutils'
require 'ftools'

CFGLIMIT=%w{fedora-{13,14} epel-5}

stage_dir='./stage'
mock_repo_dir = '/var/lib/mock/passenger-build-repo'

mockdir='/etc/mock'
#mockdir='/tmp/mock'

# If rpmbuild-md5 is installed, use it for the SRPM, so EPEL machines can read it.
rpmbuild = '/usr/bin/rpmbuild' + (File.exist?('/usr/bin/rpmbuild-md5') ? '-md5' : '')
rpmbuilddir = `rpm -E '%_topdir'`.chomp
rpmarch = `rpm -E '%_arch'`.chomp

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
@can_build = @can_build[rpmarch]
@can_build.push rpmarch

bindir=File.dirname($0)

configs = Dir["#{mockdir}/{#{CFGLIMIT.join ','}}*"].map {|f| f.gsub(%r{.*/([^.]*).cfg}, '\1')}

def limit_configs(configs, limit)
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
  # By splitting and rejoining we normalize the distro--, etc. cases.
  return tree[limit.join '-']
end

def noisy_system(*args)
  puts args.join ' '
  system(*args)
end


############################################################################
limit = ARGV[0].to_s.split /-/
if limit[2] && !@can_build.include?(limit[2])
  abort "ERROR: Cannot build '#{limit[2]}' packages on '#{rpmarch}'"
end
configs = limit_configs(configs, limit)
if configs.empty?
  abort "Can't find a set of configs for '#{ARGV[0]}' (hint try 'fedora' or 'fedora-14' or even 'fedora-14-x86_64')"
end

puts "rm -rf #{stage_dir}"
FileUtils.rm_rf(stage_dir)
puts "mkdir -p #{stage_dir}"
FileUtils.mkdir_p(stage_dir)

# Check the ages of the configs for validity
mtime = File.mtime("#{bindir}/mocksetup.sh")
if configs.any? {|c| mtime > File.mtime("#{mockdir}/passenger-#{c}.cfg") rescue true }
  unless noisy_system("#{bindir}/mocksetup.sh", mock_repo_dir)
    abort <<EndErr
Unable to run "#{bindir}/mocksetup.sh #{mock_repo_dir}". It is likely that you
need to run this command as root the first time, but if you have already done
that, it could also be that the current user (or this shell) is not in the
'mock' group.
EndErr
  end
end

# No dist for SRPM
noisy_system(rpmbuild, '--define', 'dist %nil', '-bs', 'passenger.spec')
# I really wish there was a way to query rpmbuild for this via the spec file,
# but rpmbuild --eval doesn't seem to work
srpm=`ls -1t $HOME/rpmbuild/SRPMS | head -1`.chomp
puts "mkdir -p #{stage_dir}/SRPMS"
FileUtils.mkdir_p(stage_dir + '/SRPMS')
puts "cp #{rpmbuilddir}/SRPMS/#{srpm} #{stage_dir}/SRPMS"
File.copy "#{rpmbuilddir}/SRPMS/#{srpm}", "#{stage_dir}/SRPMS"

configs.each do |cfg|
  puts "---------------------- Building #{cfg}"
  pcfg = 'passenger-' + cfg
  idir = File.join stage_dir, cfg.split(/-/)
  if noisy_system('mock', '-r', pcfg, "#{rpmbuilddir}/SRPMS/#{srpm}")
  else
    abort "Mock failed. See above for details"
  end
  FileUtils.mkdir_p(idir, :verbose => true)
  # puts "cp /var/lib/mock/#{pcfg}/result/*.rpm #{stage_dir}/#{idir}"
  # Dir["cp /var/lib/mock/#{pcfg}/result/*.rpm"].each
  FileUtils.cp(Dir["/var/lib/mock/#{pcfg}/result/*.rpm"],
              idir, :verbose => true)
end

if File.directory?("#{stage_dir}/epel")
  FileUtils.mv "#{stage_dir}/epel", "#{stage_dir}/rhel"
end

noisy_system('rpm', '--addsign', *Dir["#{stage_dir}/**/*.rpm"])
