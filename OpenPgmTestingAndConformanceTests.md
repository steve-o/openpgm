Test numbers as per the referring section in the PGM draft with alphabetical suffix for multiple matching tests.

### 3.6.2.1.a. Sending ODATA packet ###
<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/odata.pl'>odata.pl</a></tt>

</dd><dt>Overview</dt><dd>Send one data packet from the application and confirm delivery on the network.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: send "ringo"<br>
mon: wait for ODATA: "ringo"<br>
</pre>
</dd></dl>

### 5.1.1.a. ODATA sequencing ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/odata_number.pl'>odata_number.pl</a></tt>

</dd><dt>Overview</dt><dd>Send 1,000 data packets and verify sending order is same as received order.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
for (''i'' = 1..1000) {<br>
app: send ''i''<br>
mon: wait for ODATA: ''i''<br>
}<br>
</pre>
</dd></dl>

### 5.1.1.b. Maximum Cumulative Transmit Rate ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/odata_rate.pl'>odata_rate.pl</a></tt>

</dd><dt>Overview</dt><dd>Set a low TXW_MAX_RTE (1KBps), send 50 data packets and verify received data rate.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: set TXW_MAX_RTE<br>
app: create transport<br>
for (1..50) {<br>
app: send "ringo"<br>
mon: wait for ODATA<br>
''bytes'' += ODATA packet size<br>
}<br>
''rate'' = ''bytes'' / elapsed time<br>
</pre>
</dd></dl>

### 5.1.4.a. Ambient SPMs ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/spm.pl'>spm.pl</a></tt>

</dd><dt>Overview</dt><dd>Create a transport and wait for SPM announcement on the network.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
mon: wait for SPM<br>
</pre>
</dd></dl>

### 5.1.4.b. Ambient SPMs interleaved with ODATA ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/ambient_spm.pl'>ambient_spm.pl</a></tt>

</dd><dt>Overview</dt><dd>Send a stream of data packets with interval less than the minimum SPM heartbeat interval, look for an ambient SPM broadcast.<br>
<br>
</dd><dt>Remarks</dt><dd>Highly dependent on PGM parameters, data packets need to be published with greater frequency than the minimum SPM heartbeat interval.  Current configuration is publishing every 50ms which should support heartbeat intervals at 100ms and above.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
fork --------------------------------> /* child process */<br>
mon: wait for odata                    for (;;) {<br>
mon: wait for spm                        app: send "ringo"<br>
}<br>
</pre>
</dd></dl>

### 5.1.5.a. Heartbeat SPMs ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/heartbeat_spm.pl'>heartbeat_spm.pl</a></tt>

</dd><dt>Overview</dt><dd>Look for SPMs shortly following an ODATA packet.<br>
<br>
</dd><dt>Remarks</dt><dd>Highly dependent on PGM parameters, currently looking for four consecutive heartbeat SPMs less than 1000ms apart.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: send "ringo"<br>
mon: wait for ODATA<br>
for (1..4) {<br>
mon: wait for SPM<br>
}<br>
</pre>
</dd></dl>

### 5.2.a. Negative Acknowledgment Confirmation ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/ncf.pl'>ncf.pl</a></tt>

</dd><dt>Overview</dt><dd>Send a sequence of data packets from the application, then request re-transmission of one of the packets and wait for a matching NCF packet.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
sim: create transport<br>
app: create transport<br>
app: send "ringo"              /* sequence number: #1 */<br>
app: send "ichigo"                              /* #2 */<br>
app: send "momo"                                /* #3 */<br>
mon: wait for ODATA: #1, "ringo"<br>
mon: wait for ODATA: #2, "ichigo"<br>
mon: wait for ODATA: #3, "momo"<br>
sim: send NAK for #2                 /* ichigo */<br>
mon: wait for NCF: #2<br>
</pre>
</dd></dl>

### 5.3.a. Repairs ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/nak.pl'>nak.pl</a></tt>

</dd><dt>Overview</dt><dd>Send a sequence of data packets from the application, then request re-transmission of one of the packets and wait for a matching RDATA packet.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
sim: create transport<br>
app: create transport<br>
app: send "ringo"              /* sequence number: #1 */<br>
app: send "ichigo"                              /* #2 */<br>
app: send "momo"                                /* #3 */<br>
mon: wait for ODATA: #1, "ringo"<br>
mon: wait for ODATA: #2, "ichigo"<br>
mon: wait for ODATA: #3, "momo"<br>
sim: send NAK for #2                 /* ichigo */<br>
mon: wait for RDATA: #2, "ichigo"<br>
</pre>
</dd></dl>

### 6.1.a. Data Reception by ODATA ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/odata_reception.pl'>odata_reception.pl</a></tt>

</dd><dt>Overview</dt><dd>Initiate a PGM session with an ODATA and verify with following ODATA.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: listen on transport<br>
sim: create fake transport<br>
sim: publish ODATA sqn 90,000<br>
app: wait for data<br>
sim: verify no NAKs are generated from app<br>
sim: publish ODATA sqn 90,001<br>
app: wait for data<br>
</pre>
</dd></dl>

### 6.1.b. Data Reception by RDATA ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/rdata_reception.pl'>rdata_reception.pl</a></tt>

</dd><dt>Overview</dt><dd>Initiate a PGM session with a RDATA packet and verify with following ODATA.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: listen on transport<br>
sim: create fake transport<br>
sim: publish RDATA sqn 90,000<br>
app: wait for data<br>
sim: verify no NAKs are generated from app<br>
sim: publish ODATA sqn 90,001<br>
app: wait for data<br>
</pre>
</dd></dl>

### 6.1.c. Data Reception by SPM ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/spm_reception.pl'>spm_reception.pl</a></tt>

</dd><dt>Overview</dt><dd>Initiate a PGM session with a SPM and verify with matching ODATA.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: listen on transport<br>
sim: create fake transport<br>
sim: publish SPM txw_trail 90,000<br>
sim: verify no NAKs are generated from app<br>
sim: publish ODATA sqn 90,001<br>
app: wait for data<br>
</pre>
</dd></dl>

### 6.1.d. Fragmented APDU ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/apdu.pl'>apdu.pl</a></tt>

</dd><dt>Overview</dt><dd>Send a fragmented APDU and confirm reconstruction at the receiver side.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: listen on transport<br>
sim: create transport<br>
sim: publish 5KiB ODATA message<br>
app: wait for data<br>
</pre>
</dd></dl>

### 6.2.a. Out-of-sequence SPMs must be discarded ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/spm_jump.pl'>spm_jump.pl</a></tt>

</dd><dt>Overview</dt><dd>Create a PGM session with an initial SPM, follow with an out-of-sequence SPM and verify no NAKs are generated.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: listen on transport<br>
sim: create fake transport<br>
sim: publish SPM txw_lead 90,000 SPM sqn 3200<br>
sim: publish SPM txw_lead 90,005 SPM sqn 20<br>
sim: verify no NAKs are generated from app<br>
</pre>
</dd></dl>

### 6.3.a. NAK generation induced by ODATA ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/odata_jump.pl'>odata_jump.pl</a></tt>

</dd><dt>Overview</dt><dd>Create a PGM session with an initial ODATA packet, follow with an ODATA with a jumped sequence number.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: listen on transport<br>
sim: create fake transport<br>
sim: publish ODATA sqn 90,001<br>
sim: publish ODATA sqn 90,003<br>
sim: wait for NAK<br>
</pre>
</dd></dl>

### 6.3.b. NAK generation induced by RDATA ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/rdata_jump.pl'>rdata_jump.pl</a></tt>

</dd><dt>Overview</dt><dd>Create a PGM session with an initial ODATA packet, follow with an RDATA with a jumped sequence number.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: listen on transport<br>
sim: create fake transport<br>
sim: publish ODATA sqn 90,001<br>
sim: publish RDATA sqn 90,003<br>
sim: wait for NAK<br>
</pre>
</dd></dl>

### 6.3.c. NAK generation induced by SPM ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/spm_jump2.pl'>spm_jump2.pl</a></tt>

</dd><dt>Overview</dt><dd>Create a PGM session with an initial SPM, follow with an in-sequence SPM that indicates the transmit window has advanced.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: listen on transport<br>
sim: create fake transport<br>
sim: publish SPM txw_lead 90,000 SPM sqn 3200<br>
sim: publish SPM txw_lead 90,001 SPM sqn 3201<br>
sim: wait for NAK<br>
</pre>
</dd></dl>

### 6.3.d. NAK suppression by NCF ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/ncf_suppression.pl'>ncf_suppression.pl</a></tt>

</dd><dt>Overview</dt><dd>Send out-of-sequence ODATA packets to induce a NAK, but follow with a NCF to suppress sending.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
sim: create fake transport<br>
sim: sent SPM                        /* set NLA in app */<br>
sim: send "ringo"<br>
app: wait for data<br>
sim: send "ichigo" with sequence number jump<br>
sim: send NCF<br>
sim: fail if NAK received<br>
</pre>
</dd></dl>

### 6.3.f. Completion by ODATA ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/odata_completion.pl'>odata_completion.pl</a></tt>

</dd><dt>Overview</dt><dd>Send out-of-sequence ODATA packets to induce a NAK, but follow with a ODATA to complete sequence.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
app: send "nashi"<br>
mon: wait for ODATA and save TSI<br>
sim: create fake transport<br>
sim: sent SPM                        /* set NLA in app */<br>
sim: send "ringo"<br>
app: wait for data<br>
sim: send "ichigo" with sequence number jump<br>
sim: send ODATA "momo"<br>
app: wait for data: "momo"<br>
app: wait for data: "ichigo"<br>
</pre>
</dd></dl>

### 6.3.g. Completion by RDATA ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/rdata_completion.pl'>rdata_completion.pl</a></tt>

</dd><dt>Overview</dt><dd>Send out-of-sequence ODATA packets to induce a NAK, but follow with a RDATA to complete sequence.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
sim: create fake transport<br>
sim: sent SPM                        /* set NLA in app */<br>
sim: send "ringo"<br>
app: wait for data<br>
sim: send "ichigo" with sequence number jump<br>
sim: send RDATA "momo"<br>
app: wait for data: "momo"<br>
app: wait for data: "ichigo"<br>
</pre>
</dd></dl>

### 6.3.h. NAK cancellation by NCF ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/ncf_cancellation.pl'>ncf_cancellation.pl</a></tt>

</dd><dt>Overview</dt><dd>Send out-of-sequence ODATA packets to induce a NAK, wait until NAK is cancelled by NCF retry count.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
sim: create fake transport<br>
sim: sent SPM                        /* set NLA in app */<br>
sim: send "ringo"<br>
app: wait for data<br>
sim: send "ichigo" with sequence number jump<br>
fork --------------------------------> /* child process */<br>
app: wait for data                     for (;;) {<br>
sim: wait for NAK<br>
}<br>
</pre>
</dd></dl>

### 6.3.i. NAK cancellation by DATA ###

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/nak_cancellation.pl'>nak_cancellation.pl</a></tt>

</dd><dt>Overview</dt><dd>Send out-of-sequence ODATA packets to induce a NAK, wait until NAK is cancelled by DATA retry count.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
app: create transport<br>
sim: create fake transport<br>
sim: sent SPM                        /* set NLA in app */<br>
sim: send "ringo"<br>
app: wait for data<br>
sim: send "ichigo" with sequence number jump<br>
fork --------------------------------> /* child process */<br>
app: wait for data                     for (;;) {<br>
sim: wait for NAK<br>
sim: send NCF<br>
}<br>
</pre>
</dd></dl>

## 9.3. NAK List Option - OPT\_NAK\_LIST ##

<dl><dt>Description</dt><dd>The NAK List Option allows transmission of more than sequence number with a single NAK packet.<br>
<br>
<br>
<h3>9.3.a. Sending RDATA in response to valid NAK List</h3>

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/nak_list.pl'>nak_list.pl</a></tt>

</dd><dt>Overview</dt><dd>Send a sequence of data packets from the application, then request re-transmission of a list of packets and wait for a matching sequence of RDATA packets.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
sim: create fake transport<br>
app: create transport<br>
app: send "ringo"              /* sequence number: #1 */<br>
app: send "ichigo"                              /* #2 */<br>
app: send "momo"                                /* #3 */<br>
mon: wait for ODATA: #1, "ringo"<br>
mon: wait for ODATA: #2, "ichigo"<br>
mon: wait for ODATA: #3, "momo"<br>
sim: send NAK list for #1, #2, #3               /* all packets */<br>
mon: wait for RDATA: #1, "ringo"<br>
mon: wait for RDATA: #2, "ichigo"<br>
mon: wait for RDATA: #3, "momo"<br>
</pre>
</dd></dl>

<h3>9.3.b. Sending NCF in response to valid NAK List</h3>

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/ncf_list.pl'>ncf_list.pl</a></tt>

</dd><dt>Overview</dt><dd>Send a sequence of data packets from the application, then request re-transmission of a list of packets and wait for a matching NCF list packet.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
sim: create fake transport<br>
app: create transport<br>
app: send "ringo"              /* sequence number: #1 */<br>
app: send "ichigo"                              /* #2 */<br>
app: send "momo"                                /* #3 */<br>
mon: wait for ODATA: #1, "ringo"<br>
mon: wait for ODATA: #2, "ichigo"<br>
mon: wait for ODATA: #3, "momo"<br>
sim: send NAK list for #1, #2, #3               /* all packets */<br>
mon: wait for NCF list: #1, #2, #3<br>
</pre>
</dd></dl>

<h2>13. Appendix C - SPM Requests</h2>

<dl><dt>Description</dt><dd>SPM Requests (SPMRs) MAY be used to solicit an SPM from a source in a non-implosive way.<br>
<br>
<br>
<h3>13.3.1.a SPM Request on DATA opened session</h3>

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/spmr_from_odata.pl'>spmr_from_odata.pl</a></tt>

</dd><dt>Overview</dt><dd>Send a ODATA packet to lead a new session, wait for SPM Request from test application.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
sim: create fake transport<br>
app: create transport<br>
sim: send "ringo"<br>
mon: wait for SPMR<br>
</pre>
</dd></dl>

<h3>13.3.1.b SPM broadcast on SPM Request</h3>

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/spmr.pl'>spmr.pl</a></tt>

</dd><dt>Overview</dt><dd>Send a SPM Request and wait for a matching SPM broadcast.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
sim: create fake transport<br>
app: create transport<br>
sim: send SPMR<br>
mon: wait for SPM<br>
</pre>
</dd></dl>

<h3>13.3.1.c SPMR suppression by SPMR</h3>

<dl><dt>Test</dt><dd><tt><a href='http://code.google.com/p/openpgm/source/browse/#svn/trunk/openpgm/pgm/test/spmr_suppression.pl'>spmr_suppression.pl</a></tt>

</dd><dt>Overview</dt><dd>Induce a SPM Request by ODATA lead new session, suppress SPMR generation by sending a multicast peer SPMR.<br>
<br>
</dd><dt>Pseudo code</dt><dd>
<pre>
sim: create fake transport<br>
app: create transport<br>
sim: send "ringo"<br>
sim: send SPMR<br>
mon: fail on SPMR<br>
</pre>
</dd></dl>