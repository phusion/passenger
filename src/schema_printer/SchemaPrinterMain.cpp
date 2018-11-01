/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */

/*
 * SchemaPrinterMain.cpp is automatically generated from SchemaPrinterMain.cpp.cxxcodebuilder,
 * using "BEGIN ConfigKit schema" magic comments from various C++ source files.
 * Edits to SchemaPrinterMain.cpp will be lost.
 *
 * To update SchemaPrinterMain.cpp:
 *   rake apache2
 *   -OR-
 *   rake nginx
 *
 * To force regeneration of SchemaPrinterMain.cpp:
 *   rm -f src/schema_printer/SchemaPrinterMain.cpp
 *   rake src/schema_printer/SchemaPrinterMain.cpp
 */

#include <ev++.h> // include first in order to avoid macro clash on EV_ERROR
#include <iostream>
#include <Core/AdminPanelConnector.h>
#include <Core/ApiServer.h>
#include <Core/Config.h>
#include <Core/Controller/Config.h>
#include <Core/SecurityUpdateChecker.h>
#include <Core/TelemetryCollector.h>
#include <Watchdog/ApiServer.h>
#include <Watchdog/Config.h>
#include <LoggingKit/Config.h>
#include <ServerKit/Config.h>
#include <ServerKit/HttpServer.h>
#include <ServerKit/Server.h>
#include <WebSocketCommandReverseServer.h>

int
main(int argc, char *argv[]) {
	Passenger::Json::Value doc(Passenger::Json::objectValue);

	doc["Passenger::Core::AdminPanelConnector::Schema"] = Passenger::Core::AdminPanelConnector::Schema().inspect();
	doc["Passenger::Core::ApiServer::Schema"] = Passenger::Core::ApiServer::Schema().inspect();
	doc["Passenger::Core::ControllerSchema"] = Passenger::Core::ControllerSchema().inspect();
	doc["Passenger::Core::ControllerSingleAppModeSchema"] = Passenger::Core::ControllerSingleAppModeSchema().inspect();
	doc["Passenger::Core::Schema"] = Passenger::Core::Schema().inspect();
	doc["Passenger::Core::TelemetryCollector::Schema"] = Passenger::Core::TelemetryCollector::Schema().inspect();
	doc["Passenger::LoggingKit::Schema"] = Passenger::LoggingKit::Schema().inspect();
	doc["Passenger::SecurityUpdateChecker::Schema"] = Passenger::SecurityUpdateChecker::Schema().inspect();
	doc["Passenger::ServerKit::BaseServerSchema"] = Passenger::ServerKit::BaseServerSchema().inspect();
	doc["Passenger::ServerKit::HttpServerSchema"] = Passenger::ServerKit::HttpServerSchema().inspect();
	doc["Passenger::ServerKit::Schema"] = Passenger::ServerKit::Schema().inspect();
	doc["Passenger::Watchdog::ApiServer::Schema"] = Passenger::Watchdog::ApiServer::Schema().inspect();
	doc["Passenger::Watchdog::Schema"] = Passenger::Watchdog::Schema().inspect();
	doc["Passenger::WebSocketCommandReverseServer::Schema"] = Passenger::WebSocketCommandReverseServer::Schema().inspect();

	std::cout << doc.toStyledString() << std::endl;
	return 0;
}

