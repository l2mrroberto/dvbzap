# dvbzap
Tune/zap DVB card with mumudvb configuration file/variables

# description
~~~~~~~~~~~~
dvbzap is a program who tune DVB adapter.
Usage: dvbzap [options]
-c, --config : MuMuDVB Config file
-l, --list-cards : List the DVB cards and exit
-a, --card            : The DVB card to use (overrided by the configuration file)
-f, --freq            : config freq
-p, --pol             : config pol
-d, --delivery_system : config delivery_system
-s, --srate           : config srate
-m, --modulation      : config modulation
-r, --coderate        : config coderate
-n, --sat_number      : config sat_number
-l, --lnb_type        : config lnb_type
-z, --lnb_slof         : config lnb_slof
-x, --lnb_lof_standard : config lnb_lof_standard
-v, --lnb_lof_low      : config lnb_lof_low
-b, --lnb_lof_high     : config lnb_lof_high
-q           : Less verbose
-h, --help   : Help

DVBZAP Version 0.1
Built with support for DVB API Version 5.10.
Built with support for DVB-T2.
---------
Based on MUMUDVB
Originally based on dvbstream 0.6 by (C) Dave Chapman 2001-2004
Released under the GPL.
Latest version available from https://github.com/l2mrroberto/dvbzap
by mrroberto TVEpg.eu <l2mrroberto@gmail.com>
~~~~~~~~~~~~

#Installation
------------

From sources
~~~~~~~~~~~~
From a snapshot
~~~~~~~~~~~~

If you downloaded a snapshot, you will have to generate the auto(conf make etc ...) files. In order to do this you will need the autotools, automake, gettext and libtool and, type in the folder of dvbzap

~~~~~~~~~~~~
autoreconf -i -f
~~~~~~~~~~~~

Then you have a source which can be installed as a release package.

From a release package
----------------

In order to install DVBZAP type:

~~~~~~~~~~~~
$ ./configure [configure options]
$ make
# make install
~~~~~~~~~~~~

# TO DO

Clean code
