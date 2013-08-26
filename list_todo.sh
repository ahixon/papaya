#!/bin/bash
egrep '(FIXME|TODO|XXX|HACK)' apps/* -R --color
