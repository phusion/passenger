module PhusionPassenger

shared_examples_for "a Ruby loader" do
  it "prints an error page if the startup file fails to load" do
    File.write(@stub.startup_file, %q{
      raise "oh no!"
    })
    error = start(:quiet => true)
    expect(error.status).to eq(:premature_exit)
    expect(error.summary).to include("oh no!")
  end

  it "calls the starting_worker_process event after the startup file has been loaded" do
    File.prepend(@stub.startup_file, %q{
      history_file = "history.txt"
      PhusionPassenger.on_event(:starting_worker_process) do |forked|
        ::File.open(history_file, 'a') do |f|
          f.puts "worker_process_started\n"
        end
      end
      ::File.open(history_file, 'a') do |f|
        f.puts "end of startup file\n"
      end
    })
    start
    expect(@process).to be_an_instance_of(AppProcess)
    expect(File.read("#{@stub.app_root}/history.txt")).to eq(
      "end of startup file\n" \
      "worker_process_started\n")
  end

  it "calls the stopping_worker_process event on exit" do
    File.prepend(@stub.startup_file, %q{
      history_file = "history.txt"
      PhusionPassenger.on_event(:stopping_worker_process) do
        ::File.open(history_file, 'a') do |f|
          f.puts "worker_process_stopped\n"
        end
      end
      ::File.open(history_file, 'a') do |f|
        f.puts "end of startup file\n"
      end
    })
    start
    expect(@process).to be_an_instance_of(AppProcess)
    @process.input.close
    eventually(3) do
      File.read("#{@stub.app_root}/history.txt") ==
        "end of startup file\n" \
        "worker_process_stopped\n"
    end
  end
end

end # module PhusionPassenger
