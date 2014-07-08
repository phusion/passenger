# This Vagrantfile sets up an Ubuntu VM, for the purpose of developing Phusion Passenger itself.
# It is NOT for setting up a Vagrant VM for the purpose of developing your own app. See:
# https://github.com/phusion/passenger/issues/1230#issuecomment-48337881

# Vagrantfile API/syntax version. Don't touch unless you know what you're doing!
VAGRANTFILE_API_VERSION = "2"

CPUS   = ENV.fetch('CPUS', 2).to_i
MEMORY = ENV.fetch('MEMORY', 2048).to_i

Vagrant.configure(VAGRANTFILE_API_VERSION) do |config|
  config.vm.box = "phusion-open-ubuntu-14.04-amd64"
  config.vm.box_url = "https://oss-binaries.phusionpassenger.com/vagrant/boxes/latest/ubuntu-14.04-amd64-vbox.box"
  config.ssh.forward_agent = true

  # Use NFS to mount /vagrant because our unit tests expect a
  # POSIX compliant filesystem.
  config.vm.synced_folder ".", "/vagrant", :type => "nfs"

  # Passenger Standalone and 'rails server'
  config.vm.network :forwarded_port, :host => 3000, :guest => 3000
  config.vm.network :forwarded_port, :host => 3001, :guest => 3001
  config.vm.network :forwarded_port, :host => 3002, :guest => 3002
  # Apache
  config.vm.network :forwarded_port, :host => 8000, :guest => 8000
  config.vm.network :forwarded_port, :host => 8001, :guest => 8001
  config.vm.network :forwarded_port, :host => 8002, :guest => 8002
  config.vm.network :forwarded_port, :host => 8003, :guest => 8003
  config.vm.network :forwarded_port, :host => 8004, :guest => 8004
  config.vm.network :forwarded_port, :host => 8005, :guest => 8005
  config.vm.network :forwarded_port, :host => 8010, :guest => 8010
  # Nginx
  config.vm.network :forwarded_port, :host => 8100, :guest => 8100
  config.vm.network :forwarded_port, :host => 8101, :guest => 8101
  config.vm.network :forwarded_port, :host => 8102, :guest => 8102
  config.vm.network :forwarded_port, :host => 8103, :guest => 8103
  config.vm.network :forwarded_port, :host => 8104, :guest => 8104
  config.vm.network :forwarded_port, :host => 8105, :guest => 8105
  config.vm.network :forwarded_port, :host => 8110, :guest => 8110

  config.vm.provider :virtualbox do |vb, override|
    override.vm.network :private_network, :type => "dhcp"
    vb.cpus   = CPUS
    vb.memory = MEMORY
  end

  config.vm.provider :vmware_fusion do |vf, override|
    override.vm.box_url = "https://oss-binaries.phusionpassenger.com/vagrant/boxes/latest/ubuntu-14.04-amd64-vmwarefusion.box"
    vf.vmx["numvcpus"] = CPUS.to_s
    vf.vmx["memsize"]  = MEMORY.to_s
  end

  config.vm.provision :shell, :path => "dev/vagrant/provision.sh"
end
