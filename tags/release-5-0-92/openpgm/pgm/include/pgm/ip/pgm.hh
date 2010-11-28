/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM protocol
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

#ifndef __PGM_IP_PGM_HH__
#define __PGM_IP_PGM_HH__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

namespace pgm {
#define restrict
#include <pgm/pgm.h>
}

namespace ip {

class pgm
{
public:
	/// The type of a PGM endpoint.
	typedef pgm_endpoint<pgm> endpoint;

	/// Construct to represent PGM over IPv4.
	static pgm v4()
	{
		return pgm (PF_INET);
	}

	/// Construct to represent PGM over IPv6.
	static pgm v6()
	{
		return pgm (PF_INET6);
	}

	/// Obtain an identifier for the type of the protocol.
	int type() const
	{
		return SOCK_SEQPACKET;
	}

	/// Obtain an identifier for the protocol.
	int protocol() const
	{
		return IPPROTO_PGM;
	}

	/// Obtain an identifier for the protocol family.
	int family() const
	{
		return family_;
	}

	/// The PGM socket type.
	typedef pgm_socket<pgm> socket;

	/// Compare two protocols for equality.
	friend bool operator== (const pgm& p1, const pgm& p2)
	{
		return p1.family_ == p2.family_;
	}

	/// Compare two protocols for inequality.
	friend bool operator!= (const pgm& p1, const pgm& p2)
	{
		return p1.family_ != p2.family_;
	}

private:
	// Construct with a specific family.
	explicit pgm (int family)
	: family_ (family)
	{
	}

	int family_;
};

} // namespace ip


#endif /* __PGM_IP_PGM_HH__ */
