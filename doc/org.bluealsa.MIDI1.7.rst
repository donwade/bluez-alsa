==================
org.bluealsa.MIDI1
==================

------------------------------
Bluetooth Audio MIDI D-Bus API
------------------------------

:Date: October 2023
:Manual section: 7
:Manual group: D-Bus Interface
:Version: $VERSION$

SYNOPSIS
========

:Service:     org.bluealsa[.unique ID]
:Interface:   org.bluealsa.MIDI1
:Object path: [variable prefix]/{hci0,...}/[mode]

DESCRIPTION
===========

This page describes the D-Bus MIDI interface of the **bluealsa(8)** service.
The MIDI interface gives access to raw MIDI objects created by this service.

Methods
-------

fd Open()
    Open raw MIDI interface exposed by BlueALSA on given HCI adapter. This
    method returns file descriptor for MIDI stream PIPE.

Properties
----------

string Mode [readonly]
    MIDI stream operation mode (direction).

    Possible values:
    ::

        "input"
        "output"

COPYRIGHT
=========

Copyright (c) 2016-2023 Arkadiusz Bokowy.

The bluez-alsa project is licensed under the terms of the MIT license.

SEE ALSO
========

``bluealsa-cli(1)``, ``bluealsa-plugins(5)``, ``bluealsa(8)``

Project web site
  https://github.com/arkq/bluez-alsa
