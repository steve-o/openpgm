#!/bin/sh
aclocal
automake --add-missing --copy
autoreconf --install
