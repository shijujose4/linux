.. SPDX-License-Identifier: GPL-2.0

===================
EDAC Scrub control
===================

Copyright (c) 2024 HiSilicon Limited.

:Author:   Shiju Jose <shiju.jose@huawei.com>
:License:  The GNU Free Documentation License, Version 1.2
          (dual licensed under the GPL v2)
:Original Reviewers:

- Written for: 6.12
- Updated for:

Introduction
------------
The EDAC enhancement for RAS featurues exposes interfaces for controlling
the memory scrubbers in the system. The scrub device drivers in the
system register with the EDAC scrub. The driver exposes the
scrub controls to user in the sysfs.

The File System
---------------

The control attributes of the registered scrubber instance could be
accessed in the /sys/bus/edac/devices/<dev-name>/scrub*/

sysfs
-----

Sysfs files are documented in
`Documentation/ABI/testing/sysfs-edac-scrub-control`.

Example
-------

The usage takes the form shown in this example::

1. CXL memory device patrol scrubber
1.1 device based
root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/scrub0/min_cycle_duration
3600
root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/scrub0/max_cycle_duration
918000
root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/scrub0/current_cycle_duration
43200
root@localhost:~# echo 54000 > /sys/bus/edac/devices/cxl_mem0/scrub0/current_cycle_duration
root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/scrub0/current_cycle_duration
54000
root@localhost:~# echo 1 > /sys/bus/edac/devices/cxl_mem0/scrub0/enable_background
root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/scrub0/enable_background
1
root@localhost:~# echo 0 > /sys/bus/edac/devices/cxl_mem0/scrub0/enable_background
root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/scrub0/enable_background
0

1.2. region based
root@localhost:~# cat /sys/bus/edac/devices/cxl_region0/scrub0/min_cycle_duration
3600
root@localhost:~# cat /sys/bus/edac/devices/cxl_region0/scrub0/max_cycle_duration
918000
root@localhost:~# cat /sys/bus/edac/devices/cxl_region0/scrub0/current_cycle_duration
43200
root@localhost:~# echo 54000 > /sys/bus/edac/devices/cxl_region0/scrub0/current_cycle_duration
root@localhost:~# cat /sys/bus/edac/devices/cxl_region0/scrub0/current_cycle_duration
54000
root@localhost:~# echo 1 > /sys/bus/edac/devices/cxl_region0/scrub0/enable_background
root@localhost:~# cat /sys/bus/edac/devices/cxl_region0/scrub0/enable_background
1
root@localhost:~# echo 0 > /sys/bus/edac/devices/cxl_region0/scrub0/enable_background
root@localhost:~# cat /sys/bus/edac/devices/cxl_region0/scrub0/enable_background
0
