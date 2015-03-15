<img src='http://miru.hk/wiki/SPM_-_ignore.png' />

  * Reception of invalid SPM will be ignored.
  * Reception of SPM with non-matching data-destination port will be ignored.
  * Reception of SPMs with SPM sequence number older or equal to previous sequence number will be ignored.


<img src='http://miru.hk/wiki/SPM_-_session.png' />]

  * Reception of SPMs for new TSIs indicate a new PGM session.
  * Reception of SPMs for known TSIs update the stored NLA to send NAKs.


<img src='http://miru.hk/wiki/SPM_-_lost.png' />

  * Reception of SPM updates the status of the Tx Window indicating lost leading packets causing NAK generation.
