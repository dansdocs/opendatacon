/*	opendatacon
 *
 *	Copyright (c) 2017:
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
 * PyPort.cpp
 *
 *  Created on: 26/03/2017
 *      Author: Alan Murray <alan@atmurray.net>
 *		Extensively modifed: Scott Ellis <scott.ellis@novatex.com.au>
 */


// Python Interface:
// We need to pass the config JSON (both main and overrides) to the Python class, so that it has all the information that we have.
// We need to call a Python method every time we get an event, we assume it will not call network or other long running functions. We wait for it to finish.
// We need to export a function to the Python code, so that it can Post an Event into the ODC event bus.
// Do we need to call a Python like timer method, that will allow the Python code to set how long it should be until it is next called.
// So leave the extension bit out for the moment, just get to the pont where we can load the class and call its methods...

#include "PyPort.h"
#include <chrono>
#include <ctime>
#include <time.h>
#include <iomanip>
#include "../submodules/asio-1.12.2/include/asio/strand.hpp"

using namespace odc;

std::shared_ptr<asio::io_context::strand> PyPort::python_strand = nullptr;

// Constructor for PyPort --------------------------------------
PyPort::PyPort(const std::string& aName, const std::string& aConfFilename, const Json::Value& aConfOverrides):
	DataPort(aName, aConfFilename, aConfOverrides),
	JSONMain(""),
	JSONOverride("")
{
	//the creation of a new PyPortConf will get the point details
	pConf.reset(new PyPortConf(ConfFilename, ConfOverrides));

	// Just to save a lot of dereferencing..
	MyConf = static_cast<PyPortConf*>(this->pConf.get());

	// We still may need to process the file (or overrides) to get Addr details:
	ProcessFile();
}

PyPort::~PyPort()
{
	LOGDEBUG("Destructing PyPort");

	python_strand->dispatch([&]()
		{
			LOGDEBUG("reset pWrapper");
			pWrapper.reset();
			// Only 1 strand per ODC system.
			if (python_strand.use_count() == 1)
			{
			      LOGDEBUG("reset python_strand");
			//			python_strand.reset();
			}
		});
}

// The ASIO IOS instance is up, our config files have been read and parsed, this is the opportunity to kick off connections and scheduled processes
void PyPort::Build()
{
	// Only 1 strand per ODC system. Must wait until build as pIOS is not available in the constructor
	if (python_strand == nullptr)
	{
		LOGDEBUG("Create python_strand");
		python_strand.reset(new asio::io_context::strand(*pIOS));
	}

	// Every call to pWrapper should be strand protected.
	python_strand->dispatch([&]()
		{
			// If first time constructor is called, will instansiate the interpreter.
			// Pass in a pointer to our SetTimer method, so it can be called from Python code - bit circular - I know!
			// Also pass in a PublishEventCall method, so Python can send us Events to Publish.
			pWrapper.reset(new PythonWrapper(this->Name, std::bind(&PyPort::SetTimer, this, std::placeholders::_1, std::placeholders::_2),
				std::bind(&PyPort::PublishEventCall, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)));

			// Python code is loaded and class created, __init__ called.
			pWrapper->Build("PyPort", MyConf->pyModuleName, MyConf->pyClassName, this->Name);

			pWrapper->Config(JSONMain, JSONOverride);
		});

	LOGDEBUG("Loaded \"{}\" ", MyConf->pyModuleName);
}

void PyPort::Enable()
{
	if (enabled) return;
	enabled = true;

	python_strand->dispatch([&]()
		{
			pWrapper->Enable();
		});
};

void PyPort::Disable()
{
	if (!enabled) return;
	enabled = false;

	python_strand->dispatch([&]()
		{
			pWrapper->Disable();
		});
};

// We pass this method string values for fields (from Python) parse them and create the ODC event then send it.
// A pointer to this method is passed to the Wrapper class so it can be called when we get information from the Python code.
void PyPort::PublishEventCall(const char* EventType, uint32_t ODCIndex,  const char* Quality, const char* Payload )
{
	LOGDEBUG("PyPort Publish Event {}, {}, {}, {}", EventType, ODCIndex, Quality, Payload);
	// Parse strings to create event - allow time to default to now.
	/*double fval = 100.1;
	ODCIndex = 1001;
	QualityFlags qual = QualityFlags::ONLINE;
	auto event2 = std::make_shared<EventInfo>(EventType::Analog, ODCIndex, qual);
	event2->SetPayload<EventType::Analog>(std::move(fval));
	*/
	//PublishEvent(std::shared_ptr<EventInfo> event, SharedStatusCallback_t pStatusCallback = std::make_shared<std::function<void(CommandStatus status)>>([](CommandStatus status) {}))
}
// So we have received an event from the ODC message bus - it will be Control or Connect events.
// So basically the Event mechanism is agnostinc. You can be a producer of control events and a consumer of data events, or the reverse, or in some odd cases -
// A consumer of both types of events (this means you are just a listener on a conversation between two other devices.)
void PyPort::Event(std::shared_ptr<const EventInfo> event, const std::string& SenderName, SharedStatusCallback_t pStatusCallback)
{
	if (!enabled)
	{
		LOGDEBUG("PyPort {} not enabled, Event from {} ignored", Name, SenderName);
		PostCallbackCall(pStatusCallback, CommandStatus::UNDEFINED);
		return;
	}

	python_strand->dispatch([&, event, SenderName, pStatusCallback]()
		{
			CommandStatus result = pWrapper->Event(event, SenderName); // Expect no long processing or waits in the python code to handle this.

			PostCallbackCall(pStatusCallback, result);
		});
}
void PyPort::SetTimer(uint32_t id, uint32_t delayms)
{
	if (!enabled)
	{
		LOGDEBUG("PyPort {} not enabled, SetTimer call ignored", Name);
		return;
	}

	pTimer_t timer(new Timer_t(*pIOS));
	timer->expires_from_now(std::chrono::milliseconds(delayms));
	timer->async_wait(
		[&, id, timer](asio::error_code err_code) // Pass in shared ptr to keep it alive until we are done - time out or aborted
		{
			if (err_code != asio::error::operation_aborted)
			{
			      python_strand->dispatch([&,id]()
					{
						pWrapper->CallTimerHandler(id);
					});

			}
		});
};
// This is called when we have decoded a restful request, to the point where we know which instance it should be passed to. We give it a callback, which will
// be used to actually send the response back to the caller.
void PyPort::RestHandler(const std::string& url, ResponseCallback_t pResponseCallback)
{
	if (!enabled)
	{
		LOGDEBUG("PyPort {} not enabled, Restful Request ignored", Name);
		PostResponseCallbackCall(pResponseCallback, "Error Port not enambled");
		return;
	}

	python_strand->dispatch([&, url, pResponseCallback]()
		{
			std::string result = pWrapper->RestHandler(url); // Expect no long processing or waits in the python code to handle this.

			PostResponseCallbackCall(pResponseCallback, result);
		});
}

// Just schedule the callback, don't want to do it in a strand protected section.
void PyPort::PostCallbackCall(const odc::SharedStatusCallback_t& pStatusCallback, CommandStatus c)
{
	if (pStatusCallback != nullptr)
	{
		pIOS->post([&, pStatusCallback, c]()
			{
				(*pStatusCallback)(c);
			});
	}
}
void PyPort::PostResponseCallbackCall(const ResponseCallback_t & pResponseCallback, const std::string& response)
{
	if (pResponseCallback != nullptr)
	{
		pIOS->post([&, pResponseCallback, response]()
			{
				(*pResponseCallback)(response);
			});
	}
}

// This should be called twice, once for the config file setion, and the second for config overrides.
void PyPort::ProcessElements(const Json::Value& JSONRoot)
{
// We need to strip comments from the JSON here, as Python JSON handling libraries throw on finding comments.
	if (JSONMain.length() == 0)
	{
		Json::StreamWriterBuilder wbuilder;
		wbuilder["commentStyle"] = "None";                // No comments
		JSONMain = Json::writeString(wbuilder, JSONRoot); // Spit the root out as string, so we can pass to Python in build.
	}
	else if (JSONOverride.length() == 0)
	{
		Json::StreamWriterBuilder wbuilder;
		wbuilder["commentStyle"] = "None";                    // No comments
		JSONOverride = Json::writeString(wbuilder, JSONRoot); // Spit the root out as string, so we can pass to Python in build.
	}

	if (JSONRoot.isMember("ModuleName"))
		MyConf->pyModuleName = JSONRoot["ModuleName"].asString();
	if (JSONRoot.isMember("ClassName"))
		MyConf->pyClassName = JSONRoot["ClassName"].asString();
}
