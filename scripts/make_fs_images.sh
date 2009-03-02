#!/bin/sh

# Build filesystem images from directories.

mkfs.jffs2 -s 2048 -e 131072 -d home_fs/ -o jffs2.fs
mkcramfs ro_fs/ ro.fs
mkcramfs root_fs/ root.fs
