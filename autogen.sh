# autogen.sh
#
# Usage: sh autogen.sh [-f]
# Run this in the top directory to regenerate all the files.
# Option "-f" means forcefully create symlinks for missing files
# (by default: copies are made only if necessary).
#
# NB: You must have gnulib-tool on the PATH.  If you want to
# force gnulib-tool to "--import" (rather than "--update"),
# remove either lib/ or m4/ before invocation.
#
# Tested with:
# autoconf (GNU Autoconf) 2.68
# automake (GNU automake) 1.11.1
# libtool (GNU libtool) 2.4
# gnulib-tool (GNU gnulib 2010-09-26 12:54:30) 0.0.4310-f658

set -ex
if [ -d lib ] && [ -d m4 ]
then act=update
else act=import
fi
gnulib-tool --${act}
autoreconf --install --symlink "$@"

# autogen.sh ends here
