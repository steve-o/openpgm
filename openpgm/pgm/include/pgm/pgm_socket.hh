/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM socket
 *
 * Copyright (c) 2010 Miru Limited.
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

#ifndef __PGM_SOCKET_HH__
#define __PGM_SOCKET_HH__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <cerrno>
#ifndef _WIN32
#	include <cstddef>
#	include <sys/socket.h>
#else
#	include <ws2tcpip.h>
#endif

namespace cpgm {
#define restrict
#include <pgm/pgm.h>
}

template <typename Protocol>
class pgm_socket
{
public:
	/// The protocol type.
	typedef Protocol protocol_type;

	/// The endpoint type.
	typedef typename Protocol::endpoint endpoint_type;

	/// The native socket type.
	typedef struct cpgm::pgm_sock_t* native_type;

	/// Construct a pgm_socket without opening it.
	pgm_socket()
	{
	}

	// Open a new PGM socket implementation.
	bool open (::sa_family_t family, int sock_type, int protocol, cpgm::pgm_error_t** error)
	{
		return cpgm::pgm_socket (&this->native_type_, family, sock_type, protocol, error);
	}

	/// Close a PGM socket implementation.
	bool close (bool flush)
	{
		return pgm_close (this->native_type_, flush);
	}

	/// Get the native socket implementation.
	native_type native (void)
	{
		return this->native_type_;
	}

	// Bind the datagram socket to the specified local endpoint.
	bool bind (const endpoint_type& addr, cpgm::pgm_error_t** error)
	{
		return pgm_bind (this->native_type_, addr.data(), sizeof(addr.data()), error);
	}

	/// Connect the PGM socket to the specified endpoint.
	bool connect (cpgm::pgm_error_t** error)
	{
		return pgm_connect (this->native_type_, error);
	}

	/// Set a socket option.
	bool set_option (int level, int optname, const void* optval, ::socklen_t optlen)
	{
		return pgm_setsockopt (this->native_type_, level, optname, optval, optlen);
	}

	/// Get a socket option.
	bool get_option (int level, int optname, void* optval, ::socklen_t* optlen)
	{
		return pgm_getsockopt (this->native_type_, level, optname, optval, optlen);
	}

	/// Get the local endpoint.
	endpoint_type local_endpoint() const
	{
		endpoint_type endpoint;
		pgm_getsockname (this->native_type_, &endpoint);
		return endpoint;
	}

	/// Disable sends or receives on the socket.
	bool shutdown (int what)
	{
		int optname, v = 1;
#ifndef _WIN32
		if (SHUT_RD == what)		optname = cpgm::PGM_SEND_ONLY;
		else if (SHUT_WR == what)	optname = cpgm::PGM_RECV_ONLY;
#else
		if (SD_RECEIVE == what)		optname = cpgm::PGM_SEND_ONLY;
		else if (SD_SEND == what)	optname = cpgm::PGM_RECV_ONLY;
#endif
		else {
			errno = EINVAL;
			return false;
		}
		return pgm_setsockopt (this->native_type_, IPPROTO_PGM, optname, &v, sizeof(v));
	}

	/// Send some data on a connected socket.
	int send (const void* buf, std::size_t len, std::size_t* bytes_sent)
	{
		return pgm_send (this->native_type_, buf, len, bytes_sent);
	}

	/// Receive some data from the peer.
	int receive (void* buf, std::size_t len, int flags, std::size_t* bytes_read, cpgm::pgm_error_t** error)
	{
		return pgm_recv (this->native_type_, buf, len, flags, bytes_read, error);
	}

	/// Receive a datagram with the endpoint of the sender.
	int receive_from (void* buf, std::size_t len, int flags, std::size_t* bytes_read, endpoint_type* from, cpgm::pgm_error_t** error)
	{
		int ec;
		struct cpgm::pgm_sockaddr_t addr;
		socklen_t addrlen = sizeof (addr);
		ec = pgm_recvfrom (this->native_type_, buf, len, flags, bytes_read, &addr, &addrlen, error);
		from->port (addr.sa_port);
		from->address (addr.sa_addr);
/* TODO: set data-destination port */
		return ec;
	}

private:
	native_type native_type_;
};

#endif /* __PGM_SOCKET_HH__ */
