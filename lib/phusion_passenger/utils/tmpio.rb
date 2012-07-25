require 'tmpdir'

module PhusionPassenger
module Utils

# some versions of Ruby had a broken Tempfile which didn't work
# well with unlinked files.  This one is much shorter, easier
# to understand, and slightly faster.
class TmpIO < File

  # creates and returns a new File object.  The File is unlinked
  # immediately, switched to binary mode, and userspace output
  # buffering is disabled
  def self.new(namespace)
    fp = begin
      super("#{Dir::tmpdir}/#{namespace}-#{rand}", RDWR|CREAT|EXCL, 0600)
    rescue Errno::EEXIST
      retry
    end
    unlink(fp.path)
    fp.binmode
    fp.sync = true
    fp
  end

  # for easier env["rack.input"] compatibility with Rack <= 1.1
  def size
    stat.size
  end unless File.method_defined?(:size)
end

end # module Utils
end # module PhusionPassenger
