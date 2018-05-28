/*	opendatacon
*
*	Copyright (c) 2018:
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
* MD3Port.cpp
*
*  Created on: 01/04/2018
*      Author: Scott Ellis <scott.ellis@novatex.com.au>
*/

#include "MD3Port.h"
#include <iostream>
#include "MD3PortConf.h"
#include <openpal/logging/LogLevels.h>

MD3Port::MD3Port(std::string aName, std::string aConfFilename, const Json::Value aConfOverrides) :
	DataPort(aName, aConfFilename, aConfOverrides),
	pConnection(nullptr)
{
	//the creation of a new MD3PortConf will get the point details
	pConf.reset(new MD3PortConf(ConfFilename, ConfOverrides));

	//We still may need to process the file (or overrides) to get Addr details:
	ProcessFile();
}

TCPClientServer MD3Port::ClientOrServer()
{
	MD3PortConf* pConf = static_cast<MD3PortConf*>(this->pConf.get());
	if (pConf->mAddrConf.ClientServer == TCPClientServer::DEFAULT)
		return TCPClientServer::SERVER;
	return pConf->mAddrConf.ClientServer;
}

//DataPort function for UI
/*
const Json::Value MD3Port::GetStatus() const
{
	auto ret_val = Json::Value();

	if (!enabled)
		ret_val["Result"] = "Port disabled";
	else if (link_dead)
		ret_val["Result"] = "Port enabled - link down";
	else if (status == opendnp3::LinkStatus::RESET)
		ret_val["Result"] = "Port enabled - link up (reset)";
	else if (status == opendnp3::LinkStatus::UNRESET)
		ret_val["Result"] = "Port enabled - link up (unreset)";
	else
		ret_val["Result"] = "Port enabled - link status unknown";

	return ret_val;
}
*/

void MD3Port::ProcessElements(const Json::Value& JSONRoot)
{
	// The points are handled by the MD3PointConf class.
	// This is all the port specfic stuff.

	if (!JSONRoot.isObject()) return;

	if (JSONRoot.isMember("IP") && JSONRoot.isMember("SerialDevice"))
		std::cout << "Warning: MD3 port serial device AND IP address specified - IP overrides" << std::endl;

	if (JSONRoot.isMember("IP"))
	{
		static_cast<MD3PortConf*>(pConf.get())->mAddrConf.IP = JSONRoot["IP"].asString();
		static_cast<MD3PortConf*>(pConf.get())->mAddrConf.SerialSettings.deviceName = "";
	}

	if (JSONRoot.isMember("Port"))
		static_cast<MD3PortConf*>(pConf.get())->mAddrConf.Port = JSONRoot["Port"].asUInt();

	if (JSONRoot.isMember("TCPClientServer"))
	{
		if (JSONRoot["TCPClientServer"].asString() == "CLIENT")
			static_cast<MD3PortConf*>(pConf.get())->mAddrConf.ClientServer = TCPClientServer::CLIENT;
		else if (JSONRoot["TCPClientServer"].asString() == "SERVER")
			static_cast<MD3PortConf*>(pConf.get())->mAddrConf.ClientServer = TCPClientServer::SERVER;
		else if (JSONRoot["TCPClientServer"].asString() == "DEFAULT")
			static_cast<MD3PortConf*>(pConf.get())->mAddrConf.ClientServer = TCPClientServer::DEFAULT;
		else
			std::cout << "Warning: Invalid TCP client/server type: " << JSONRoot["TCPClientServer"].asString() << ", should be CLIENT, SERVER, or DEFAULT." << std::endl;
	}

	if (JSONRoot.isMember("OutstationAddr"))
		static_cast<MD3PortConf*>(pConf.get())->mAddrConf.OutstationAddr = JSONRoot["OutstationAddr"].asUInt();

	if (JSONRoot.isMember("NewDigitalCommands"))
	{
		static_cast<MD3PortConf*>(pConf.get())->mAddrConf.NewDigitalCommands = JSONRoot["NewDigitalCommands"].asBool();
	}
}

int MD3Port::Limit(int val, int max)
{
	return val > max ? max : val;
}
MD3PortConf* MD3Port::MyConf()
{
	return static_cast<MD3PortConf*>(this->pConf.get());
}

std::shared_ptr<MD3PointConf> MD3Port::MyPointConf()
{
	return MyConf()->pPointConf;
}

void MD3Port::SetSendTCPDataFn(std::function<void(std::string)> Send)
{
	SendTCPDataFn = Send;
}

// Test only method for simulating input from the TCP Connection.
void MD3Port::InjectSimulatedTCPMessage(buf_t&readbuf)
{
	// Just pass to the Connection ReadCompletionHandler, as if it had come in from the TCP port
	pConnection->ReadCompletionHandler(readbuf);
}

// The only method that sends to the TCP Socket
void MD3Port::SendMD3Message(std::vector<MD3BlockData> &CompleteMD3Message)
{
	if (CompleteMD3Message.size() == 0)
	{
		LOG("MD3Port", openpal::logflags::ERR, "", "Tried to send an empty response");
		return;
	}

	// Turn the blocks into a binary string.
	std::string MD3Message;
	for (auto blk : CompleteMD3Message)
	{
		MD3Message += blk.ToBinaryString();
	}

	// This is a pointer to a function, so that we can hook it for testing. Otherwise calls the pSockMan Write templated function
	// Small overhead to allow for testing - Is there a better way? - could not hook the pSockMan->Write function and/or another passed in function due to differences between a method and a lambda
	if (SendTCPDataFn != nullptr)
		SendTCPDataFn(MD3Message);
	else
	{
		pConnection->Write(std::string(MD3Message));
	}
}