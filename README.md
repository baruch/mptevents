mptevents
=========

The MPT drivers from LSI have an event reporting mechanism built in to the
driver which enables to get a sneak peak into what happens in the SAS network
at a level below that of the OS itself and can provide insights when debugging
SAS issues.

mptevents is a small daemon intended to expose that information by reporting it
all to syslog.

How to Use
----------

Just make sure to run this daemon at system startup or when debugging is
needed, give it the device name to open such as /dev/mptctl or /dev/mpt2ctl and
it will send syslog messages.

License
-------

My code is licensed under the MIT license (see LICENSE file). The files under
the mpt directory are taken verbatim from the Linux kernel and are thus
licensed under the GPLv2.

Author
------

Baruch Even <baruch@ev-en.org>
