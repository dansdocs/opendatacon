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
* MD3OutStationPort.h
*
*  Created on: 01/04/2018
*      Author: Scott Ellis <scott.ellis@novatex.com.au>
*/

/* The out station port is connected to the Overall System Scada master, so the master thinks it is talking to an outstation.
 This code then fires off events to the connector, which the connected master port(s) (of some type DNP3/ModBus/MD3) will turn back into scada commands and send out to the "real" Outstation.
 So it makes sense to connect the SIM (which generates data) to a DNP3 Outstation which will feed the data back to the SCADA master.
 So an Event to an outstation will be data that needs to be sent up to the scada master.
 An event from an outstation will be a master control signal to turn something on or off.
*/
#include <iostream>
#include <future>
#include <regex>
#include <chrono>

#include "MD3.h"
#include "MD3Utility.h"
#include "MD3OutstationPort.h"


MD3OutstationPort::MD3OutstationPort(const std::string & aName, const std::string &aConfFilename, const Json::Value & aConfOverrides):
	MD3Port(aName, aConfFilename, aConfOverrides)
{
	// Don't load conf here, do it in MD3Port
	std::string over = "None";
	if (aConfOverrides.isObject()) over = aConfOverrides.toStyledString();

	SystemFlags.SetDigitalChangedFlagCalculationMethod(std::bind(&MD3OutstationPort::DigitalChangedFlagCalculationMethod, this));
	SystemFlags.SetTimeTaggedDataAvailableFlagCalculationMethod(std::bind(&MD3OutstationPort::TimeTaggedDataAvailableFlagCalculationMethod, this));

	IsOutStation = true;

	LOGDEBUG("MD3Outstation Constructor - " + aName + " - " + aConfFilename + " Overrides - " + over);
}

MD3OutstationPort::~MD3OutstationPort()
{
	Disable();
	pConnection->RemoveOutstation(MyConf->mAddrConf.OutstationAddr);
	// The pConnection will be closed by this point, so is not holding any resources, and will be freed on program close when the static list is destroyed.
}

void MD3OutstationPort::Enable()
{
	if (enabled) return;
	try
	{
		if (pConnection.get() == nullptr)
			throw std::runtime_error("Connection manager uninitialised");

		pConnection->Open(); // Any outstation can take the port down and back up - same as OpenDNP operation for multidrop
		enabled = true;
	}
	catch (std::exception& e)
	{
		LOGERROR("Problem opening connection : " + Name + " : " + e.what());
		return;
	}
}
void MD3OutstationPort::Disable()
{
	if (!enabled) return;
	enabled = false;

	if (pConnection.get() == nullptr)
		return;
	pConnection->Close(); // Any outstation can take the port down and back up - same as OpenDNP operation for multidrop
}

// Have to fire the SocketStateHandler for all other OutStations sharing this socket.
void MD3OutstationPort::SocketStateHandler(bool state)
{
	std::string msg;
	if (state)
	{
		PublishEvent(ConnectState::CONNECTED);
		msg = Name + ": Connection established.";
	}
	else
	{
		PublishEvent(ConnectState::DISCONNECTED);
		msg = Name + ": Connection closed.";
	}
	LOGINFO(msg);
}

void MD3OutstationPort::Build()
{
	std::string ChannelID = MyConf->mAddrConf.ChannelID();

	// Need a couple of things passed to the point table.
	MyPointConf->PointTable.Build(IsOutStation, MyPointConf->NewDigitalCommands, *pIOS);

	pConnection = MD3Connection::GetConnection(ChannelID); //Static method

	if (pConnection == nullptr)
	{
		pConnection.reset(new MD3Connection(pIOS, IsServer(), MyConf->mAddrConf.IP,
			std::to_string(MyConf->mAddrConf.Port), this, true, MyConf->TCPConnectRetryPeriodms)); // Retry period cannot be different for multidrop outstations

		MD3Connection::AddConnection(ChannelID, pConnection); //Static method
	}

	std::function<void(MD3Message_t &MD3Message)> aReadCallback = std::bind(&MD3OutstationPort::ProcessMD3Message, this, std::placeholders::_1);
	std::function<void(bool)> aStateCallback = std::bind(&MD3OutstationPort::SocketStateHandler, this, std::placeholders::_1);

	pConnection->AddOutstation(MyConf->mAddrConf.OutstationAddr, aReadCallback, aStateCallback );
}

void MD3OutstationPort::SendMD3Message(const MD3Message_t &CompleteMD3Message)
{
	if (CompleteMD3Message.size() == 0)
	{
		LOGERROR("OS - Tried to send an empty message to the TCP Port");
		return;
	}
	LOGDEBUG("OS - Sending Message - " + MD3MessageAsString(CompleteMD3Message));
	// Done this way just to get context into log messages.
	MD3Port::SendMD3Message(CompleteMD3Message);
}

#pragma region OpenDataConInteraction

#pragma region PerformEvents

// We are going to send a command to the opendatacon connector to do some kind of operation.
// If there is a master on that connector it will then send the command on down to the "real" outstation.
// This method will be called in response to data appearing on our TCP connection.
// Remember there can be multiple responders!
//
//TODO: This is the blocking code that Neil has talked about rewriting to use an async callback, so we don�t get stuck here.

CommandStatus MD3OutstationPort::Perform(std::shared_ptr<EventInfo> event, bool waitforresult)
{
	if (!enabled)
		return CommandStatus::UNDEFINED;

	// The ODC calls will ALWAYS return (at some point) with success or failure. Time outs are done by the ports on the TCP communications that they do.
	if (!waitforresult)
	{
		PublishEvent(event); // Could have a callback, but we are not waiting, so don't give it one!
		return CommandStatus::SUCCESS;
	}

	//NEIL: enquire about the possibility of the opendnp3 API having a callback for the result that would avoid the below polling loop
	std::atomic_bool cb_executed(false);
	CommandStatus cb_status;
	auto StatusCallback = std::make_shared<std::function<void(CommandStatus status)>>([&](CommandStatus status)
		{
			cb_status = status;
			cb_executed = true;
		});

	PublishEvent(event, StatusCallback);

	while (!cb_executed)
	{
		//This loop pegs a core and blocks the outstation strand,
		//	but there's no other way to wait for the result.
		//	We can maybe do some work while we wait.
		pIOS->poll_one();
	}
	return cb_status;
}

#pragma endregion PerformEvents


#pragma region DataEvents

// We received a change in data from an Event (from the opendatacon Connector) now store it so that it can be produced when the Scada master polls us
// for a group or individually on our TCP connection.
// What we return here is not used in anyway that I can see.
void MD3OutstationPort::Event(std::shared_ptr<const EventInfo> event, const std::string& SenderName, SharedStatusCallback_t pStatusCallback)
{
	if (!enabled)
	{
		return (*pStatusCallback)(CommandStatus::UNDEFINED);
	}

	auto event_type = event->GetEventType();
	size_t ODCIndex = event->GetIndex();

	switch (event->GetEventType())
	{
		case EventType::Analog:
		{
			uint16_t analogmeas = (uint16_t)event->GetPayload<EventType::Analog>();

			LOGDEBUG("OS - Received Event - Analog - Index " + std::to_string(ODCIndex) + " Value 0x" + to_hexstring(analogmeas));
			if (!MyPointConf->PointTable.SetAnalogValueUsingODCIndex(ODCIndex, analogmeas))
			{
				LOGERROR("Tried to set the value for an invalid analog point index " + std::to_string(ODCIndex));
				return (*pStatusCallback)(CommandStatus::UNDEFINED);
			}
			return (*pStatusCallback)(CommandStatus::SUCCESS);
		}
		case EventType::Counter:
		{
			uint16_t countermeas = event->GetPayload<EventType::Counter>();

			LOGDEBUG("OS - Received Event - Counter - Index " + std::to_string(ODCIndex) + " Value 0x" + to_hexstring(countermeas));
			if (!MyPointConf->PointTable.SetCounterValueUsingODCIndex(ODCIndex, countermeas))
			{
				LOGERROR("Tried to set the value for an invalid counter point index " + std::to_string(ODCIndex));
				return (*pStatusCallback)(CommandStatus::UNDEFINED);
			}
			return (*pStatusCallback)(CommandStatus::SUCCESS);
		}
		case EventType::Binary:
		{
			// MD3 only maintains a time tagged change list for digitals/binaries Epoch is 1970, 1, 1 - Same as for MD3
			MD3Time now = MD3Now(); // msec since epoch.
			MD3Time eventtime = event->GetTimestamp();
			uint8_t meas = event->GetPayload<EventType::Binary>();

			LOGDEBUG("OS - Received Event - Binary - Index " + std::to_string(ODCIndex) + " Value 0x" + to_hexstring(meas));

			// Check that the passed time is within 30 minutes of the actual time, if not use the current time
			if (MyPointConf->OverrideOldTimeStamps)
			{
				if (abs((int64_t)(now / 1000) - (int64_t)(eventtime / 1000)) < 60 * 30)
				{
					eventtime = now; // msec since epoch.
					LOGDEBUG("Binary time tag value is too far from current time (>30min) changing to current time. Point index " + std::to_string(ODCIndex));
				}
			}

			if (!MyPointConf->PointTable.SetBinaryValueUsingODCIndex(ODCIndex, meas, eventtime))
			{
				LOGERROR("Tried to set the value for an invalid binary point index " + std::to_string(ODCIndex));
				return (*pStatusCallback)(CommandStatus::UNDEFINED);
			}

			return (*pStatusCallback)(CommandStatus::SUCCESS);
		}
		case EventType::ConnectState:
		{
			auto state = event->GetPayload<EventType::ConnectState>();

			// This will be fired by (typically) an MD3OutMaster port on the "other" side of the ODC Event bus. i.e. something downstream has connected
			// We should probably send any Digital or Analog outputs, but we can't send POM (Pulse)

			if (state == ConnectState::CONNECTED)
			{
				LOGDEBUG("Upstream (other side of ODC) port enabled - So a Master will send us events - and we can send what we have over ODC ");
				// We don�t know the state of the upstream data, so send event information for all points.

			}
			else if (state == ConnectState::DISCONNECTED)
			{
				// If we were an on demand connection, we would take down the connection . For MD3 we are using persistent connections only.
				// We have lost an ODC connection, so events we send don't go anywhere.
			}

			return (*pStatusCallback)(CommandStatus::SUCCESS);
		}
		case EventType::AnalogQuality:
		{
			if ((event->GetQuality() != QualityFlags::ONLINE))
			{
				if (!MyPointConf->PointTable.SetAnalogValueUsingODCIndex(ODCIndex, (uint16_t)0x8000))
				{
					LOGERROR("Tried to set the failure value for an invalid analog point index " + std::to_string(ODCIndex));
					return (*pStatusCallback)(CommandStatus::UNDEFINED);
				}
			}
			return (*pStatusCallback)(CommandStatus::SUCCESS);
		}
		case EventType::CounterQuality:
		{
			if ((event->GetQuality() != QualityFlags::ONLINE))
			{
				if (!MyPointConf->PointTable.SetCounterValueUsingODCIndex(ODCIndex, (uint16_t)0x8000))
				{
					LOGERROR("Tried to set the failure value for an invalid counter point index " + std::to_string(ODCIndex));
					return (*pStatusCallback)(CommandStatus::UNDEFINED);
				}
			}
			return (*pStatusCallback)(CommandStatus::SUCCESS);
		}
		default:
			return (*pStatusCallback)(CommandStatus::NOT_SUPPORTED);
	}
}



#pragma endregion

#pragma endregion OpenDataConInteraction