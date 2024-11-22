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
Memory devices may support repair operations to address issues in their
memory media. Post Package Repair (PPR) and memory sparing are examples
of such features.

Post Package Repair(PPR)
~~~~~~~~~~~~~~~~~~~~~~~~
Post Package Repair is a maintenance operation requests the memory device
to perform repair operation on its media, in detail is a memory self-healing
feature that fixes a failing memory location by replacing it with a spare
row in a DRAM device. For example, a CXL memory device with DRAM components
that support PPR features may implement PPR maintenance operations. DRAM
components may support types of PPR functions, hard PPR, for a permanent row
repair, and soft PPR, for a temporary row repair. Soft PPR is much faster
than hard PPR, but the repair is lost with a power cycle.  The data may not
be retained and memory requests may not be correctly processed during a
repair operation. In such case, the repair operation should not executed
at runtime.
For example, CXL memory devices, soft PPR and hard PPR repair operations
may be supported. See CXL spec rev 3.1 sections 8.2.9.7.1.1 PPR Maintenance
Operations, 8.2.9.7.1.2 sPPR Maintenance Operation and 8.2.9.7.1.3 hPPR
Maintenance Operation for more details.

Memory Sparing
~~~~~~~~~~~~~~
Memory sparing is a repair function that replaces a portion of memory with
a portion of functional memory at that same sparing granularity. Memory
sparing has cacheline/row/bank/rank sparing granularities. For example, in
memory-sparing mode, one memory rank serves as a spare for other ranks on
the same channel in case they fail. The spare rank is held in reserve and
not used as active memory until a failure is indicated, with reserved
capacity subtracted from the total available memory in the system. The DIMM
installation order for memory sparing varies based on the number of processors
and memory modules installed in the server. After an error threshold is
surpassed in a system protected by memory sparing, the content of a failing
rank of DIMMs is copied to the spare rank. The failing rank is then taken
offline and the spare rank placed online for use as active memory in place
of the failed rank.

For example, CXL memory devices may support various subclasses for sparing
operation vary in terms of the scope of the sparing being performed.
Cacheline sparing subclass refers to a sparing action that can replace a
full cacheline. Row sparing is provided as an alternative to PPR sparing
functions and its scope is that of a single DDR row. Bank sparing allows
an entire bank to be replaced. Rank sparing is defined as an operation
in which an entire DDR rank is replaced. See CXL spec 3.1 section
8.2.9.7.1.4 Memory Sparing Maintenance Operations for more details.

Use cases of generic memory repair features control
---------------------------------------------------

1. The soft PPR , hard PPR and memory-sparing features share similar
control attributes. Therefore, there is a need for a standardized, generic
sysfs repair control that is exposed to userspace and used by
administrators, scripts and tools.

2. When a CXL device detects an error in a memory component, it may inform
the host of the need for a repair maintenance operation by using an event
record where the "maintenance needed" flag is set. The event record
specifies the device physical address(DPA) and attributes of the memory that
requires repair. The kernel reports the corresponding CXL general media or
DRAM trace event to userspace, and userspace tools (e.g. rasdaemon) initiate
a repair maintenance operation in response to the device request using the
sysfs repair control.

3. Userspace tools, such as rasdaemon, may request a PPR/sparing on a memory
region when an uncorrected memory error or an excess of corrected memory
errors is reported on that memory.

4. Multiple PPR/sparing instances may be present per memory device.

The File System
---------------

The control attributes of a registered memory repair instance could be
accessed in the

/sys/bus/edac/devices/<dev-name>/mem_repairX/

sysfs
-----

Sysfs files are documented in

`Documentation/ABI/testing/sysfs-edac-memory-repair`.

Example
-------

The usage takes the form shown in this example:

1. CXL memory device Soft Post Package Repair (Soft PPR)

# read capabilities

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/dpa_support

1

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/nibble_mask

0x0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/persist_mode

0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/repair_function

0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/repair_safe_when_in_use

1

# set and readback attributes

root@localhost:~# echo 0x8a2d > /sys/bus/edac/devices/cxl_mem0/mem_repair0/nibble_mask

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/min_dpa

0x0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/max_dpa

0xfffffff

root@localhost:~# echo 0x300000 >  /sys/bus/edac/devices/cxl_mem0/mem_repair0/dpa

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/dpa

0x300000

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair0/nibble_mask

0x8a2d

# issue repair operations

# reapir returns error if unsupported/resources are not available
# for the repair operation.

root@localhost:~# echo 1 > /sys/bus/edac/devices/cxl_mem0/mem_repair0/repair

1.2. CXL memory sparing

# read capabilities

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/repair_function

2

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/dpa_support

1

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/persist_mode

0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/repair_safe_when_in_use

1

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/min_dpa

0x0

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/max_dpa

0xfffffff

#set and readback attributes

root@localhost:~# echo 0x700000 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/dpa

root@localhost:~# echo 1 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/bank_group

root@localhost:~# echo 3 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/bank

root@localhost:~# echo 2 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/channel

root@localhost:~# echo  7 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/rank

root@localhost:~# echo 0x240a > /sys/bus/edac/devices/cxl_mem0/mem_repair1/row

root@localhost:~# echo 5 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/sub_channel

root@localhost:~# echo 11 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/column

root@localhost:~# echo 0x85c2 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/nibble_mask

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/bank_group

1

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/bank

3

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/channel

2

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/rank

7

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/row

0x240a

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/sub_channel

5

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/column

11

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/nibble_mask

0x85c2

root@localhost:~# cat /sys/bus/edac/devices/cxl_mem0/mem_repair1/dpa

0x700000

# issue repair operation
# repair returns error if unsupported or resources are not available for the
# repair operation.

root@localhost:~# echo 1 > /sys/bus/edac/devices/cxl_mem0/mem_repair1/repair
