#!/bin/bash
set -ex
set -o pipefail


### Update /etc/hosts

if ! grep -q passenger.test /etc/hosts; then
	cat >>/etc/hosts <<-EOF

127.0.0.1 passenger.test
127.0.0.1 mycook.passenger.test
127.0.0.1 zsfa.passenger.test
127.0.0.1 norails.passenger.test
127.0.0.1 1.passenger.test 2.passenger.test 3.passenger.test
127.0.0.1 4.passenger.test 5.passenger.test 6.passenger.test
127.0.0.1 7.passenger.test 8.passenger.test 9.passenger.test
127.0.0.1 rack.test foobar.test
	EOF
fi


### Update bashrc and bash profile

if ! grep -q bashrc.mine /etc/bash.bashrc; then
	echo ". /etc/bash.bashrc.mine" >> /etc/bash.bashrc
fi
if ! grep -q bashrc.mine /home/vagrant/.bashrc; then
	echo ". /etc/bash.bashrc.mine" >> /home/vagrant/.bashrc
fi
if ! grep -q /vagrant /home/vagrant/.profile; then
	echo "if tty -s; then cd /vagrant; fi" >> /home/vagrant/.profile
fi
cp /vagrant/dev/vagrant/bashrc /etc/bash.bashrc.mine
cp /vagrant/dev/vagrant/sudoers.conf /etc/sudoers.d/passenger
chmod 440 /etc/sudoers.d/passenger


### Install native dependencies

apt-get update
apt-get install -y build-essential git bash-completion ccache wget \
	libxml2-dev libxslt1-dev libsqlite3-dev libcurl4-openssl-dev libpcre3-dev \
	ruby ruby-dev nodejs npm \
	apache2-mpm-worker apache2-threaded-dev


### Install basic gems

if [[ ! -e /usr/local/bin/rake ]]; then
	gem install rake --no-rdoc --no-ri
fi
if [[ ! -e /usr/local/bin/drake ]]; then
	gem install drake --no-rdoc --no-ri
fi
if [[ ! -e /usr/local/bin/bundler ]]; then
	gem install bundler --no-rdoc --no-ri
fi


### Install Phusion Passenger development dependencies

pushd /vagrant
if [[ ! -e ~/.test_deps_installed ]]; then
	rake test:install_deps SUDO=1 DEPS_TARGET=~/bundle
	touch ~/.test_deps_installed
else
	bundle install --path ~/bundle
fi
popd


### Install Nginx source code

pushd /home/vagrant
if [[ ! -e nginx ]]; then
	sudo -u vagrant -H git clone -b branches/stable-1.6 https://github.com/nginx/nginx.git
fi
sudo -u vagrant -H mkdir -p nginx/inst/conf
sudo -u vagrant -H cp /vagrant/dev/vagrant/nginx_start nginx/start
if [[ ! -e nginx/Rakefile ]]; then
	sudo -u vagrant -H cp /vagrant/dev/vagrant/nginx_rakefile nginx/Rakefile
fi
if [[ ! -e nginx/inst/conf/nginx.conf ]]; then
	sudo -u vagrant -H cp /vagrant/dev/vagrant/nginx.conf nginx/inst/conf/
fi
if [[ ! -e nginx/nginx.conf && ! -h nginx/nginx.conf ]]; then
	sudo -u vagrant -H ln -s inst/conf/nginx.conf nginx/nginx.conf
fi
if [[ ! -e nginx/access.log && ! -h nginx/access.log ]]; then
	sudo -u vagrant -H ln -s inst/logs/access.log nginx/access.log
fi
if [[ ! -e nginx/error.log && ! -h nginx/error.log ]]; then
	sudo -u vagrant -H ln -s inst/logs/error.log nginx/error.log
fi
popd


### Set up Apache

should_restart_apache=false
cp /vagrant/dev/vagrant/apache_ports.conf /etc/apache2/ports.conf
cp /vagrant/dev/vagrant/apache_default_site.conf /etc/apache2/sites-available/000-default.conf
if [[ ! -e /etc/apache2/mods-available/passenger.conf ]]; then
	cp /vagrant/dev/vagrant/apache_passenger.conf /etc/apache2/mods-available/passenger.conf
fi
if [[ ! -e /etc/apache2/mods-available/passenger.load ]]; then
	cp /vagrant/dev/vagrant/apache_passenger.load /etc/apache2/mods-available/passenger.load
fi
if [[ ! -e /etc/apache2/sites-available/010-rack.test.conf ]]; then
	cp /vagrant/dev/vagrant/apache_rack_test.conf /etc/apache2/sites-available/010-rack.test.conf
	a2ensite 010-rack.test
	should_restart_apache=true
fi
if $should_restart_apache; then
	service apache2 restart
fi
