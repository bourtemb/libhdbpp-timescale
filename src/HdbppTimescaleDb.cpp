/* Copyright (C) : 2014-2019
   European Synchrotron Radiation Facility
   BP 220, Grenoble 38043, FRANCE

   This file is part of libhdb++timescale.

   libhdb++timescale is free software: you can redistribute it and/or modify
   it under the terms of the Lesser GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   libhdb++timescale is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser
   GNU General Public License for more details.

   You should have received a copy of the Lesser GNU General Public License
   along with libhdb++timescale.  If not, see <http://www.gnu.org/licenses/>. */

#include "hdb++/HdbppTimescaleDb.hpp"

#include "DbConnection.hpp"
#include "HdbppTxDataEvent.hpp"
#include "HdbppTxDataEventError.hpp"
#include "HdbppTxHistoryEvent.hpp"
#include "HdbppTxNewAttribute.hpp"
#include "HdbppTxParameterEvent.hpp"
#include "LibUtils.hpp"

#include <locale>
#include <memory>
#include <vector>

using namespace std;
using namespace hdbpp_internal;

namespace hdbpp
{
// declaring this variable here removes it from the header, and keeps the header clean.
// It is allocated in the constructor. It can be abstracted further to allow easy plug
// in of different backends at a later point
unique_ptr<pqxx_conn::DbConnection> Conn;

// simple class to gather utility functions that were previously part of HdbppTimescaleDb,
// removes them from the header and keeps it clean for includes
struct HdbppTimescaleDbUtils
{
    static string getConfigParam(const map<string, string> &conf, const string &param, bool mandatory);
    static map<string, string> extractConfig(vector<string> config, const string &separator);
};

//=============================================================================
//=============================================================================
map<string, string> HdbppTimescaleDbUtils::extractConfig(vector<string> config, const string &separator)
{
    map<string, string> results;

    for (auto &item : config)
    {
        auto found_separator = item.find_first_of(separator);

        if (found_separator != string::npos && found_separator > 0)
            results.insert(make_pair(item.substr(0, found_separator), item.substr(found_separator + 1)));
    }

    return results;
}

//=============================================================================
//=============================================================================
string HdbppTimescaleDbUtils::getConfigParam(const map<string, string> &conf, const string &param, bool mandatory)
{
    auto iter = conf.find(param);

    if (iter == conf.end() && mandatory)
    {
        std::string msg {"Configuration parsing error: mandatory configuration parameter: " + param + " not found"};
        Tango::Except::throw_exception("Invalid Argument", msg, LOCATION_INFO);
    }

    // for non-mandatory config params that have not been set, just return
    // an empty string
    return iter == conf.end() ? "" : (*iter).second;
}

//=============================================================================
//=============================================================================
HdbppTimescaleDb::HdbppTimescaleDb(const vector<string> &configuration)
{
    auto param_to_lower = [](auto param) {
        locale loc;
        string tmp;

        for (string::size_type i = 0; i < param.length(); ++i)
            tmp += tolower(param[i], loc);

        return tmp;
    };

    // convert the config vector to a map
    auto libhdb_conf = HdbppTimescaleDbUtils::extractConfig(configuration, "=");

    // logging_level optional config parameter ----
    auto level = param_to_lower(HdbppTimescaleDbUtils::getConfigParam(libhdb_conf, "logging_level", false));
    auto log_file = HdbppTimescaleDbUtils::getConfigParam(libhdb_conf, "log_file", false);
    auto log_console = HdbppTimescaleDbUtils::getConfigParam(libhdb_conf, "log_console", false);
    auto log_file_name = HdbppTimescaleDbUtils::getConfigParam(libhdb_conf, "log_file_name", false);

    LogConfigurator::initLogging(
        param_to_lower(log_file) == "true", param_to_lower(log_console) == "true", log_file_name);

    if (level == "error" || level.empty())
        LogConfigurator::setLoggingLevel(spdlog::level::level_enum::err);
    else if (level == "warning")
        LogConfigurator::setLoggingLevel(spdlog::level::level_enum::warn);
    else if (level == "info")
        LogConfigurator::setLoggingLevel(spdlog::level::level_enum::info);
    else if (level == "debug")
        LogConfigurator::setLoggingLevel(spdlog::level::level_enum::debug);
    else if (level == "trace")
        LogConfigurator::setLoggingLevel(spdlog::level::level_enum::trace);
    else if (level == "disabled")
        LogConfigurator::setLoggingLevel(spdlog::level::level_enum::off);
    else
        LogConfigurator::setLoggingLevel(spdlog::level::level_enum::err);

    spdlog::info("Logging level: {}", level);
    spdlog::info("Logging to file: {}, logging to console: {}", log_file, log_console);
    spdlog::info("Logfile (if any): {}", log_file_name);

    spdlog::info("Starting libhdbpp-timescale shared library...");

    // connect_string mandatory config parameter ----
    auto connection_string = HdbppTimescaleDbUtils::getConfigParam(libhdb_conf, "connect_string", true);
    spdlog::info("Mandatory config parameter connect_string: {}", connection_string);

    // allocate a connection to store data with
    Conn = make_unique<pqxx_conn::DbConnection>();

    // now bring up the connection
    Conn->connect(connection_string);
    spdlog::info("Started libhdbpp-timescale shared library successfully");
}

//=============================================================================
//=============================================================================
HdbppTimescaleDb::~HdbppTimescaleDb()
{
    if (Conn->isOpen())
        Conn->disconnect();

    LogConfigurator::shutdownLogging();
}

//=============================================================================
//=============================================================================
void HdbppTimescaleDb::insert_Attr(Tango::EventData *event_data, HdbEventDataType event_data_type)
{
    assert(event_data);
    assert(event_data->attr_value);
    spdlog::trace("Insert data event for attribute: {}", event_data->attr_name);

    // if there is an error, we store an error, since there will be no data passed in
    if (event_data->err)
    {
        spdlog::trace("Event type is error for attribute: {}", event_data->attr_name);

        // no time data is passed for errors, so make something up
        struct timeval tv
        {};

        struct Tango::TimeVal tango_tv
        {};

        gettimeofday(&tv, nullptr);
        tango_tv.tv_sec = tv.tv_sec;
        tango_tv.tv_usec = tv.tv_usec;
        tango_tv.tv_nsec = 0;

        Conn->createTx<HdbppTxDataEventError>()
            .withName(event_data->attr_name)
            .withTraits(static_cast<Tango::AttrWriteType>(event_data_type.write_type),
                static_cast<Tango::AttrDataFormat>(event_data_type.data_format),
                static_cast<Tango::CmdArgType>(event_data_type.data_type))
            .withError(string(event_data->errors[0].desc))
            .withEventTime(tango_tv)
            .withQuality(event_data->attr_value->get_quality())
            .store();
    }
    else
    {
        spdlog::trace("Event type is data for attribute: {}", event_data->attr_name);

        // build a data event request, this will store 0 or more data elements,
        // pending on type, format and quality
        Conn->createTx<HdbppTxDataEvent>()
            .withName(event_data->attr_name)
            .withTraits(static_cast<Tango::AttrWriteType>(event_data_type.write_type),
                static_cast<Tango::AttrDataFormat>(event_data_type.data_format),
                static_cast<Tango::CmdArgType>(event_data_type.data_type))
            .withAttribute(event_data->attr_value)
            .withEventTime(event_data->attr_value->get_date())
            .withQuality(event_data->attr_value->get_quality())
            .store();
    }
}

//=============================================================================
//=============================================================================
void HdbppTimescaleDb::insert_param_Attr(
    Tango::AttrConfEventData *conf_event_data, HdbEventDataType /* event_data_type */)
{
    assert(conf_event_data);
    spdlog::trace("Insert parameter event request for attribute: {}", conf_event_data->attr_name);

    Conn->createTx<HdbppTxParameterEvent>()
        .withName(conf_event_data->attr_name)
        .withEventTime(conf_event_data->get_date())
        .withAttrInfo(*(conf_event_data->attr_conf))
        .store();
}

//=============================================================================
//=============================================================================
void HdbppTimescaleDb::configure_Attr(
    std::string fqdn_attr_name, int type, int format, int write_type, unsigned int /* ttl */)
{
    assert(!fqdn_attr_name.empty());
    spdlog::trace("Insert new attribute request for attribute: {}", fqdn_attr_name);

    // forgive the ugly casting, but for some reason we receive the enum values
    // already cast to ints, we cast them back to enums so they function as
    // enums again
    Conn->createTx<HdbppTxNewAttribute>()
        .withName(fqdn_attr_name)
        .withTraits(static_cast<Tango::AttrWriteType>(write_type),
            static_cast<Tango::AttrDataFormat>(format),
            static_cast<Tango::CmdArgType>(type))
        .store();
}

//=============================================================================
//=============================================================================
void HdbppTimescaleDb::updateTTL_Attr(std::string fqdn_attr_name, unsigned int ttl)
{
    assert(!fqdn_attr_name.empty());
    spdlog::trace("TTL event request for attribute: {}, with ttl: {}", fqdn_attr_name, ttl);

    // TODO implement
}

//=============================================================================
//=============================================================================
void HdbppTimescaleDb::event_Attr(std::string fqdn_attr_name, unsigned char event)
{
    assert(!fqdn_attr_name.empty());
    spdlog::trace("History event request for attribute: {}", fqdn_attr_name);
    Conn->createTx<HdbppTxHistoryEvent>().withName(fqdn_attr_name).withEvent(event).store();
}

//=============================================================================
//=============================================================================
AbstractDB *HdbppTimescaleDbFactory::create_db(vector<string> configuration)
{
    return new HdbppTimescaleDb(configuration);
}
} // namespace hdbpp

//=============================================================================
//=============================================================================
hdbpp::DBFactory *getDBFactory()
{
    auto *factory = new hdbpp::HdbppTimescaleDbFactory();
    return static_cast<hdbpp::DBFactory*>(factory);
}
