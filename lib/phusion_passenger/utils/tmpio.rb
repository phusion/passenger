require 'tmpdir'
require 'fileutils'

module PhusionPassenger
module Utils

# some versions of Ruby had a broken Tempfile which didn't work
# well with unlinked files.  This one is much shorter, easier
# to understand, and slightly faster.
class TmpIO < File

  # creates and returns a new File object.  The File is unlinked
  # immediately, switched to binary mode, and userspace output
  # buffering is disabled
  def self.new(namespace, options = nil)
    if options
      mode   = options[:mode] || RDWR
      binary = options.fetch(:binary, true)
      suffix = options[:suffix]
      unlink_immediately = options.fetch(:unlink_immediately, true)
    else
      mode   = RDWR
      binary = true
      suffix = nil
      unlink_immediately = true
    end
    fp = begin
      super("#{Dir::tmpdir}/#{namespace}-#{rand(0x100000000).to_s(36)}#{suffix}", mode | CREAT | EXCL, 0600)
    rescue Errno::EEXIST
      retry
    end
    unlink(fp.path) if unlink_immediately
    fp.binmode if binary
    fp.sync = true
    fp
  end

  # for easier env["rack.input"] compatibility with Rack <= 1.1
  def size
    stat.size
  end unless File.method_defined?(:size)
end

# Like Dir.mktmpdir, but creates shorter filenames.
def self.mktmpdir(prefix_suffix=nil, tmpdir=nil)
  case prefix_suffix
  when nil
    prefix = "d"
    suffix = ""
  when String
    prefix = prefix_suffix
    suffix = ""
  when Array
    prefix = prefix_suffix[0]
    suffix = prefix_suffix[1]
  else
    raise ArgumentError, "unexpected prefix_suffix: #{prefix_suffix.inspect}"
  end
  tmpdir ||= Dir.tmpdir
  begin
    path = "#{tmpdir}/#{prefix}#{rand(0x100000000).to_s(36)}"
    path << suffix
    Dir.mkdir(path, 0700)
  rescue Errno::EEXIST
    retry
  end

  if block_given?
    begin
      yield path
    ensure
      FileUtils.remove_entry_secure path
    end
  else
    path
  end
end

end # module Utils
end # module PhusionPassenger
