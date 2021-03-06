/*	opendatacon
 *
 *	Copyright (c) 2014:
 *
 *		DCrip3fJguWgVCLrZFfA7sIGgvx1Ou3fHfCxnrz4svAi
 *		yxeOtDhDCXf1Z4ApgXvX5ahqQmzRfJ2DoX8S05SqHA==
 *
 *	Licensed under the Apache License, Version 2.0 (the "License");
 *	you may not use this file except in compliance with the License.
 *	You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 *	Unless required by applicable law or agreed to in writing, software
 *	distributed under the License is distributed on an "AS IS" BASIS,
 *	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *	See the License for the specific language governing permissions and
 *	limitations under the License.
 */
/*
 * asio.cpp
 *
 *  Created on: 11/07/2019
 *      Author: Neil Stephens <dearknarl@gmail.com>
 */

#include <opendatacon/asio.h>

//compile asio only in libODC
//ASIO_SEPARATE_COMPILATION lets other modules link to it
#include <asio/impl/src.hpp>

namespace odc
{

std::unique_ptr<asio::io_service::work> asio_service::make_work()
{
	return std::make_unique<asio::io_service::work>(*unwrap_this);
}
std::unique_ptr<asio::io_service::strand> asio_service::make_strand()
{
	return std::make_unique<asio::io_service::strand>(*unwrap_this);
}
std::unique_ptr<asio::steady_timer> asio_service::make_steady_timer()
{
	return std::make_unique<asio::steady_timer>(*unwrap_this);
}
std::unique_ptr<asio::steady_timer> asio_service::make_steady_timer(std::chrono::steady_clock::duration t)
{
	return std::make_unique<asio::steady_timer>(*unwrap_this, t);
}
std::unique_ptr<asio::steady_timer> asio_service::make_steady_timer(std::chrono::steady_clock::time_point t)
{
	return std::make_unique<asio::steady_timer>(*unwrap_this, t);
}
std::unique_ptr<asio::ip::tcp::resolver> asio_service::make_tcp_resolver()
{
	return std::make_unique<asio::ip::tcp::resolver>(*unwrap_this);
}
std::unique_ptr<asio::ip::tcp::socket> asio_service::make_tcp_socket()
{
	return std::make_unique<asio::ip::tcp::socket>(*unwrap_this);
}
std::unique_ptr<asio::ip::tcp::acceptor> asio_service::make_tcp_acceptor(asio::ip::tcp::resolver::iterator EndPoint)
{
	return std::make_unique<asio::ip::tcp::acceptor>(*unwrap_this,*EndPoint);
}
std::unique_ptr<asio::ip::tcp::acceptor> asio_service::make_tcp_acceptor()
{
	return std::make_unique<asio::ip::tcp::acceptor>(*unwrap_this);
}
std::unique_ptr<asio::ip::udp::resolver> asio_service::make_udp_resolver()
{
	return std::make_unique<asio::ip::udp::resolver>(*unwrap_this);
}
std::unique_ptr<asio::ip::udp::socket> asio_service::make_udp_socket()
{
	return std::make_unique<asio::ip::udp::socket>(*unwrap_this);
}

} //namespace odc
