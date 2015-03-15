<img src='http://miru.hk/wiki/ODATA_incoming_-_valid.png' />

  * Reception of a valid ODATA packet will start a new session passing packet payload to the application.


<img src='http://miru.hk/wiki/ODATA_incoming_-_invalid.png' />

  * Reception of an invalid ODATA will be discarded.
  * Reception of an ODATA packet with an unmatched data-destination port will be discarded.


<img src='http://miru.hk/wiki/ODATA_incoming_-_unknown.png' />

  * Reception of valid ODATA will cause generation of SPM-Request (SPMR) if the NLA has not been learned from a broadcast SPM.


<img src='http://miru.hk/wiki/ODATA_incoming_-_lost.png' />

  * Reception of a valid ODATA following by another ODATA packet with a jump in sequence number should generate a NAK to the sender.


<img src='http://miru.hk/wiki/ODATA_incoming_-_complete.png' />

  * Reception of out of sequence ODATA will be stored in the receive window until contiguous data is available.
  * Completion of a sequence of contiguous data in the receive window will be passed upstream to the application.
