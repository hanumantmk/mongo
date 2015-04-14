#!/bin/sh

curl -L -O -k https://github.com/v8/v8-git-mirror/archive/4.1.0.27.tar.gz
tar -zxvf 4.1.0.27.tar.gz --strip-components 1 --exclude-from upstream_ignore
rm 4.1.0.27.tar.gz
