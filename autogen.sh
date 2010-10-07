# autogen.sh
#
# Usage: sh autogen.sh
# Run this in the top directory to regenerate all the files.
# NB: You must have gnulib-tool on the PATH.  If you want to
# force gnulib-tool to "--add-import" (rather than "--update"),
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
else act=add-import
fi
gnulib-tool --${act}
autoreconf --install --symlink

# These override what ‘autoreconf --install’ creates.
# Another way is to use gnulib's config/srclist-update.
actually ()
{
    gnulib-tool --copy-file $1 $2
}
actually doc/INSTALL.UTF-8 INSTALL
actually build-aux/config.guess
actually build-aux/config.sub
actually build-aux/install-sh
actually build-aux/missing
actually build-aux/mdate-sh
actually build-aux/texinfo.tex
actually build-aux/depcomp
actually doc/fdl.texi

# We aren't really interested in the backup files.
rm -f INSTALL~ build-aux/*~

# autogen.sh ends here
