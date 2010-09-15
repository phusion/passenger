#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

require 'phusion_passenger/utils'

module PhusionPassenger

# This class maintains a collection of AbstractServer objects. One can add new
# AbstractServer objects, or look up existing ones via a key.
# AbstractServerCollection also automatically takes care of cleaning up
# AbstractServers that have been idle for too long.
#
# This class exists because both SpawnManager and ClassicRails::FrameworkSpawner need this kind
# of functionality. SpawnManager maintains a collection of ClassicRails::FrameworkSpawner
# and ClassicRails::ApplicationSpawner objects, while ClassicRails::FrameworkSpawner maintains a
# collection of ClassicRails::ApplicationSpawner objects.
#
# This class is thread-safe as long as the specified thread-safety rules are followed.
class AbstractServerCollection
	attr_reader :next_cleaning_time
	
	include Utils
	
	def initialize
		@collection = {}
		@lock = Mutex.new
		@cleanup_lock = Mutex.new
		@cond = ConditionVariable.new
		@done = false
		
		# The next time the cleaner thread should check for idle servers.
		# The value may be nil, in which case the value will be calculated
		# at the end of the #synchronized block.
		#
		# Invariant:
		#    if value is not nil:
		#       There exists an s in @collection with s.next_cleaning_time == value.
		#       for all s in @collection:
		#          if eligable_for_cleanup?(s):
		#             s.next_cleaning_time <= value
		@next_cleaning_time = Time.now + 60 * 60
		@next_cleaning_time_changed = false
		
		@cleaner_thread = Thread.new do
			begin
				@lock.synchronize do
					cleaner_thread_main
				end
			rescue Exception => e
				print_exception(self.class.to_s, e)
			end
		end
	end
	
	# Acquire the lock for this AbstractServerCollection object, and run
	# the code within the block. The entire block will be a single atomic
	# operation.
	def synchronize
		@lock.synchronize do
			@in_synchronize_block = true
			begin
				yield
			ensure
				if @next_cleaning_time.nil?
					@collection.each_value do |server|
						if @next_cleaning_time.nil? ||
						   (eligable_for_cleanup?(server) &&
						    server.next_cleaning_time < @next_cleaning_time
						   )
							@next_cleaning_time = server.next_cleaning_time
						end
					end
					if @next_cleaning_time.nil?
						# There are no servers in the collection with an idle timeout.
						@next_cleaning_time = Time.now + 60 * 60
					end
					@next_cleaning_time_changed = true
				end
				if @next_cleaning_time_changed
					@next_cleaning_time_changed = false
					@cond.signal
				end
				@in_synchronize_block = false
			end
		end
	end
	
	# Lookup and returns an AbstractServer with the given key.
	#
	# If there is no AbstractSerer associated with the given key, then the given
	# block will be called. That block must return an AbstractServer object. Then,
	# that object will be stored in the collection, and returned.
	#
	# The block must set the 'max_idle_time' attribute on the AbstractServer.
	# AbstractServerCollection's idle cleaning interval will be adapted to accomodate
	# with this. Changing the value outside this block is not guaranteed to have any
	# effect on the idle cleaning interval.
	# A max_idle_time value of nil or 0 means the AbstractServer will never be idle cleaned.
	#
	# If the block raises an exception, then the collection will not be modified,
	# and the exception will be propagated.
	#
	# Precondition: this method must be called within a #synchronize block.
	def lookup_or_add(key)
		raise ArgumentError, "cleanup() has already been called." if @done
		must_be_in_synchronize_block
		server = @collection[key]
		if server
			register_activity(server)
			return server
		else
			server = yield
			if !server.respond_to?(:start)
				raise TypeError, "The block didn't return a valid AbstractServer object."
			end
			if eligable_for_cleanup?(server)
				server.next_cleaning_time = Time.now + server.max_idle_time
				if @next_cleaning_time && server.next_cleaning_time < @next_cleaning_time
					@next_cleaning_time = server.next_cleaning_time
					@next_cleaning_time_changed = true
				end
			end
			@collection[key] = server
			return server
		end
	end
	
	# Checks whether there's an AbstractServer object associated with the given key.
	#
	# Precondition: this method must be called within a #synchronize block.
	def has_key?(key)
		must_be_in_synchronize_block
		return @collection.has_key?(key)
	end
	
	# Checks whether the collection is empty.
	#
	# Precondition: this method must be called within a #synchronize block.
	def empty?
		must_be_in_synchronize_block
		return @collection.empty?
	end
	
	# Deletes from the collection the AbstractServer that's associated with the
	# given key. If no such AbstractServer exists, nothing will happen.
	#
	# If the AbstractServer is started, then it will be stopped before deletion.
	#
	# Precondition: this method must be called within a #synchronize block.
	def delete(key)
		raise ArgumentError, "cleanup() has already been called." if @done
		must_be_in_synchronize_block
		server = @collection[key]
		if server
			if server.started?
				server.stop
			end
			@collection.delete(key)
			if server.next_cleaning_time == @next_cleaning_time
				@next_cleaning_time = nil
			end
		end
	end
	
	# Notify this AbstractServerCollection that +server+ has performed an activity.
	# This AbstractServerCollection will update the idle information associated with +server+
	# accordingly.
	#
	# lookup_or_add already automatically updates idle information, so you only need to
	# call this method if the time at which the server has performed an activity is
	# not close to the time at which lookup_or_add had been called.
	#
	# Precondition: this method must be called within a #synchronize block.
	def register_activity(server)
		must_be_in_synchronize_block
		if eligable_for_cleanup?(server)
			if server.next_cleaning_time == @next_cleaning_time
				@next_cleaning_time = nil
			end
			server.next_cleaning_time = Time.now + server.max_idle_time
		end
	end
	
	# Tell the cleaner thread to check the collection as soon as possible, instead
	# of sleeping until the next scheduled cleaning time.
	#
	# Precondition: this method must NOT be called within a #synchronize block.
	def check_idle_servers!
		must_not_be_in_synchronize_block
		@lock.synchronize do
			@next_cleaning_time = Time.now - 60 * 60
			@cond.signal
		end
	end
	
	# Iterate over all AbstractServer objects.
	#
	# Precondition: this method must be called within a #synchronize block.
	def each
		must_be_in_synchronize_block
		each_pair do |key, server|
			yield server
		end
	end
	
	# Iterate over all keys and associated AbstractServer objects.
	#
	# Precondition: this method must be called within a #synchronize block.
	def each_pair
		raise ArgumentError, "cleanup() has already been called." if @done
		must_be_in_synchronize_block
		@collection.each_pair do |key, server|
			yield(key, server)
		end
	end
	
	# Delete all AbstractServers from the collection. Each AbstractServer will be
	# stopped, if necessary.
	#
	# Precondition: this method must be called within a #synchronize block.
	def clear
		must_be_in_synchronize_block
		@collection.each_value do |server|
			if server.started?
				server.stop
			end
		end
		@collection.clear
		@next_cleaning_time = nil
	end
	
	# Cleanup all resources used by this AbstractServerCollection. All AbstractServers
	# from the collection will be deleted. Each AbstractServer will be stopped, if
	# necessary. The background thread which removes idle AbstractServers will be stopped.
	#
	# After calling this method, this AbstractServerCollection object will become
	# unusable.
	#
	# Precondition: this method must *NOT* be called within a #synchronize block.
	def cleanup
		must_not_be_in_synchronize_block
		@cleanup_lock.synchronize do
			return if @done
			@lock.synchronize do
				@done = true
				@cond.signal
			end
			@cleaner_thread.join
			synchronize do
				clear
			end
		end
	end

private
	def cleaner_thread_main
		while !@done
			current_time = Time.now
			# We add a 0.2 seconds delay to the sleep time because system
			# timers are not entirely accurate.
			sleep_time = (@next_cleaning_time - current_time).to_f + 0.2
			if sleep_time > 0 && @cond.timed_wait(@lock, sleep_time)
				next
			else
				keys_to_delete = nil
				@next_cleaning_time = nil
				@collection.each_pair do |key, server|
					if eligable_for_cleanup?(server)
						# Cleanup this server if its idle timeout has expired.
						if server.next_cleaning_time <= current_time
							keys_to_delete ||= []
							keys_to_delete << key
							if server.started?
								server.stop
							end
						# If not, then calculate the next cleaning time because
						# we're iterating the collection anyway.
						elsif @next_cleaning_time.nil? ||
						      server.next_cleaning_time < @next_cleaning_time
							@next_cleaning_time = server.next_cleaning_time
						end
					end
				end
				if keys_to_delete
					keys_to_delete.each do |key|
						@collection.delete(key)
					end
				end
				if @next_cleaning_time.nil?
					# There are no servers in the collection with an idle timeout.
					@next_cleaning_time = Time.now + 60 * 60
				end
			end
		end
	end
	
	# Checks whether the given server is eligible for being idle cleaned.
	def eligable_for_cleanup?(server)
		return server.max_idle_time && server.max_idle_time != 0
	end
	
	def must_be_in_synchronize_block
		if !@in_synchronize_block
			raise RuntimeError, "This method may only be called within a #synchronize block!"
		end
	end
	
	def must_not_be_in_synchronize_block
		if @in_synchronize_block
			raise RuntimeError, "This method may NOT be called within a #synchronize block!"
		end
	end
end

end # module PhusionPassenger
