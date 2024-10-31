.. SPDX-License-Identifier: GPL-2.0

==========================
EDAC Memory Repair Control
==========================

Copyright (c) 2024 HiSilicon Limited.

:Author:   Shiju Jose <shiju.jose@huawei.com>
:License:  The GNU Free Documentation License, Version 1.2
          (dual licensed under the GPL v2)
:Original Reviewers:

- Written for: 6.13

Introduction
------------
Memory devices may support memory repair and maintenance operations to
perform repairs on faulty memory media. Various types of memory repair
features are available, such as Post Package Repair (PPR) and memory
sparing.

Post Package Repair(PPR)
~~~~~~~~~~~~~~~~~~~~~~~~
PPR maintenance operation requests the memory device to perform a repair
operation on its media if supported. A memory device may support two types
of PPR: Hard PPR (hPPR), for a permanent row repair and Soft PPR (sPPR),
for a temporary row repair. sPPR is much faster than hPPR, but the repair
is lost with a power cycle. During the execution of a PPR maintenance
operation, a memory device, may or may not retain data and may or may not
be able to process memory requests correctly. sPPR maintenance operation
may be executed at runtime, if data is retained and memory requests are
correctly processed. hPPR maintenance operation may be executed only at
boot because data would not be retained. In CXL devices, sPPR and hPPR
repair operations may be supported (CXL spec rev 3.1 sections 8.2.9.7.1.2
and 8.2.9.7.1.3).

Memory Sparing
~~~~~~~~~~~~~~
Memory sparing is defined as a repair function that replaces a portion of
memory with a portion of functional memory at that same DPA. User space
tool, e.g. rasdaemon, may request the sparing operation for a given
address for which the uncorrectable error is reported. In CXL,
(CXL spec 3.1 section 8.2.9.7.1.4) subclasses for sparing operation vary
in terms of the scope of the sparing being performed. Cacheline sparing
subclass refers to a sparing action that can replace a full cacheline.
Row sparing is provided as an alternative to PPR sparing functions and its
scope is that of a single DDR row. Bank sparing allows an entire bank to
be replaced. Rank sparing is defined as an operation in which an entire
DDR rank is replaced.

Use cases of generic memory repair features control
---------------------------------------------------

1. The Soft PPR (sPPR), Hard PPR (hPPR), and memory-sparing features share
similar control interfaces. Therefore, there is a need for a standardized,
generic sysfs repair control that is exposed to userspace and used by
administrators, scripts, and tools.

2. When a CXL device detects a failure in a memory component, it may inform
the host of the need for a repair maintenance operation by using an event
record where the "maintenance needed" flag is set. The event record
specifies the DPA that requires repair. The kernel reports the corresponding
CXL general media or DRAM trace event to userspace, and userspace tools
(e.g., rasdaemon) initiate a repair maintenance operation in response to
the device request using the sysfs repair control.

3. Userspace tools, such as rasdaemon, may request a PPR/sparing on a memory
region when an uncorrected memory error or an excess of corrected memory
errors is reported on that memory.

4. Multiple PPR/sparing instances may be present per memory device.

The File System
---------------

The control attributes of a registered scrubber instance could be
accessed in the

/sys/bus/edac/devices/<dev-name>/mem_repairX/

sysfs
-----

Sysfs files are documented in

`Documentation/ABI/testing/sysfs-edac-memory-repair`.

Example
-------

The usage takes the form shown in this example:

1. CXL memory device sPPR

# read capabilities

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/dpa_support

1

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/nibble_mask

0x0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/persist_mode_avail

0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/persist_mode

0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/repair_type

0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/repair_safe_when_in_use

1

# set and readback attributes

root@localhost:~# echo 0x8a2d > /sys/bus/edac/devices/cxl_mem0/mem_repair0/nibble_mask

root@localhost:~# echo 0x300000 >  /sys/bus/edac/devices/cxl_mem0/mem_repair0/dpa

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/dpa

0x300000

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/nibble_mask

0x8a2d

# issue repair operations

# query and reapir return error if unsupported/failed.

root@localhost:~# echo 1 > /sys/bus/edac/devices/cxl_mem0/mem_repair0/query

root@localhost:~# echo 1 > /sys/bus/edac/devices/cxl_mem0/mem_repair0/repair

1.2. CXL memory sparing

# read capabilities

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/repair_type

2

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/dpa_support

1

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/persist_mode_avail

0,1

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/persist_mode

0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/repair_safe_when_in_use

1

#set and readback attributes

root@localhost:~# echo 1 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/bank_group

root@localhost:~# echo 3 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/bank

root@localhost:~# echo 2 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/channel

root@localhost:~# echo  7 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/rank

root@localhost:~# echo 0x4fb9 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/row

root@localhost:~# echo 5 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/sub_channel

root@localhost:~# echo 11 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/column

root@localhost:~# echo 0x85c2 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/nibble_mask

root@localhost:~# echo 0x700000 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/dpa

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/bank_group

1

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/bank

3

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/channel

2

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/rank

7

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/row

0x4fb9

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/sub_channel

5

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/column

11

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/nibble_mask

0x85c2

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/dpa

0x700000

# issue repair operations

# query and repair return error if unsupported/failed.

root@localhost:~# echo 1 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/query

root@localhost:~# echo 1 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/repair
