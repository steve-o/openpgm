## Application ##
<dl><dt>Purpose</dt><dd>Runs the PGM network stack to test. This is implemented as a thin agnostic layer above the API allowing control and feedback to the tester.<br>
</dd><dt>Syntax</dt><dd>
<pre>
app [-n _network_]<br>
[-s _service_]<br>
</pre>
</dd><dt>Remarks</dt><dd>Control is provided by standard input with feedback to standard output, the available commands follow in alphabetical order.  After an input command is processed and is ready to accept another command the output "<tt>READY</tt>" is displayed.<br>
</dd></dl>

### bind ###
<dl><dt>Syntax</dt><dd>
<pre>
bind _session_name_<br>
</pre>
</dd><dt>Purpose</dt><dd>Bind a transport to the network.<br>
</dd><dt>Remarks</dt><dd>The transport must be previously created with the <tt>create</tt> command.<br>
<br>
Example output for binding a session called <i>transport1</i>:<br>
<pre>
*create _transport1_*<br>
created new session "_transport1_"<br>
READY<br>
*bind _transport1_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr>
</table>
</dd></dl>

### create ###
<dl><dt>Syntax</dt><dd>
<pre>
create _session_name_<br>
</pre>
</dd><dt>Purpose</dt><dd>Create a new transport.<br>
</dd><dt>Remarks</dt><dd>Example output for creating a new transport session called <i>transport1</i>:<br>
<pre>
*create _transport1_*<br>
created new session "_transport1_"<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr>
</table>
</dd></dl>

### destroy ###
<dl><dt>Syntax</dt><dd>
<pre>
destroy _session_name_<br>
</pre>
</dd><dt>Purpose</dt><dd>Destroy a transport.<br>
</dd><dt>Remarks</dt><dd>Example output for destroying a session called <i>transport1</i>:<br>
<pre>
*create _transport1_*<br>
created new session "_transport1_"<br>
READY<br>
*destroy _transport1_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr>
</table>
</dd></dl>

### listen ###
<dl><dt>Syntax</dt><dd>
<pre>
listen _session_name_<br>
</pre>
</dd><dt>Purpose</dt><dd>Listen to data packets on a transport.<br>
</dd><dt>Remarks</dt><dd>Example output for listening to a session called <i>transport1</i>:<br>
<pre>
*create _transport1_*<br>
created new session "_transport1_"<br>
READY<br>
*bind _transport1_*<br>
READY<br>
*listen _transport1_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr>
</table>
</dd></dl>

### quit ###
<dl><dt>Syntax</dt><dd>
<pre>
quit<br>
</pre>
</dd><dt>Purpose</dt><dd>Terminates the application and destroy all active transports.  Call to finish a test early, usually upon failure, or at test completion upon success.<br>
</dd><dt>Remarks</dt><dd>Example output with no active transport sessions:<br>
<pre>
*quit*<br>
** Message: event loop terminated, cleaning up.<br>
** Message: destroying sessions.<br>
unbinding stdin.<br>
** Message: finished.<br>
</pre>
</dd></dl>

### send ###
<dl><dt>Syntax</dt><dd>
<pre>
send _session_name_ _string_<br>
send _session_name_ _string_ x _repeat_count_<br>
</pre>
</dd><dt>Purpose</dt><dd>Send a simple message on the specified bound transport.<br>
</dd><dt>Remarks</dt><dd>The transport must be bound with <tt>bind</tt> command before messages can be sent.  The format restrictions on the <i>string</i> are to simplify output parsing.<br>
<br>
Example output for sending the string "<i>hello</i>" on the transport session <i>transport1</i>:<br>
<pre>
*send _transport1 hello_*<br>
READY<br>
</pre>
To send a large message, or APDU fragmented across multiple packets use the second format:<br>
<pre>
*send _transport1 goodbye_ x _1000_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>string</tt></td>
<td>An alphanumeric string to send, must not contain any spaces.</td>
</tr><tr>
<td><tt>repeat_count</tt></td>
<td>Repetitions of <tt>string</tt> to include in the message payload.</td>
</tr>
</table>
</dd></dl>

### set NAK\_BO\_IVL ###
<dl><dt>Syntax</dt><dd>
<pre>
set _session_name_ NAK_BO_IVL _interval_<br>
</pre>
</dd><dt>Purpose</dt><dd>Sets <tt>NAK_BO_IVL</tt>, the maximum value of <tt>NAK_RB_IVL</tt>, the back-off interval to wait whilst watching for matching NCFs or NAKs, before publishing a NAK.<br>
</dd><dt>Remarks</dt><dd>Example output:<br>
<pre>
*create _transport1_*<br>
created new session "_transport1_"<br>
READY<br>
*set _transport1_ NAK_BO_IVL _50_*<br>
READY<br>
*bind _transport1_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>interval</tt></td>
<td>Interval in milliseconds.</td>
</tr>
</table>
</dd></dl>

### set NAK\_RPT\_IVL ###
<dl><dt>Syntax</dt><dd>
<pre>
set _session_name_ NAK_RPT_IVL _interval_<br>
</pre>
</dd><dt>Purpose</dt><dd>Set <tt>NAK_RPT_IVL</tt>, the interval after sending or receiving a NAK and waiting for a matching NCF.<br>
</dd><dt>Remarks</dt><dd>Example output:<br>
<pre>
*create _transport1_*<br>
created new session "_transport1_"<br>
READY<br>
*set _transport1_ NAK_RPT_IVL _2000_*<br>
READY<br>
*bind _transport1_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>interval</tt></td>
<td>Interval in milliseconds.</td>
</tr>
</table>
</dd></dl>

### set NAK\_RDATA\_IVL ###
<dl><dt>Syntax</dt><dd>
<pre>
set _session_name_ NAK_RDATA_IVL _rate_<br>
</pre>
</dd><dt>Purpose</dt><dd>Set <tt>NAK_RDATA_IVL</tt>, the interval between receiving a NCF and waiting for a matching RDATA.<br>
</dd><dt>Remarks</dt><dd>Example output:<br>
<pre>
*create _transport1_*<br>
created new session "_transport1_"<br>
READY<br>
*set _transport1_ NAK_RDATA_IVL _2000_*<br>
READY<br>
*bind _transport1_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>interval</tt></td>
<td>Interval in milliseconds.</td>
</tr>
</table>
</dd></dl>

### set NAK\_NCF\_RETRIES ###
<dl><dt>Syntax</dt><dd>
<pre>
set _session_name_ NAK_NCF_RETRIES _retries_<br>
</pre>
</dd><dt>Purpose</dt><dd>Set <tt>NAK_NCF_RETRIES</tt>, the maximum number of times to wait for a NCF before cancelling NAK generation.<br>
</dd><dt>Remarks</dt><dd>Example output:<br>
<pre>
*create _transport1_*<br>
created new session "_transport1_"<br>
READY<br>
*set _transport1_ NAK_NCF_RETRIES _50_*<br>
READY<br>
*bind _transport1_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>retries</tt></td>
<td>Number of retries.</td>
</tr>
</table>
</dd></dl>

### set NAK\_DATA\_RETRIES ###
<dl><dt>Syntax</dt><dd>
<pre>
set _session_name_ NAK_DATA_RETRIES _retries_<br>
</pre>
</dd><dt>Purpose</dt><dd>Set <tt>NAK_DATA_RETRIES</tt>, the maximum number of times to wait for repair data before cancelling NAK generation.<br>
</dd><dt>Remarks</dt><dd>Example output:<br>
<pre>
*create _transport1_*<br>
created new session "_transport1_"<br>
READY<br>
*set _transport1_ NAK_DATA_RETRIES _1000_*<br>
READY<br>
*bind _transport1_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>retries</tt></td>
<td>Number of retries.</td>
</tr>
</table>
</dd></dl>

### set TXW\_MAX\_RTE ###
<dl><dt>Syntax</dt><dd>
<pre>
set _session_name_ TXW_MAX_RTE _rate_<br>
</pre>
</dd><dt>Purpose</dt><dd>Set sender maximum cumulative transmission rate.<br>
</dd><dt>Remarks</dt><dd>Example output for setting a rate of 1KBps:<br>
<pre>
*create _transport1_*<br>
created new session "transport1"<br>
READY<br>
*set _transport1_ TXW_MAX_RTE _1000_*<br>
READY<br>
*bind _transport1_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>rate</tt></td>
<td>Transmission rate in bytes per second.</td>
</tr>
</table>
</dd></dl>

### set FEC ###
<dl><dt>Syntax</dt><dd>
<pre>
set _session_name_ FEC RS(_n_, _k_)<br>
</pre>
</dd><dt>Purpose</dt><dd>Enable Reed-Solomon FEC engine and set RS code.<br>
</dd><dt>Remarks</dt><dd>Example output for using a RS(255,64) code:<br>
<pre>
*create _transport1_*<br>
created new session "transport1"<br>
READY<br>
*set _transport1_ FEC RS(_255_, _64_)<br>
READY<br>
*bind _transport1_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>n</tt></td>
<td>FEC Block length.</td>
</tr><tr>
<td><tt>k</tt></td>
<td>Transmission Group length.</td>
</tr>
</table>
</dd></dl>

## Simulator ##
<dl><dt>Purpose</dt><dd>A host which can send and receive PGM packets in order to illicit a response from the application under test.<br>
</dd><dt>Syntax</dt><dd>
<pre>
sim [-n _network_]<br>
[-s _service_]<br>
</pre>
</dd><dt>Remarks</dt><dd>Control is provided by standard input with feedback to standard output, the available commands follow in alphabetical order.  After an input command is processed and is ready to accept another command the output "<tt>READY</tt>" is displayed.<br>
</dd></dl>

### bind ###
See <tt><a href='#bind.md'>Application/bind</a></tt>.


### set FEC ###
See <tt><a href='#setFec.md'>Application/set FEC</a></tt>.


### create ###
<dl><dt>Syntax</dt><dd>
<pre>
create [ _fake_ ] _session_name_<br>
</pre>
</dd><dt>Purpose</dt><dd>Create a new transport.<br>
</dd><dt>Remarks</dt><dd>A fake transport is necessary framework for sending PGM protocol packets in an absolute controlled manner.  SPMs will not be automatically published, and NAKs will be not be generated on detected data loss.<br>
<br>
Example output for creating a fake session:<br>
<pre>
*create fake* transport1<br>
READY<br>
</pre>

See <tt><a href='#create.md'>Application/create</a></tt>.<br>
</dd></dl>

### destroy ###
See <tt><a href='#destroy.md'>Application/destroy</a></tt>.


### net send data ###
<dl><dt>Syntax</dt><dd>
<pre>
net send odata _session_name_ _sequence_number_ _txw_trail_ _string_<br>
net send rdata _session_name_ _sequence_number_ _txw_trail_ _string_<br>
</pre>
</dd><dt>Purpose</dt><dd>Send an ODATA or RDATA packet with specified sequence number and TXW_TRAIL.<br>
</dd><dt>Remarks</dt><dd>Example output:<br>
<pre>
*net send odata _transport1 401 400 ringo_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>sequence_number</tt></td>
<td>Packet sequence number to request re-transmission of.</td>
</tr><tr>
<td><tt>txw_trail</tt></td>
<td>TXW_TRAIL: the trailing edge of the transmit window.</td>
</tr><tr>
<td><tt>string</tt></td>
<td>An alphanumeric string to send, must not contain any spaces.</td>
</tr>
</table>
</dd></dl>

### net send parity data ###
<dl><dt>Syntax</dt><dd>
<pre>
net send parity odata _session_name_ _sequence_number_ _txw_trail_ _strings_<br>
net send parity rdata _session_name_ _sequence_number_ _txw_trail_ _strings_<br>
</pre>
</dd><dt>Purpose</dt><dd>Send a parity ODATA (pro-active) or RDATA (on-demand) packet with specified sequence number and TXW_TRAIL.<br>
</dd><dt>Remarks</dt><dd>Example output:<br>
<pre>
*net send parity rdata _transport1 401 400 ringo ichigo momo budo_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>sequence_number</tt></td>
<td>Packet sequence number to request re-transmission of.</td>
</tr><tr>
<td><tt>txw_trail</tt></td>
<td>TXW_TRAIL: the trailing edge of the transmit window.</td>
</tr><tr>
<td><tt>string</tt></td>
<td>A list of alphanumeric strings representing the original data block, must not contain any spaces.</td>
</tr>
</table>
</dd></dl>

### net send nak ###
<dl><dt>Syntax</dt><dd>
<pre>
net send nak _session_name_ _TSI_ _sequence_number_ [ ,_sequence_number_ ]...<br>
</pre>
</dd><dt>Purpose</dt><dd>Send a NAK to a specified Transport Session Identifier (TSI).<br>
</dd><dt>Remarks</dt><dd>Example output for sending a NAK for sequence number #1 on the transport session <i>transport1</i> to the sender with session identifier "<i>4.232.81.47.21.115.34649</i>":<br>
<pre>
*net send nak _transport1 4.232.81.47.21.115.34649 1_*<br>
READY<br>
</pre>
Example sending a list of NAKs for sequence numbers 2,3,4:<br>
<pre>
*net send nak _transport1 4.232.81.47.21.115.34649 2,3,4_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>TSI</tt></td>
<td>Transport Session Identifier (TSI).</td>
</tr><tr>
<td><tt>sequence_number</tt></td>
<td>Packet sequence number to request re-transmission of.</td>
</tr>
</table>
</dd></dl>

### net send spm ###
<dl><dt>Syntax</dt><dd>
<pre>
net send spm _session_name_ _sequence_number_ _txw_trail_ _txw_lead_<br>
</pre>
</dd><dt>Purpose</dt><dd>Send a SPM with specified sequence numbers and transmit window parameters.<br>
</dd><dt>Remarks</dt><dd>Example output:<br>
<pre>
*net send spm _transport1 60 801 800_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>sequence_number</tt></td>
<td>Packet sequence number to request re-transmission of.</td>
</tr><tr>
<td><tt>txw_trail</tt></td>
<td>TXW_TRAIL: the trailing edge of the transmit window.</td>
</tr><tr>
<td><tt>txw_lead</tt></td>
<td>TXW_LEAD: the leading edge of the transmit window.</td>
</tr>
</table>
</dd></dl>

### net send parity spm ###
<dl><dt>Syntax</dt><dd>
<pre>
net send spm _session_name_ _sequence_number_ _txw_trail_ _txw_lead_ pro-active on-demand _k'<br>
</pre>
</dd><dt>Purpose</dt><dd>Send a parity SPM with specified sequence numbers, transmit window parameters, and which FEC services to advertise.<br>
</dd><dt>Remarks</dt><dd>Example output:<br>
<pre>
*net send spm _transport1 60 801 800_ on-demand _64_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>sequence_number</tt></td>
<td>Packet sequence number to request re-transmission of.</td>
</tr><tr>
<td><tt>txw_trail</tt></td>
<td>TXW_TRAIL: the trailing edge of the transmit window.</td>
</tr><tr>
<td><tt>txw_lead</tt></td>
<td>TXW_LEAD: the leading edge of the transmit window.</td>
</tr>
</table>
</dd></dl>

### net send spmr ###
<dl><dt>Syntax</dt><dd>
<pre>
net send spmr _session_name_ _TSI_<br>
</pre>
</dd><dt>Purpose</dt><dd>Send a SPMR to local multicast segment and unicast to the specified TSI.<br>
</dd><dt>Remarks</dt><dd>Example output:<br>
<pre>
*net send spmr _transport1 4.232.81.47.21.115.34649_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>session_name</tt></td>
<td>The transport identifier.</td>
</tr><tr>
<td><tt>TSI</tt></td>
<td>Transport Session Identifier.</td>
</tr>
</table>
</dd></dl>

### quit ###
See <tt><a href='#quit.md'>Application/quit</a></tt>.

### send ###
See <tt><a href='#send.md'>Application/send</a></tt>.

## Monitor ##
<dl><dt>Purpose</dt><dd>An independent network monitor to verify what is actually sent on the network.<br>
</dd><dt>Syntax</dt><dd>
<pre>
monitor<br>
</pre>
</dd><dt>Remarks</dt><dd>Control is provided by standard input with feedback to standard output, the available commands follow in alphabetical order.  After an input command is processed and is ready to accept another command the output "<tt>READY</tt>" is displayed.<br>
</dd></dl>

### filter ###
<dl><dt>Syntax</dt><dd>
<pre>
filter _IPv4_address_<br>
</pre>
</dd><dt>Purpose</dt><dd>Filter out packets that do not matched the provided IP address.<br>
</dd><dt>Remarks</dt><dd>Example output to filter for only packets from the host <i>10.6.28.31</i>:<br>
<pre>
*filter _10.6.28.31_*<br>
READY<br>
</pre>
</dd><dt>Parameters</dt><dd>
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>IPv4_address</tt></td>
<td>A dotted quad IPv4 address.</td>
</tr>
</table>
</dd></dl>

### quit ###
See <tt><a href='#quit.md'>Application/quit</a></tt>.