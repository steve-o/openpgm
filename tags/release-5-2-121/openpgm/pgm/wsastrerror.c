/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Winsock Error strings.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>

#ifndef _WIN32

/* compatibility stubs */

char *
pgm_wsastrerror (
	const int	wsa_errnum
	)
{
	return _("Unknown.");
}

char*
pgm_adapter_strerror (
	const int	adapter_errnum
	)
{
	return _("Unknown.");
}

char*
pgm_win_strerror (
	char*		buf,
	size_t		buflen,
	const int	win_errnum
	)
{
	pgm_strncpy_s (buf, buflen, _("Unknown."), _TRUNCATE);
	return buf;
}

#else
#	include <ws2tcpip.h>

char*
pgm_wsastrerror (
	const int	wsa_errnum
	)
{
	switch (wsa_errnum) {
#ifdef WSA_INVALID_HANDLE
	case WSA_INVALID_HANDLE: return _("Specified event object handle is invalid.");
#endif
#ifdef WSA_NOT_ENOUGH_MEMORY
	case WSA_NOT_ENOUGH_MEMORY: return _("Insufficient memory available.");
#endif
#ifdef WSA_INVALID_PARAMETER
	case WSA_INVALID_PARAMETER: return _("One or more parameters are invalid.");
#endif
#ifdef WSA_OPERATION_ABORTED
	case WSA_OPERATION_ABORTED: return _("Overlapped operation aborted.");
#endif
#ifdef WSA_IO_INCOMPLETE
	case WSA_IO_INCOMPLETE: return _("Overlapped I/O event object not in signaled state.");
#endif
#ifdef WSA_IO_PENDING
	case WSA_IO_PENDING: return _("Overlapped operations will complete later.");
#endif
#ifdef WSAEINTR
	case WSAEINTR: return _("Interrupted function call.");
#endif
#ifdef WSAEBADF
	case WSAEBADF: return _("File handle is not valid.");
#endif
#ifdef WSAEACCES
	case WSAEACCES: return _("Permission denied.");
#endif
#ifdef WSAEFAULT
	case WSAEFAULT: return _("Bad address.");
#endif
#ifdef WSAEINVAL
	case WSAEINVAL: return _("Invalid argument.");
#endif
#ifdef WSAEMFILE
	case WSAEMFILE: return _("Too many open files.");
#endif
#ifdef WSAEWOULDBLOCK
	case WSAEWOULDBLOCK: return _("Resource temporarily unavailable.");
#endif
#ifdef WSAEINPROGRESS
	case WSAEINPROGRESS: return _("Operation now in progress.");
#endif
#ifdef WSAEALREADY
	case WSAEALREADY: return _("Operation already in progress.");
#endif
#ifdef WSAENOTSOCK
	case WSAENOTSOCK: return _("Socket operation on nonsocket.");
#endif
#ifdef WSAEDESTADDRREQ
	case WSAEDESTADDRREQ: return _("Destination address required.");
#endif
#ifdef WSAEMSGSIZE
	case WSAEMSGSIZE: return _("Message too long.");
#endif
#ifdef WSAEPROTOTYPE
	case WSAEPROTOTYPE: return _("Protocol wrong type for socket.");
#endif
#ifdef WSAENOPROTOOPT
	case WSAENOPROTOOPT: return _("Bad protocol option.");
#endif
#ifdef WSAEPROTONOSUPPORT
	case WSAEPROTONOSUPPORT: return _("Protocol not supported.");
#endif
#ifdef WSAESOCKTNOSUPPORT
	case WSAESOCKTNOSUPPORT: return _("Socket type not supported.");
#endif
#ifdef WSAEOPNOTSUPP
	case WSAEOPNOTSUPP: return _("Operation not supported.");
#endif
#ifdef WSAEPFNOSUPPORT
	case WSAEPFNOSUPPORT: return _("Protocol family not supported.");
#endif
#ifdef WSAEAFNOSUPPORT
	case WSAEAFNOSUPPORT: return _("Address family not supported by protocol family.");
#endif
#ifdef WSAEADDRINUSE
	case WSAEADDRINUSE: return _("Address already in use.");
#endif
#ifdef WSAEADDRNOTAVAIL
	case WSAEADDRNOTAVAIL: return _("Cannot assign requested address.");
#endif
#ifdef WSAENETDOWN
	case WSAENETDOWN: return _("Network is down.");
#endif
#ifdef WSAENETUNREACH
	case WSAENETUNREACH: return _("Network is unreachable.");
#endif
#ifdef WSAENETRESET
	case WSAENETRESET: return _("Network dropped connection on reset.");
#endif
#ifdef WSAECONNABORTED
	case WSAECONNABORTED: return _("Software caused connection abort.");
#endif
#ifdef WSAECONNRESET
	case WSAECONNRESET: return _("Connection reset by peer.");
#endif
#ifdef WSAENOBUFS
	case WSAENOBUFS: return _("No buffer space available.");
#endif
#ifdef WSAEISCONN
	case WSAEISCONN: return _("Socket is already connected.");
#endif
#ifdef WSAENOTCONN
	case WSAENOTCONN: return _("Socket is not connected.");
#endif
#ifdef WSAESHUTDOWN
	case WSAESHUTDOWN: return _("Cannot send after socket shutdown.");
#endif
#ifdef WSAETOOMANYREFS
	case WSAETOOMANYREFS: return _("Too many references.");
#endif
#ifdef WSAETIMEDOUT
	case WSAETIMEDOUT: return _("Connection timed out.");
#endif
#ifdef WSAECONNREFUSED
	case WSAECONNREFUSED: return _("Connection refused.");
#endif
#ifdef WSAELOOP
	case WSAELOOP: return _("Cannot translate name.");
#endif
#ifdef WSAENAMETOOLONG
	case WSAENAMETOOLONG: return _("Name too long.");
#endif
#ifdef WSAEHOSTDOWN
	case WSAEHOSTDOWN: return _("Host is down.");
#endif
#ifdef WSAEHOSTUNREACH
	case WSAEHOSTUNREACH: return _("No route to host.");
#endif
#ifdef WSAENOTEMPTY
	case WSAENOTEMPTY: return _("Directory not empty.");
#endif
#ifdef WSAEPROCLIM
	case WSAEPROCLIM: return _("Too many processes.");
#endif
#ifdef WSAEUSERS
	case WSAEUSERS: return _("User quota exceeded.");
#endif
#ifdef WSAEDQUOT
	case WSAEDQUOT: return _("Disk quota exceeded.");
#endif
#ifdef WSAESTALE
	case WSAESTALE: return _("Stale file handle reference.");
#endif
#ifdef WSAEREMOTE
	case WSAEREMOTE: return _("Item is remote.");
#endif
#ifdef WSASYSNOTREADY
	case WSASYSNOTREADY: return _("Network subsystem is unavailable.");
#endif
#ifdef WSAVERNOTSUPPORTED
	case WSAVERNOTSUPPORTED: return _("Winsock.dll version out of range.");
#endif
#ifdef WSANOTINITIALISED
	case WSANOTINITIALISED: return _("Successful WSAStartup not yet performed.");
#endif
#ifdef WSAEDISCON
	case WSAEDISCON: return _("Graceful shutdown in progress.");
#endif
#ifdef WSAENOMORE
	case WSAENOMORE: return _("No more results.");
#endif
#ifdef WSAECANCELLED
	case WSAECANCELLED: return _("Call has been canceled.");
#endif
#ifdef WSAEINVALIDPROCTABLE
	case WSAEINVALIDPROCTABLE: return _("Procedure call table is invalid.");
#endif
#ifdef WSAEINVALIDPROVIDER
	case WSAEINVALIDPROVIDER: return _("Service provider is invalid.");
#endif
#ifdef WSAEPROVIDERFAILEDINIT
	case WSAEPROVIDERFAILEDINIT: return _("Service provider failed to initialize.");
#endif
#ifdef WSASYSCALLFAILURE
	case WSASYSCALLFAILURE: return _("System call failure.");
#endif
#ifdef WSASERVICE_NOT_FOUND
	case WSASERVICE_NOT_FOUND: return _("Service not found.");
#endif
#ifdef WSATYPE_NOT_FOUND
	case WSATYPE_NOT_FOUND: return _("Class type not found.");
#endif
#ifdef WSA_E_NO_MORE
	case WSA_E_NO_MORE: return _("No more results.");
#endif
#ifdef WSA_E_CANCELLED
	case WSA_E_CANCELLED: return _("Call was canceled.");
#endif
#ifdef WSAEREFUSED
	case WSAEREFUSED: return _("Database query was refused.");
#endif
#ifdef WSAHOST_NOT_FOUND
	case WSAHOST_NOT_FOUND: return _("Host not found.");
#endif
#ifdef WSATRY_AGAIN
	case WSATRY_AGAIN: return _("Nonauthoritative host not found.");
#endif
#ifdef WSANO_RECOVERY
	case WSANO_RECOVERY: return _("This is a nonrecoverable error.");
#endif
#ifdef WSANO_DATA
	case WSANO_DATA: return _("Valid name, no data record of requested type.");
#endif
#ifdef WSA_QOS_RECEIVERS
	case WSA_QOS_RECEIVERS: return _("QOS receivers.");
#endif
#ifdef WSA_QOS_SENDERS
	case WSA_QOS_SENDERS: return _("QOS senders.");
#endif
#ifdef WSA_QOS_NO_SENDERS
	case WSA_QOS_NO_SENDERS: return _("No QOS senders.");
#endif
#ifdef WSA_QOS_NO_RECEIVERS
	case WSA_QOS_NO_RECEIVERS: return _("QOS no receivers.");
#endif
#ifdef WSA_QOS_REQUEST_CONFIRMED
	case WSA_QOS_REQUEST_CONFIRMED: return _("QOS request confirmed.");
#endif
#ifdef WSA_QOS_ADMISSION_FAILURE
	case WSA_QOS_ADMISSION_FAILURE: return _("QOS admission error.");
#endif
#ifdef WSA_QOS_POLICY_FAILURE
	case WSA_QOS_POLICY_FAILURE: return _("QOS policy failure.");
#endif
#ifdef WSA_QOS_BAD_STYLE
	case WSA_QOS_BAD_STYLE: return _("QOS bad style.");
#endif
#ifdef WSA_QOS_BAD_OBJECT
	case WSA_QOS_BAD_OBJECT: return _("QOS bad object.");
#endif
#ifdef WSA_QOS_TRAFFIC_CTRL_ERROR
	case WSA_QOS_TRAFFIC_CTRL_ERROR: return _("QOS traffic control error.");
#endif
#ifdef WSA_QOS_GENERIC_ERROR
	case WSA_QOS_GENERIC_ERROR: return _("QOS generic error.");
#endif
#ifdef WSA_QOS_ESERVICETYPE
	case WSA_QOS_ESERVICETYPE: return _("QOS service type error.");
#endif
#ifdef WSA_QOS_EFLOWSPEC
	case WSA_QOS_EFLOWSPEC: return _("QOS flowspec error.");
#endif
#ifdef WSA_QOS_EPROVSPECBUF
	case WSA_QOS_EPROVSPECBUF: return _("Invalid QOS provider buffer.");
#endif
#ifdef WSA_QOS_EFILTERSTYLE
	case WSA_QOS_EFILTERSTYLE: return _("Invalid QOS filter style.");
#endif
#ifdef WSA_QOS_EFILTERTYPE
	case WSA_QOS_EFILTERTYPE: return _("Invalid QOS filter type.");
#endif
#ifdef WSA_QOS_EFILTERCOUNT
	case WSA_QOS_EFILTERCOUNT: return _("Incorrect QOS filter count.");
#endif
#ifdef WSA_QOS_EOBJLENGTH
	case WSA_QOS_EOBJLENGTH: return _("Invalid QOS object length.");
#endif
#ifdef WSA_QOS_EFLOWCOUNT
	case WSA_QOS_EFLOWCOUNT: return _("Incorrect QOS flow count.");
#endif
#ifdef WSA_QOS_EUNKOWNPSOBJ
	case WSA_QOS_EUNKOWNPSOBJ: return _("Unrecognized QOS object.");
#endif
#ifdef WSA_QOS_EPOLICYOBJ
	case WSA_QOS_EPOLICYOBJ: return _("Invalid QOS policy object.");
#endif
#ifdef WSA_QOS_EFLOWDESC
	case WSA_QOS_EFLOWDESC: return _("Invalid QOS flow descriptor.");
#endif
#ifdef WSA_QOS_EPSFLOWSPEC
	case WSA_QOS_EPSFLOWSPEC: return _("Invalid QOS provider-specific flowspec.");
#endif
#ifdef WSA_QOS_EPSFILTERSPEC
	case WSA_QOS_EPSFILTERSPEC: return _("Invalid QOS provider-specific filterspec.");
#endif
#ifdef WSA_QOS_ESDMODEOBJ
	case WSA_QOS_ESDMODEOBJ: return _("Invalid QOS shape discard mode object.");
#endif
#ifdef WSA_QOS_ESHAPERATEOBJ
	case WSA_QOS_ESHAPERATEOBJ: return _("Invalid QOS shaping rate object.");
#endif
#ifdef WSA_QOS_RESERVED_PETYPE
	case WSA_QOS_RESERVED_PETYPE: return _("Reserved policy QOS element type.");
#endif
	default: return _("Unknown.");
	}
}

char*
pgm_adapter_strerror (
	const int	adapter_errnum
	)
{
	switch (adapter_errnum) {
#ifdef ERROR_ADDRESS_NOT_ASSOCIATED
	case ERROR_ADDRESS_NOT_ASSOCIATED: return _("DHCP lease information was available.");
#endif
#ifdef ERROR_BUFFER_OVERFLOW
	case ERROR_BUFFER_OVERFLOW: return _("The buffer to receive the adapter information is too small.");
#endif
#ifdef ERROR_INVALID_DATA
	case ERROR_INVALID_DATA: return _("Invalid adapter information was retrieved.");
#endif
#ifdef ERROR_INVALID_PARAMETER
	case ERROR_INVALID_PARAMETER: return _("One of the parameters is invalid.");
#endif
#ifdef ERROR_NOT_ENOUGH_MEMORY
	case ERROR_NOT_ENOUGH_MEMORY: return _("Insufficient memory resources are available to complete the operation.");
#endif
#ifdef ERROR_NO_DATA
	case ERROR_NO_DATA: return _("No adapter information exists for the local computer.");
#endif
#ifdef ERROR_NOT_SUPPORTED
	case ERROR_NOT_SUPPORTED: return _("The GetAdaptersInfo function is not supported by the operating system running on the local computer..");
#endif
	default: return _("Other.");
	}
}

char*
pgm_win_strerror (
	char*		buf,
	size_t		buflen,
	const int	win_errnum
	)
{
	const DWORD nSize = (DWORD)buflen;
	FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
		       NULL,		/* source */
		       win_errnum,	/* message id */
		       MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),	/* language id */
		       (LPTSTR)buf,
		       nSize,
		       NULL);		/* arguments */
	return buf;
}

#endif /* _WIN32 */

/* eof */
