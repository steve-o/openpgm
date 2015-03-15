_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
struct pgm_iovec {<br>
#ifndef _WIN32<br>
void*		iov_base;<br>
size_t		iov_len;	/* length of data */<br>
#else<br>
u_long		iov_len;<br>
char*		iov_base;<br>
#endif /* _WIN32 */<br>
};<br>
</pre>

### Purpose ###
A scatter/gather message vector.

### Remarks ###
On Windows platforms <tt>pgm_iovec</tt> is compatible with <tt><a href='http://msdn.microsoft.com/en-us/library/ms741542(VS.85).aspx'>WSABUF</a></tt>, on Unix is compatible with <tt>struct iovec</tt>.

### See Also ###
  * <tt>struct <a href='OpenPgm5CReferencePgmSkBuffT.md'>pgm_sk_buff_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmSendv.md'>pgm_sendv()</a></tt><br>
</li><li><a href='OpenPgm5CReferencePgmSkbs.md'>PGM SKBs</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.<br>
</li><li><tt><a href='http://msdn.microsoft.com/en-us/library/ms741542(VS.85).aspx'>WSABUF</a></tt> in MSDN Winsock Reference.<br>
</li><li><a href='http://www.gnu.org/s/libc/manual/html_node/Scatter_002dGather.html'>Fast Scatter-Gather I/O</a> in the GNU C Library.<br>
</li><li><tt><a href='http://www.linux.com/learn/docs/man/4465-writev2'>readv(2) and writev(2)</a></tt> in Linux Programmer's Manual.