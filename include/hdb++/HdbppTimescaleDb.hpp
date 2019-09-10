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

#ifndef _HDBPP_TIMESCALE_HPP
#define _HDBPP_TIMESCALE_HPP

#include <hdb++/AbstractDB.h>
#include <string>
#include <tango.h>
#include <vector>

namespace hdbpp
{
class HdbppTimescaleDb : public AbstractDB
{
public:
    /**
     * @brief HdbppTimescaleDb constructor
     *
     * The configuration parameters must contain the following strings:
     *
     * @param configuration A list of configuration parameters to start the driver with.
     */
    HdbppTimescaleDb(const std::vector<std::string> &configuration);

    /**
     * @brief Destroy the HdbppTimescaleDb library object
     */
    virtual ~HdbppTimescaleDb();

    /**
     * @brief Insert an attribute archive event into the database
     *
     * Inserts an attribute archive event for the EventData into the database. If the attribute
     * does not exist in the database, then an exception will be raised. If the attr_value
     * field of the data parameter if empty, then the attribute is in an error state
     * and the error message will be archived.
     *
     * @param event_data Tango event data about the attribute.
     * @param event_data_type HDB event data for the attribute.
     * @throw Tango::DevFailed
     */
    virtual void insert_Attr(Tango::EventData *event_data, HdbEventDataType event_data_type);

    /**
     * @brief Inserts the attribute configuration data.
     *
     * Inserts the attribute configuration data (Tango Attribute Configuration event data)
     * into the database. The attribute must be configured to be stored in HDB++,
     * otherwise an exception will be thrown.
     *
     * @param conf_event_data Tango event data about the attribute.
     * @param event_data_type HDB event data for the attribute.
     * @throw Tango::DevFailed
     */
    virtual void insert_param_Attr(Tango::AttrConfEventData *conf_event_data, HdbEventDataType /* event_data_type */);

    /**
     * @brief Add and configure an attribute in the database.
     *
     * Trying to reconfigure an existing attribute will result in an exception, and if an
     * attribute already exists with the same configuration then the ttl will be updated if
     * different.
     *
     * @param fqdn_attr_name Fully qualified attribute name
     * @param type The type of the attribute.
     * @param format The format of the attribute.
     * @param write_type The read/write access of the type.
     * @param  ttl The time to live in hour, 0 for infinity
     * @throw Tango::DevFailed
     */
    virtual void configure_Attr(
        std::string fqdn_attr_name, int type, int format, int write_type, unsigned int /* ttl */);

    /**
     * @brief Update the ttl value for an attribute.
     *
     * The attribute must have been configured to be stored in HDB++, otherwise an exception
     * is raised
     *
     * @param fqdn_attr_name Fully qualified attribute nam
     * @param ttl The time to live in hour, 0 for infinity
     * @throw Tango::DevFailed
     */
    virtual void updateTTL_Attr(std::string fqdn_attr_name, unsigned int ttl);

    /**
    * @brief Record a start, Stop, Pause or Remove history event for an attribute.
    *
    * Inserts a history event for the attribute name passed to the function. The attribute
    * must have been configured to be stored in HDB++, otherwise an exception is raised.
    * This function will also insert an additional CRASH history event before the START
    * history event if the given event parameter is DB_START and if the last history event
    * stored was also a START event.
    *
    * @param fqdn_attr_name Fully qualified attribute name
    * @param event
    * @throw Tango::DevFailed
    */
    virtual void event_Attr(std::string fqdn_attr_name, unsigned char event);
};

class HdbppTimescaleDbFactory : public DBFactory
{
public:
    virtual AbstractDB *create_db(std::vector<std::string> configuration);
};

} // namespace hdbpp
#endif // _HDBPP_TIMESCALE_HPP
