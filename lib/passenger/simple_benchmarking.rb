class Object # :nodoc:
	@@benchmark_results = {}

	def b!(name)
		time1 = Time.now
		begin
			yield
		ensure
			time2 = Time.now
			@@benchmark_results[name] = 0 unless @@benchmark_results.has_key?(name)
			@@benchmark_results[name] += time2 - time1
		end 
	end

	def benchmark_report
		total = 0
		@@benchmark_results.each_value do |time|
			total += time
		end
		@@benchmark_results.each_pair do |name, time|
			printf "%-12s: %.4f (%.2f%%)\n", name, time, time / total * 100
		end
		printf "-- Total: %.4f\n", total
	end
end
