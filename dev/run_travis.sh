#!/bin/bash
set -ev
rake apache2
rake nginx
