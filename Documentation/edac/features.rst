.. SPDX-License-Identifier: GPL-2.0

============================================
Augmenting EDAC for controlling RAS features
============================================

Copyright (c) 2024 HiSilicon Limited.

:Author:   Shiju Jose <shiju.jose@huawei.com>
:License:  The GNU Free Documentation License, Version 1.2
          (dual licensed under the GPL v2)
:Original Reviewers:

- Written for: 6.13

Introduction
------------
The expansion of EDAC for controlling RAS features and exposing features
control attributes to userspace via sysfs. Some Examples:

* Scrub control

* Error Check Scrub (ECS) control

* ACPI RAS2 features

* Post Package Repair (PPR) control

* Memory Sparing Repair control etc.

High level design is illustrated in the following diagram::

         _______________________________________________
        |   Userspace - Rasdaemon                       |
        |  _____________                                |
        | | RAS CXL mem |      _______________          |
        | |error handler|---->|               |         |
        | |_____________|     | RAS dynamic   |         |
        |  _____________      | scrub, memory |         |
        | | RAS memory  |---->| repair control|         |
        | |error handler|     |_______________|         |
        | |_____________|          |                    |
        |__________________________|____________________|
                                   |
                                   |
    _______________________________|______________________________
   |     Kernel EDAC extension for | controlling RAS Features     |
   | ______________________________|____________________________  |
   || EDAC Core          Sysfs EDAC| Bus                        | |
   ||    __________________________|_________     _____________ | |
   ||   |/sys/bus/edac/devices/<dev>/scrubX/ |   | EDAC device || |
   ||   |/sys/bus/edac/devices/<dev>/ecsX/   |<->| EDAC MC     || |
   ||   |/sys/bus/edac/devices/<dev>/repairX |   | EDAC sysfs  || |
   ||   |____________________________________|   |_____________|| |
   ||                           EDAC|Bus                        | |
   ||                               |                           | |
   ||    __________ Get feature     |      Get feature          | |
   ||   |          |desc   _________|______ desc  __________    | |
   ||   |EDAC scrub|<-----| EDAC device    |     |          |   | |
   ||   |__________|      | driver- RAS    |---->| EDAC mem |   | |
   ||    __________       | feature control|     | repair   |   | |
   ||   |          |<-----|________________|     |__________|   | |
   ||   |EDAC ECS  |    Register RAS|features                   | |
   ||   |__________|                |                           | |
   ||         ______________________|_____________              | |
   ||_________|_______________|__________________|______________| |
   |   _______|____    _______|_______       ____|__________      |
   |  |            |  | CXL mem driver|     | Client driver |     |
   |  | ACPI RAS2  |  | scrub, ECS,   |     | memory repair |     |
   |  | driver     |  | sparing, PPR  |     | features      |     |
   |  |____________|  |_______________|     |_______________|     |
   |        |                 |                    |              |
   |________|_________________|____________________|______________|
            |                 |                    |
    ________|_________________|____________________|______________
   |     ___|_________________|____________________|_______       |
   |    |                                                  |      |
   |    |            Platform HW and Firmware              |      |
   |    |__________________________________________________|      |
   |______________________________________________________________|


1. EDAC Features components - Create feature specific descriptors.
For example, EDAC scrub, EDAC ECS, EDAC memory repair in the above
diagram.

2. EDAC device driver for controlling RAS Features - Get feature's attribute
descriptors from EDAC RAS feature component and registers device's RAS
features with EDAC bus and exposes the features control attributes via
the sysfs EDAC bus. For example, /sys/bus/edac/devices/<dev-name>/<feature>X/

3. RAS dynamic feature controller - Userspace sample modules in rasdaemon for
dynamic scrub/repair control to issue scrubbing/repair when excess number
of corrected memory errors are reported in a short span of time.
