[![Gem Version](https://badge.fury.io/rb/passenger.svg)](https://badge.fury.io/rb/passenger)
# <img src="images/passenger_logo.svg" alt="passenger logo" style="margin-bottom: -.2em; width: 1.4em"> Phusion Passenger
<h3>Supercharge your Ruby, Node.js and Python apps</h3>

[Phusion Passenger™](https://www.phusionpassenger.com/) is a web server and application server, designed to be fast, robust and lightweight. It takes a lot of complexity out of deploying web apps, adds powerful enterprise-grade features that are useful in production, and makes administration much easier and less complex. Phusion Passenger supports Ruby, Python, Node.js and Meteor, and is being used by high-profile companies such as **Apple, Pixar, New York Times, AirBnB, Juniper** etc as well as [over 650.000 websites](http://trends.builtwith.com/Web-Server/Phusion-Passenger).

<a href="https://vimeo.com/224923750"><img src="https://github.com/phusion/passenger/blob/stable-5.2/images/justin.png" height="400"></a><br><em>Phusion Passenger - the smart app server</em>

<p>What makes Passenger so fast and reliable is its <strong>C++</strong> core, its <strong>zero-copy</strong> architecture, its <strong>watchdog</strong> system and its <strong>hybrid</strong> evented, multi-threaded and multi-process design.</p>

## Fuse Panel - GUI for Passenger
The <strong>smarter</strong> and <strong>simpler command center</strong> for your applications. <strong><a href="https://www.phusionpassenger.com/fuse-panel" target="_blank">Fuse Panel</a></strong> monitors your apps in near real-time, offers insights in your logs and enables analyzing and troubleshooting issues. New and exclusively available in the Fuse Panel is much-requested feature <strong>per-application log viewing and splitting</strong>.

Regardless of if you’re running Passenger open source or you’re one of our enterprise customers, the Fuse Panel is available for free while in beta. <a href="https://www.phusionpassenger.com/fuse-panel" target="_blank">Give it a try</a>.

<img src="https://github.com/phusion/passenger/blob/stable-5.2/images/spark.png" align="left" width="300">

### Learn more:
- [Website](https://www.phusionpassenger.com/)
- [Fuse Panel](https://www.phusionpassenger.com/fuse-panel)
- [Documentation &amp; Support](https://www.phusionpassenger.com/support)
- [Consultancy](https://www.phusion.nl/consultancy)
- [Twitter](https://twitter.com/phusion_nl)
- [Blog](http://blog.phusion.nl/)

<br/><br/><br/><br/><br/>

## Installation

Please follow [the installation instructions on the website](https://www.phusionpassenger.com/library/install/).

### Installing the source directly from git

If you mean to install the latest version of Passenger directly from this git repository, then you should run one of the following commands. Installing from the git repository is basically the same as the tarball installation method, as [described in the manual](https://www.phusionpassenger.com/library/install/), with one exception: you need to clone git submodules:

    git submodule update --init --recursive

After that, run one of the following:

    ./bin/passenger-install-apache2-module

-OR-

    ./bin/passenger-install-nginx-module

-OR-

    # From your application directory
    ~/path-to-passenger/bin/passenger start

For troubleshooting, configuration and tips, please also refer to the above documentation. For further support, please refer to [the Phusion Passenger support page](https://www.phusionpassenger.com/support).

Ruby users can also build a gem from the Git repository and install the gem.

    gem build passenger.gemspec
    gem install passenger-x.x.x.gem

## Further reading

 * The `doc/` directory.
 * [Contributors Guide](https://github.com/phusion/passenger/blob/master/CONTRIBUTING.md)
 * [Phusion Passenger support page](https://www.phusionpassenger.com/support)
 * [Phusion Passenger release notes](https://blog.phusion.nl/tag/passenger-releases/)

## Legal

"Passenger" and "Phusion Passenger" are registered trademarks of Phusion Holding B.V.
