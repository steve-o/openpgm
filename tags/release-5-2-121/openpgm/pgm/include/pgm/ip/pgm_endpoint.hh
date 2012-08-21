/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM endpoint
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

#ifndef __PGM_IP_PGM_ENDPOINT_HH__
#define __PGM_IP_PGM_ENDPOINT_HH__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

namespace pgm {
#define restrict
#include <pgm/pgm.h>
}

namespace ip {

template <typename InternetProtocol>
class pgm_endpoint
{
public:
	/// The protocol type associated with the endpoint.
	typedef InternetProtocol protocol_type;

	typedef struct cpgm::pgm_sockaddr_t data_type;

	/// Default constructor.
	pgm_endpoint()
	: data_()
	{
		data_.sa_port = 0;
		cpgm::pgm_tsi_t tmp_addr = PGM_TSI_INIT;
		data_.sa_addr = tmp_addr;
	}

	/// Construct an endpoint using a port number, specified in host byte
	/// order.  The GSI will be generated from the node name.
	/**
	 * @par examples
	 * To initialise a PGM endpoint for port 7500, use:
	 * @code
	 * ip::pgm::endpoint ep (7500);
	 * @endcode
	 */
	pgm_endpoint (unsigned short port_num)
	: data_()
	{
		data_.sa_port = port_num;
		data_.sa_addr.sport = 0;
		cpgm::pgm_gsi_create_from_hostname (&data_.sa_addr.gsi, NULL);
	}

	/// Construct an endpoint using a port number and a TSI.
	pgm_endpoint (const cpgm::pgm_tsi_t& tsi, unsigned short port_num)
	: data_()
	{
		data_.sa_port = port_num;
		data_.sa_addr = tsi;
	}

	/// Construct an endpoint using a port number and a memory area.
	pgm_endpoint (const void* src, std::size_t len, unsigned short port_num)
	: data_()
	{
		data_.sa_port = port_num;
		data_.sa_addr.sport = 0;
		cpgm::pgm_gsi_create_from_data (&data_.sa_addr.gsi, static_cast<const uint8_t*>(src), len);
	}

	/// Copy constructor.
	pgm_endpoint (const pgm_endpoint& other)
	: data_ (other.data_)
	{
	}

	/// Assign from another endpoint.
	pgm_endpoint& operator= (const pgm_endpoint& other)
	{
		data_ = other.data_;
		return *this;
	}

	/// Get the underlying endpoint in the native type.
	const data_type* data() const
	{
		return &data_;
	}

	/// Get the underlying size of the endpoint in the native type.
	std::size_t size() const
	{
		return sizeof(data_type);
	}

	/// Get the port associated with the endpoint. The port number is always in
	/// the host's byte order.
	unsigned short port() const
	{
		return data_.sa_port;
	}

	/// Set the port associated with the endpoint. The port number is always in
	/// the host's byte order.
	void port (unsigned short port_num)
	{
		data_.sa_port = port_num;
	}

	/// Get the TSI associated with the endpoint.
	const cpgm::pgm_tsi_t* address() const
	{
		return &data_.sa_addr;
	}

	/// Set the TSI associated with the endpoint.
	void address (cpgm::pgm_tsi_t& addr)
	{
		data_.sa_addr = addr;
	}

	/// Compare two endpoints for equality.
	friend bool operator== (const pgm_endpoint<InternetProtocol>& e1,
		const pgm_endpoint<InternetProtocol>& e2)
	{
		return e1.address() == e2.address() && e1.port() == e2.port();
	}

	/// Compare two endpoints for inequality.
	friend bool operator!= (const pgm_endpoint<InternetProtocol>& e1,
		const pgm_endpoint<InternetProtocol>& e2)
	{
		return e1.address() != e2.address() || e1.port() != e2.port();
	}

	/// Compare endpoints for ordering.
	friend bool operator<(const pgm_endpoint<InternetProtocol>& e1,
		const pgm_endpoint<InternetProtocol>& e2)
	{
		if (e1.address() < e2.address())
			return true;
		if (e1.address() != e2.address())
			return false;
		return e1.port() < e2.port();
	}

private:
	// The underlying PGM socket address.
	data_type data_;
};

} // namespace ip

#endif /* __PGM_IP_PGM_ENDPOINT_HH__ */
