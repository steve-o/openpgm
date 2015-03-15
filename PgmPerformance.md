# PGM Protocol Efficiency #

Excluding bit usage of SPM broadcasts, PGM efficiency is 95.0% for single packet APDUs.

<pre>
MAC Pre-amble:                         7 bytes<br>
Start-of-Frame-Delimiter:              1 byte<br>
802.3 MAC Frame:                      18 bytes<br>
IPv4 Header:                          20 bytes<br>
PGM Header:                           11 bytes<br>
ODATA Header:                          8 bytes<br>
Payload:                           1,461 bytes<br>
Interframe Gap:                       12 bytes<br>
* ----------------------------------------------*<br>
* Total:                             1,538 bytes*<br>
<br>
_ Efficiency: (1,461 ÷ 1,538 × 100%)     = 95.0%_<br>
* ----------------------------------------------*<br>
</pre>

NB: 802.3 MAC frame = 6 bytes MAC destination + 6 bytes MAC source + 2 bytes ethertype + 4 bytes checksum.



For a 64KB APDU on standard Ethernet framing, no FEC, efficiency drops to 93.6%.

<pre>
Number of packets  = (64 × 1,024) ÷ 1,433 = 46<br>
Payload per packet = (64 × 1,024) ÷ 46 = 1,425 bytes + 14 bytes remainder.<br>
<br>
MAC Pre-amble:                         7 bytes<br>
Start-of-Frame-Delimiter:              1 byte<br>
802.3 MAC Frame:                      18 bytes<br>
IPv4 Header:                          20 bytes<br>
PGM Header:                           11 bytes<br>
OPT_LENGTH Header:                     4 bytes<br>
OPT_FRAGMENT Header:                  16 bytes<br>
ODATA Header:                          8 bytes<br>
Payload:                           1,425 bytes<br>
Interframe Gap:                       12 bytes<br>
* ----------------------------------------------*<br>
* Sub-Total:                         1,522 bytes*<br>
*                                     × 46 packets*<br>
*                                   70,012 bytes*<br>
* Subtract remainder:                  -14 bytes*<br>
* Total:                            69,998 bytes*<br>
<br>
_ Efficiency: (65,536 ÷ 69,001 × 100%)   = 93.6%_<br>
* ----------------------------------------------*<br>
</pre>

NB: Maximum fragment payload is calculated as follows.

<pre>
Maximum IP payload:                1,500 bytes<br>
Subtract IPv4 Header:                -20 bytes<br>
Subtract PGM Header:                 -19 bytes<br>
Subtract OPT_LENGTH Header:           -4 bytes<br>
Subtract OPT_FRAGMENT Header:        -16 bytes<br>
Subtract ODATA Header:                -8 bytes<br>
* ----------------------------------------------*<br>
* Total:                             1,433 bytes*<br>
* ----------------------------------------------*<br>
</pre>

For UDP encapsulation of single packet APDUs efficiency is at 94.5% for IPv4 and 93.2% for IPv6.

<pre>
MAC Pre-amble:                         7 bytes<br>
Start-of-Frame-Delimiter:              1 byte<br>
802.3 MAC Frame:                      18 bytes<br>
IPv4 Header:                          20 bytes<br>
UDP Header:                            8 bytes<br>
PGM Header:                           11 bytes<br>
ODATA Header:                          8 bytes<br>
Payload:                           1,453 bytes<br>
Interframe Gap:                       12 bytes<br>
* ----------------------------------------------*<br>
* Total:                             1,538 bytes*<br>
<br>
_ Efficiency: (1,453 ÷ 1,538 × 100%)     = 94.5%_<br>
* ----------------------------------------------*<br>
</pre>

<pre>
MAC Pre-amble:                         7 bytes<br>
Start-of-Frame-Delimiter:              1 byte<br>
802.3 MAC Frame:                      18 bytes<br>
IPv6 Header:                          40 bytes<br>
UDP Header:                            8 bytes<br>
PGM Header:                           11 bytes<br>
ODATA Header:                          8 bytes<br>
Payload:                           1,433 bytes<br>
Interframe Gap:                       12 bytes<br>
* ----------------------------------------------*<br>
* Total:                             1,538 bytes*<br>
<br>
_ Efficiency: (1,433 ÷ 1,538 × 100%)     = 93.2%_<br>
* ----------------------------------------------*<br>
</pre>

FEC only affects multiple packet APDUs when using variable packet length per TPDU, for a 64KB APDU as above there is no difference.

<pre>
Number of packets  = (64 × 1,024) ÷ 1,431 = 46<br>
Payload per packet = (64 × 1,024) ÷ 46 = 1,425 bytes + 14 bytes remainder.<br>
<br>
MAC Pre-amble:                         7 bytes<br>
Start-of-Frame-Delimiter:              1 byte<br>
802.3 MAC Frame:                      18 bytes<br>
IPv4 Header:                          20 bytes<br>
PGM Header:                           11 bytes<br>
OPT_LENGTH Header:                     4 bytes<br>
OPT_FRAGMENT Header:                  16 bytes<br>
ODATA Header:                          8 bytes<br>
Payload:                           1,425 bytes<br>
Interframe Gap:                       12 bytes<br>
* ----------------------------------------------*<br>
* Sub-Total:                         1,522 bytes*<br>
*                                     × 46 packets*<br>
*                                   70,012 bytes*<br>
* Subtract remainder:                  -14 bytes*<br>
* Total:                            69,998 bytes*<br>
<br>
_ Efficiency: (65,536 ÷ 69,998 * 100%)   = 93.6%_<br>
* ----------------------------------------------*<br>
</pre>

NB: Maximum packet payload is calculated as 1,433 - 2 = 1,431 bytes.


# Ethernet Line Performance #
Minimum Ethernet frame is 64 bytes, therefore calculating maximum packet rate for gigabit Ethernet with an interframe gap of 96 ns.

<pre>
MAC Pre-amble:                         7 bytes<br>
Start-of-Frame-Delimiter:              1 byte<br>
802.3 MAC Frame:                      18 bytes<br>
Payload:                              46 bytes<br>
Interframe Gap:                       12 bytes<br>
* ----------------------------------------------*<br>
* Total:                                84 bytes*<br>
<br>
_ Maximum packets-per-second: 1,000,000,000 ÷ (84 × 8) = 1,488,100 pps_<br>
* ----------------------------------------------*<br>
</pre>

At reduced IFG of 64 bit times (8 bytes) the rate increases to 1,562,500 pps.

Maximum Ethernet frame is 1,518 bytes, therefore maximum full frame rate is calculated as follows.

<pre>
MAC Pre-amble:                         7 bytes<br>
Start-of-Frame-Delimiter:              1 byte<br>
802.3 MAC Frame:                      18 bytes<br>
Payload:                           1,500 bytes<br>
Interframe Gap:                       12 bytes<br>
* ----------------------------------------------*<br>
* Total:                             1,538 bytes*<br>
<br>
_ Maximum packets-per-second: 1,000,000,000 ÷ (1,538 × 8) = 81,274 pps_<br>
* ----------------------------------------------*<br>
</pre>

At reduced IFG of 64 bit times (8 bytes) the rate increases to 81,486 pps.


# Maximum Goodput #
Goodput is the application level throughput, i.e. the number of useful bits per unit of time forwarded by the network from a certain source address to a certain destination, excluding protocol overhead, and excluding retransmitted data packets.

<pre>
IP/PGM:              82,274 × 1,461 bytes = 962mbs⁻¹.<br>
UDP over IPv4/PGM:   82,274 × 1,453 bytes = 956mbs⁻¹.<br>
UDP over IPv6/PGM:   82,274 × 1,433 bytes = 943mbs⁻¹.<br>
</pre>