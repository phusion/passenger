#!/usr/bin/env python
vars = { 'config_opts': {} }
execfile("/etc/mock/default.cfg", vars)
print(vars['config_opts']['dist'])
