mptevents
=========

[![Build Status](https://travis-ci.org/baruch/mptevents.svg?branch=master)](https://travis-ci.org/baruch/mptevents)

The MPT drivers from LSI have an event reporting mechanism built in to the
driver which enables to get a sneak peak into what happens in the SAS network
at a level below that of the OS itself and can provide insights when debugging
SAS issues.

mptevents is a small daemon intended to expose that information by reporting it
all to syslog.

How to Use
----------

Just make sure to run this daemon at system startup or when debugging is
needed, give it the device name to open such as /dev/mpt2ctl or /dev/mpt3ctl
and it will send syslog messages.

This daemon will try to auto-detect each supported host in /sys/class/scsi_host.
Any unsupported host (e.g. ahci) will be ignored.

If you give it no arguments it will try to auto-detect the control device and
use that if it finds only one. If more than one is available you'll need to
decide which one to use.

Understanding the logs
----------------------

The logs generated will look something like the following, I will add inline comments to explain some of the things that can be seen here.

    SAS Device Status Change: ioc=1 context=2 tag=ffff rc=8(INTERNAL_DEVICE_RESET) port=0 asc=00 ascq=00 handle=000a reserved2=0 SASAddress=5000cca02b0458ba

Here the device was reset for some reason, the SAS HBA most likely lost contact with the device and issued an internal reset to clear out all associated IOs against the device. We can see the SAS address and the handle both of which identify the device.

    SAS Device Status Change: ioc=1 context=3 tag=ffff rc=14(COMPLETED_INTERNAL_DEV_RESET) port=0 asc=00 ascq=00 handle=000a reserved2=0 SASAddress=5000cca02b0458ba

Here the reset was completed, there were no pending IOs to abort so things were quick (syslog output will provide timestamps as well, at least from the time the event was received by mptevents).

    SAS Discovery: context=12 flags=01(IN_PROGRESS) reason=1(STARTED) physical_port=0 discovery_status=0() reserved1=0
    SAS Device Status Change: ioc=1 context=13 tag=ffff rc=8(INTERNAL_DEVICE_RESET) port=0 asc=00 ascq=00 handle=000a reserved2=0 SASAddress=5000cca02b0458ba
    SAS Topology Change List: context=14 enclosure_handle=2 expander_dev_handle=9 num_phys=37 num_entries=1 start_phy_num=11 exp_status=3(RESPONDING) physical_port=0 reserved1=0 reserved2=0
    SAS Topology Change List Entry (1/1): attached_dev_handle=a link_rate=a(prev=RATE_6_0,next=UNKNOWN_LINK_RATE) phy_status=5(DELAY_NOT_RESPONDING)
    SAS Device Status Change: ioc=1 context=15 tag=ffff rc=14(COMPLETED_INTERNAL_DEV_RESET) port=0 asc=00 ascq=00 handle=000a reserved2=0 SASAddress=5000cca02b0458ba
    SAS Discovery: context=16 flags=00(IN_PROGRESS) reason=2(COMPLETED) physical_port=0 discovery_status=0() reserved1=0

This shows a typical disk dropping from the SAS network, there is a "SAS Discovery" starting first, the information about the device is cleared and we also get a "SAS Topology Change List" that shows a single device switching from 6Gbps to unknown which means there is no device present anymore.

    SAS Discovery: context=17 flags=01(IN_PROGRESS) reason=1(STARTED) physical_port=0 discovery_status=0() reserved1=0
    SAS Topology Change List: context=18 enclosure_handle=2 expander_dev_handle=9 num_phys=37 num_entries=1 start_phy_num=11 exp_status=3(RESPONDING) physical_port=0 reserved1=0 reserved2=0
    SAS Topology Change List Entry (1/1): attached_dev_handle=a link_rate=0(prev=UNKNOWN_LINK_RATE,next=UNKNOWN_LINK_RATE) phy_status=2(DELAY_NOT_RESPONDING)
    SAS Discovery: context=19 flags=02(DEVICE_CHANGE) reason=2(COMPLETED) physical_port=0 discovery_status=0() reserved1=0
    SAS Discovery: context=20 flags=01(IN_PROGRESS) reason=1(STARTED) physical_port=0 discovery_status=0() reserved1=0
    SAS Discovery: context=21 flags=02(DEVICE_CHANGE) reason=2(COMPLETED) physical_port=0 discovery_status=0() reserved1=0
    SAS Topology Change List: context=22 enclosure_handle=2 expander_dev_handle=9 num_phys=37 num_entries=1 start_phy_num=11 exp_status=3(RESPONDING) physical_port=0 reserved1=0 reserved2=0
    SAS Topology Change List Entry (1/1): attached_dev_handle=a link_rate=a0(prev=UNKNOWN_LINK_RATE,next=RATE_6_0) phy_status=1(DELAY_NOT_RESPONDING)

The above now shows how a device joins, again a SAS discovery a topology change list showing a device (weirdly with no new speed, could be associated with a fast removal and reinsertion causing the DELAY\_NOT\_RESPONDING state. In the second discovery we already get the device joining in.

Support
-------

Since the tool is quite obscure and requires understanding of the SAS/SATA protocol and the way things behave at a very low level I'm also happy to give a helping hand by looking at debug logs and trying to explain them. I'm also doing this for my own benefit, while I saw plenty of such traces debugging SAS/SATA issues I'm always interested in seeing more of these. My email is provided below, feel free to contact me with your problems and I'd do my best to help. I cannot guarantee anything besides a sincere attempt to debug such issues.

License
-------

My code is licensed under the MIT license (see LICENSE file). The files under
the mpt directory are taken verbatim from the Linux kernel and are thus
licensed under the GPLv2.

Author
------

Baruch Even <baruch@ev-en.org>
