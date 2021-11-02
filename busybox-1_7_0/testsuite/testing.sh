# Simple test harness infrastructurei for BusyBox
#
# Copyright 2005 by Rob Landley
#
# License is GPLv2, see LICENSE in the busybox tarball for full license text.

# This file defines two functions, "testing" and "optionflag"

# The following environment variables may be set to enable optional behavior
# in "testing":
#    VERBOSE - Print the diff -u of each failed test case.
#    DEBUG - Enable command tracing.
#    SKIP - do not perform this test (this is set by "optionflag")
#
# The "testing" function takes five arguments:
#	$1) Description to display when running command
#	$2) Command line arguments to command
#	$3) Expected result (on stdout)
#	$4) Data written to file "input"
#	$5) Data written to stdin
#
# The exit value of testing is the exit value of the command it ran.
#
# The environment variable "FAILCOUNT" contains a cumulative total of the
# number of failed tests.

# The "optional" function is used to skip certain tests, ala:
#   optionflag CONFIG_FEATURE_THINGY
#
# The "optional" function checks the environment variable "OPTIONFLAGS",
# which is either empty (in which case it always clears SKIP) or
# else contains a colon-separated list of features (in which case the function
# clears SKIP if the flag was found, or sets it to 1 if the flag was not found).

export FAILCOUNT=0
export SKIP=

# Helper functions

optional()
{
  option=`echo "$OPTIONFLAGS" | egrep "(^|:)$1(:|\$)"`
  # Not set?
  if [ -z "$1" ] || [ -z "$OPTIONFLAGS" ] || [ ${#option} -ne 0 ]
  then
    SKIP=""
    return
  fi
  SKIP=1
}

# The testing function

testing()
{
  NAME="$1"
  [ -z "$1" ] && NAME=$2

  if [ $# -ne 5 ]
  then
    echo "Test $NAME has the wrong number of arguments ($# $*)" >&2
    exit
  fi

  [ -n "$DEBUG" ] && set -x

  if [ -n "$SKIP" ]
  then
    echo "SKIPPED: $NAME"
    return 0
  fi

  echo -ne "$3" > expected
  echo -ne "$4" > input
  [ -z "$VERBOSE" ] || echo "echo '$5' | $2"
  echo -ne "$5" | eval "$2" > actual
  RETVAL=$?

  cmp expected actual >/dev/null 2>/dev/null
  if [ $? -ne 0 ]
  then
    FAILCOUNT=$[$FAILCOUNT+1]
    echo "FAIL: $NAME"
    [ -n "$VERBOSE" ] && diff -u expected actual
  else
    echo "PASS: $NAME"
  fi
  rm -f input expected actual

  [ -n "$DEBUG" ] && set +x

  return $RETVAL
}

# Recursively grab an executable and all the libraries needed to run it.
# Source paths beginning with / will be copied into destpath, otherwise
# the file is assumed to already be there and only its library dependencies
# are copied.

mkchroot()
{
  [ $# -lt 2 ] && return

  echo -n .

  dest=$1
  shift
  for i in "$@"
  do
    [ "${i:0:1}" == "/" ] || i=$(which $i)
    [ -f "$dest/$i" ] && continue
    if [ -e "$i" ]
    then
      d=`echo "$i" | grep -o '.*/'` &&
      mkdir -p "$dest/$d" &&
      cat "$i" > "$dest/$i" &&
      chmod +x "$dest/$i"
    else
      echo "Not found: $i"
    fi
    mkchroot "$dest" $(ldd "$i" | egrep -o '/.* ')
  done
}

# Set up a chroot environment and run commands within it.
# Needed commands listed on command line
# Script fed to stdin.

dochroot()
{
  mkdir tmpdir4chroot
  mount -t ramfs tmpdir4chroot tmpdir4chroot
  mkdir -p tmpdir4chroot/{etc,sys,proc,tmp,dev}
  cp -L testing.sh tmpdir4chroot

  # Copy utilities from command line arguments

  echo -n "Setup chroot"
  mkchroot tmpdir4chroot $*
  echo

  mknod tmpdir4chroot/dev/tty c 5 0
  mknod tmpdir4chroot/dev/null c 1 3
  mknod tmpdir4chroot/dev/zero c 1 5

  # Copy script from stdin

  cat > tmpdir4chroot/test.sh
  chmod +x tmpdir4chroot/test.sh
  chroot tmpdir4chroot /test.sh
  umount -l tmpdir4chroot
  rmdir tmpdir4chroot
}

