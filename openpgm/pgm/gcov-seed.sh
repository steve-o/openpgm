#!/bin/bash

./ref/debug/async_unittest
./ref/debug/checksum_unittest
./ref/debug/getifaddrs_unittest
./ref/debug/getnodeaddr_unittest
./ref/debug/gsi_unittest
./ref/debug/http_unittest
./ref/debug/if_unittest
./ref/debug/indextoaddr_unittest
./ref/debug/inet_network_unittest
./ref/debug/md5_unittest
./ref/debug/net_unittest
./ref/debug/packet_unittest
./ref/debug/pgmMIB_unittest
./ref/debug/pgm_unittest
./ref/debug/rate_control_unittest
./ref/debug/receiver_unittest
./ref/debug/recv_unittest
./ref/debug/reed_solomon_unittest
./ref/debug/rxwi_unittest
./ref/debug/signal_unittest
./ref/debug/snmp_unittest
./ref/debug/source_unittest
./ref/debug/timer_unittest
./ref/debug/time_unittest
user=`id -nu`
group=`id -ng`
sudo execcap 'cap_net_raw=ep' /sbin/sucap $user $group ./ref/debug/transport_unittest
sudo find ref/debug/ -user 0 -exec chown $user:$group {} \;
./ref/debug/tsi_unittest
./ref/debug/txwi_unittest

