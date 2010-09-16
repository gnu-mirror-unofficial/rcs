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
# autoconf (GNU Autoconf) 2.67
# automake (GNU automake) 1.11.1
# ltmain.sh (GNU libtool) 2.2.6b
# gnulib-tool (GNU gnulib 2010-09-16 00:25:57) 0.0.4259-55645

set -ex
if [ -d lib ] && [ -d m4 ]
then act=update
else act=import
fi
gnulib-tool --${act}
autoreconf --install --symlink "$@"

# autogen.sh ends here
