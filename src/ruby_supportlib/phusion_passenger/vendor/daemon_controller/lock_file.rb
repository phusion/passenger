# daemon_controller, library for robust daemon management
# Copyright (c) 2010-2017 Phusion Holding B.V.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

require 'fcntl'

module PhusionPassenger
class DaemonController
  # A lock file is a synchronization mechanism, like a Mutex, but it also allows
  # inter-process synchronization (as opposed to only inter-thread synchronization
  # within a single process).
  #
  # Processes can obtain either a shared lock or an exclusive lock. It's possible
  # for multiple processes to obtain a shared lock on a file as long as no
  # exclusive lock has been obtained by a process. If a process has obtained an
  # exclusive lock, then no other processes can lock the file, whether they're
  # trying to obtain a shared lock or an exclusive lock.
  #
  # Note that on JRuby, LockFile can only guarantee synchronization between
  # threads if the different threads use the same LockFile object. Specifying the
  # same filename is not enough.
  class LockFile
    class AlreadyLocked < StandardError
    end

    # Create a LockFile object. The lock file is initially not locked.
    #
    # +filename+ may point to a nonexistant file. In that case, the lock
    # file will not be created until one's trying to obtain a lock.
    #
    # Note that LockFile will use this exact filename. So if +filename+
    # is a relative filename, then the actual lock file that will be used
    # depends on the current working directory.
    def initialize(filename)
      @filename = filename
    end

    # Obtain an exclusive lock on the lock file, yield the given block,
    # then unlock the lockfile. If the lock file was already locked (whether
    # shared or exclusively) by another process/thread then this method will
    # block until the lock file has been unlocked.
    #
    # The lock file *must* be writable, otherwise an Errno::EACCESS
    # exception will be raised.
    def exclusive_lock
      File.open(@filename, 'w') do |f|
        if Fcntl.const_defined? :F_SETFD
          f.fcntl(Fcntl::F_SETFD, Fcntl::FD_CLOEXEC)
        end
        f.flock(File::LOCK_EX)
        yield
      end
    end

    # Obtain an exclusive lock on the lock file, yield the given block,
    # then unlock the lockfile. If the lock file was already exclusively
    # locked by another process/thread then this method will
    # block until the exclusive lock has been released. This method will not
    # block if only shared locks have been obtained.
    #
    # The lock file *must* be writable, otherwise an Errno::EACCESS
    # exception will be raised.
    def shared_lock
      File.open(@filename, 'w+') do |f|
        if Fcntl.const_defined? :F_SETFD
          f.fcntl(Fcntl::F_SETFD, Fcntl::FD_CLOEXEC)
        end
        f.flock(File::LOCK_SH)
        yield
      end
    end

    # Try to obtain a shared lock on the lock file, similar to #shared_lock.
    # But unlike #shared_lock, this method will raise AlreadyLocked if
    # no lock can be obtained, instead of blocking.
    #
    # If a lock can be obtained, then the given block will be yielded.
    def try_shared_lock
      File.open(@filename, 'w+') do |f|
        if Fcntl.const_defined? :F_SETFD
          f.fcntl(Fcntl::F_SETFD, Fcntl::FD_CLOEXEC)
        end
        if f.flock(File::LOCK_SH | File::LOCK_NB)
          yield
        else
          raise AlreadyLocked
        end
      end
    end

    # Try to obtain an exclusive lock on the lock file, similar to #exclusive_lock.
    # But unlike #exclusive_lock, this method will raise AlreadyLocked if
    # no lock can be obtained, instead of blocking.
    #
    # If a lock can be obtained, then the given block will be yielded.
    def try_exclusive_lock
      File.open(@filename, 'w') do |f|
        if Fcntl.const_defined? :F_SETFD
          f.fcntl(Fcntl::F_SETFD, Fcntl::FD_CLOEXEC)
        end
        if f.flock(File::LOCK_EX | File::LOCK_NB)
          yield
        else
          raise AlreadyLocked
        end
      end
    end
  end # class LockFile
end # class DaemonController
end # module PhusionPassenger
