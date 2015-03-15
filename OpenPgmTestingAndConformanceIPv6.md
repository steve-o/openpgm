## Known Issues ##
  * [Zone Indices](http://en.wikipedia.org/wiki/IPv6#Link-local_addresses_and_zone_indices) are not completely supported in interface parsing.

If you bridge an IPv6 interface on two different networks, i.e. using VLAN tagging, both interfaces will keep the same IPv6 link-local address, e.g.
<pre>
eth0      Link encap:Ethernet  HWaddr 00:30:1b:b7:a2:09<br>
inet6 addr: fe80::230:1bff:feb7:a209/64 Scope:Link<br>
<br>
eth0.615  Link encap:Ethernet  HWaddr 00:30:1b:b7:a2:09<br>
inet6 addr: fe80::230:1bff:feb7:a209/64 Scope:Link<br>
</pre>
Zone indices would allow the user to specify the adapter by index on Windows platforms, e.g.
<pre>
$ pgmsend -n "fe80::230:1bff:feb7:a209%2" moo<br>
$ pgmsend -n "fe80::230:1bff:feb7:a209%3" baa<br>
</pre>
Or the interface name on Unix platforms, e.g.
<pre>
$ pgmsend -n "fe80::230:1bff:feb7:a209%eth0" moo<br>
$ pgmsend -n "fe80::230:1bff:feb7:a209%eth0.615" baa<br>
</pre>
The interface index can be seen running <tt>dumpif</tt>
<pre>
Pgm-Message: #1 name lo (lo)         IPv6 ::1<br>
Pgm-Message: #2 name eth0 (eth0)     IPv6 fe80::230:1bff:feb7:a209<br>
Pgm-Message: #3 name eth0.615 (eth0. IPv6 fe80::230:1bff:feb7:a209<br>
</pre>
OpenPGM provides the ability to specify the interface as part of the network parameter but not the scope, similarly a multicast address must be specified if a dual protocol stack is enabled.
<pre>
$ pgmsend -n "eth0" moo<br>
$ pgmsend -n "eth0.615" baa<br>
</pre>
or
<pre>
$ pgmsend -n "eth0;ff08::1" moo<br>
$ pgmsend -n "eth0.615;ff08::1" baa<br>
</pre>