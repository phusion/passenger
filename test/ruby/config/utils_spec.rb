require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')

PhusionPassenger.require_passenger_lib 'admin_tools/instance_registry'
PhusionPassenger.require_passenger_lib 'config/utils'

module PhusionPassenger

  describe Config::Utils do
    class TestCommand
      include Config::Utils

      def initialize(options)
        @options = options
      end

      def select_instance
        select_passenger_instance
      end
    end

    def select_instance_exit_status(options)
      begin
        TestCommand.new(options).select_instance
      rescue SystemExit => e
        e.status
      end
    end

    specify 'exits successfully when Passenger is not running and the error is ignored' do
      registry = double('instance registry', list: [])
      expect(AdminTools::InstanceRegistry).to receive(:new).and_return(registry)

      status = nil
      expect {
        status = select_instance_exit_status(ignore_passenger_not_running: true)
      }.to output(/\*\*\* WARNING:/).to_stderr_from_any_process
      expect(status).to eq(0)
    end

    specify 'fails when Passenger is not running by default' do
      registry = double('instance registry', list: [])
      expect(AdminTools::InstanceRegistry).to receive(:new).and_return(registry)

      status = nil
      expect {
        status = select_instance_exit_status({})
      }.to output(/\*\*\* ERROR:/).to_stderr_from_any_process
      expect(status).to eq(1)
    end
  end

end # module PhusionPassenger
