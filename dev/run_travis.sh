#!/bin/bash
set -e
echo "$ rake apache2"
rake apache2
echo "$ rake nginx"
rake nginx
echo "$ rake test:ruby"
rake test:ruby
