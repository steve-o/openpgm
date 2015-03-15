OpenPGM makes it easier to develop highly scalable distributed applications that exchange data across and with assistance from the network.  OpenPGM is a network data transport it does not impose a message format or high level communications logic.  OpenPGM can run on many different hardware and software platforms enabling a heterogeneous network platform.

For the programmer OpenPGM provides an API which embeds the communication system within the application.  OpenPGM does not interfere or replace existing inter-process communication, there is no external daemon to arbitrate on behalf of multiple applications on a host communicating on the same transport.

OpenPGM currently defines one language interface based upon the [GLib](http://library.gnome.org/devel/glib/) low-level core library.


**OpenPGM operating environment**

<img src='http://miru.hk/wiki/OpenPGM_operating_environment.png' />

Any computer can run any number of OpenPGM programs, however restrictions in the PGM protocol mean that a transport cannot be shared between processes.
