#!/bin/bash

gcov -fno ref/debug async_unittest.c
gcov -fno ref/debug checksum_unittest.c
gcov -fno ref/debug getifaddrs_unittest.c
gcov -fno ref/debug getnodeaddr_unittest.c
gcov -fno ref/debug gsi_unittest.c
gcov -fno ref/debug http_unittest.c
gcov -fno ref/debug if_unittest.c
gcov -fno ref/debug indextoaddr_unittest.c
gcov -fno ref/debug inet_network_unittest.c
gcov -fno ref/debug md5_unittest.c
gcov -fno ref/debug net_unittest.c
gcov -fno ref/debug packet_unittest.c
gcov -fno ref/debug pgmMIB_unittest.c
gcov -fno ref/debug pgm_unittest.c
gcov -fno ref/debug rate_control_unittest.c
gcov -fno ref/debug receiver_unittest.c
gcov -fno ref/debug recv_unittest.c
gcov -fno ref/debug reed_solomon_unittest.c
gcov -fno ref/debug rxwi_unittest.c
gcov -fno ref/debug signal_unittest.c
gcov -fno ref/debug snmp_unittest.c
gcov -fno ref/debug source_unittest.c
gcov -fno ref/debug timer_unittest.c
gcov -fno ref/debug time_unittest.c
gcov -fno ref/debug tsi_unittest.c
gcov -fno ref/debug txwi_unittest.c

