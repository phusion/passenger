# Phusion Passenger Developer QuickStart

<a href="https://vimeo.com/phusionnl/review/97427161/15cb4cc59a"><img src="http://blog.phusion.nl/wp-content/uploads/2014/06/passenger_developer_quickstart.png"></a>

_Watch the Developer QuickStart screencast_

Phusion Passenger provides an easy and convenient development environment that contributors can use. The development environment is a 64-bit Ubuntu 14.04 virtual machine and contains everything that you need. The build toolchain is already setup, all dependencies are installed and web servers are preconfigured. You can start coding almost immediately. And because it's a virtual machine, it doesn't matter which host operating system you're using.

You use this development environment through [Vagrant](http://www.vagrantup.com/) and [VirtualBox](https://www.virtualbox.org/). Vagrant is an extremely useful tool for managing virtual machines, while VirtualBox is virtualization software. Both tools are free and open source. Vagrant allows you to easily share a directory between the host OS and the VM. This means that you can use the editor on your host OS to edit source files, and compile inside the VM.

## Getting started

 1. Install [Vagrant](http://www.vagrantup.com/).
 2. Install [VirtualBox](http://www.virtualbox.org/).
 3. Inside the Phusion Passenger source tree, run: `vagrant up`. This will spin up the VM and will set it up. This can take a while so feel free to grab a cup of coffee.
 4. Once the VM has been setup, login to it by running: `vagrant ssh`.

## Workflow

The workflow is to:

 * Edit code on the host.
 * Use git commands on the host.
 * Compile in the VM.
 * Run tests in the VM.

The Phusion Passenger source code is located in /vagrant.

When you're done developing Phusion Passenger, you can shut down the VM by running `vagrant halt`. Next time you want to spin it up again, run `vagrant up`.

## Starting and accessing Apache

Apache is installed, but it's not set up with Phusion Passenger by default. So you must first compile the Phusion Passenger Apache module:

    cd /vagrant
    rake apache2

Next, enable the Phusion Passenger module and restart Apache:

    sudo a2enmod passenger
    sudo service apache2 restart

You can now access Apache from the host on http://127.0.0.1:8000 through :8005. The VM has a sample Ruby Rack application configured on http://127.0.0.1:8001/. Visit that URL and see it in action. Its source code is located in `dev/rack.test` in the Phusion Passenger source tree.

## Starting and accessing Nginx

The Nginx source code and binaries are located in /home/vagrant/nginx. If this is the first time you use Nginx in this VM, you must install it. Run:

    cd /home/vagrant/nginx
    rake bootstrap

Next, start Nginx by using a script that the development environment provides:

    ./start

You can now access Nginx from the host on http://127.0.0.1:8100 through :8105. The VM has a sample Ruby Rack application configured on http://127.0.0.1:8101/. Visit that URL and see it in action. Its source code is located in `dev/rack.test` in the Phusion Passenger source tree.

## Running tests

Tests can be run immediately without any setup.

    rake test:cxx
    rake test:integration:apache2
    rake test:integration:nginx

## Further reading

 * [Contributors Guide](https://github.com/phusion/passenger/blob/master/CONTRIBUTING.md)
 * [Design and Architecture](https://www.phusionpassenger.com/documentation/Design%20and%20Architecture.html)
 * [Coding Tips and Pitfalls](https://github.com/phusion/passenger/blob/master/doc/CodingTipsAndPitfalls.md)
