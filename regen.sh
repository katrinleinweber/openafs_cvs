#!/bin/sh

while getopts "q" flag
do
    case "$flag" in
	q)
	    skipman=1;
	    ;;
	*)
	    echo "Usage ./regen.sh [-q]"
	    echo "	-q skips man page generation"
	    exit
	    ;;
    esac
done

echo "Updating configuration..."
echo "Running aclocal"
aclocal -I src/cf
echo "Running autoconf"
autoconf
echo "Running autoconf for configure-libafs"
autoconf configure-libafs.in > configure-libafs
chmod +x configure-libafs
echo "Running autoheader"
autoheader
#echo "Running automake"
#automake

echo "Deleting autom4te.cache directory"
rm -r autom4te.cache

if [ $skipman ] ; then
    echo "Skipping man page build"
else 
    # Rebuild the man pages, to not require those building from source to have
    # pod2man available.
    if test -d doc/man-pages ; then
        echo "Building man pages"
        (cd doc/man-pages && ./generate-man)
    fi
fi
