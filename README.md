# <img src="images/passenger_logo.svg" alt="passenger logo" style="margin-bottom: -.2em; width: 1.4em"> Phusion Passenger
<h3>SuperCharge your Ruby, Node.js and Python apps</h3>

<video id="video" class="video" preload="metadata" controls height="400" style="margin-bottom: 2em">
    <source src="https://player.vimeo.com/external/224923750.hd.mp4?s=6931550c8a2bedabba0822a6ec7966c45ee1fbc4&profile_id=174" type="video/mp4">
</video>

[Phusion Passenger™](https://www.phusionpassenger.com/) is a web server and application server, designed to be fast, robust and lightweight. It takes a lot of complexity out of deploying web apps, adds powerful enterprise-grade features that are useful in production, and makes administration much easier and less complex. Phusion Passenger supports Ruby, Python, Node.js and Meteor, and is being used by high-profile companies such as **Apple, Pixar, New York Times, AirBnB, Juniper** etc as well as [over 650.000 websites](http://trends.builtwith.com/Web-Server/Phusion-Passenger).

<div style="display: flex; margin-bottom: 2em

">
	<img src="images/spark.png" alt="spark" width="30%" style="align-self: flex-start; margin-top: 2em">
	<div style="margin-left: 5em">
		<p>What makes it so fast and reliable is its <strong>C++</strong> core, its <strong>zero-copy</strong> architecture, its <strong>watchdog</strong> system and its <strong>hybrid</strong> evented, multi-threaded and multi-process design.</p>
		<h3>Learn more:</h3>
		<ul>
			<li><a href="https://www.phusionpassenger.com/">Website</a></li>
			<li><a href="https://www.phusionpassenger.com/support">Documentation &amp; Support</a></li>
			<li><a href="https://twitter.com/phusion_nl">Twitter</a></li>
			<li><a href="http://blog.phusion.nl/">Blog</a></li>
		</ul>
	</div>
</div>

## Installation

Please follow [the installation instructions on the website](https://www.phusionpassenger.com/get_it_now).

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
 * https://www.phusionpassenger.com/support

## Legal

"Passenger", "Phusion Passenger" and "Union Station" are registered trademarks of Phusion Holding B.V.
