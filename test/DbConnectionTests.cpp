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

#include "DbConnection.hpp"
#include "HdbppDefines.hpp"
#include "LibUtils.hpp"
#include "QueryBuilder.hpp"
#include "TestHelpers.hpp"
#include "TimescaleSchema.hpp"
#include "catch2/catch.hpp"

#include <cfloat>
#include <pqxx/pqxx>
#include <string>
#include <tuple>
#include <locale>

using namespace std;
using namespace pqxx;
using namespace hdbpp_internal;
using namespace hdbpp_internal::pqxx_conn;
using namespace hdbpp_test::attr_name;
using namespace hdbpp_test::attr_info;
using namespace hdbpp_test::psql_conn_test;
using namespace hdbpp_test::data_gen;

namespace psql_conn_test
{
// define it globally so we can use its cache during tests
QueryBuilder TestQueryBuilder;

void clearTable(pqxx::connection &conn, const string &table_name)
{
    {
        work tx {conn};
        tx.exec("TRUNCATE " + table_name + "  RESTART IDENTITY CASCADE");
        tx.commit();
    }
}

// wrapper to store an attribute
void storeTestAttribute(DbConnection &conn, const AttributeTraits &traits)
{
    REQUIRE_NOTHROW(conn.storeAttribute(
        TestAttrFinalName, TestAttrCs, TestAttrDomain, TestAttrFamily, TestAttrMember, TestAttrName, traits));
}

// wrapper to store some event data, and return the data for comparison
template<Tango::CmdArgType Type>
tuple<vector<typename TangoTypeTraits<Type>::type>, vector<typename TangoTypeTraits<Type>::type>> storeTestEventData(
    DbConnection &conn, const AttributeTraits &traits, int quality = Tango::ATTR_VALID)
{
    struct timeval tv
    {};
    gettimeofday(&tv, nullptr);
    double event_time = tv.tv_sec + tv.tv_usec / 1.0e6;

    auto r = generateData<Type>(traits, !traits.hasReadData());
    auto w = generateData<Type>(traits, !traits.hasWriteData());

    // make a copy for the consistency check
    auto ret = make_tuple((*r), (*w));

    REQUIRE_NOTHROW(conn.storeDataEvent(TestAttrFinalName, event_time, quality, move(r), move(w), traits));

    return ret;
}

// generic compare for most types
template<typename T>
bool compareData(T lhs, T rhs)
{
    // just to help debug
    REQUIRE(lhs == rhs);
    return lhs == rhs;
}

// float needs a specialised compare to ensure its close enough
template<>
bool compareData<float>(float lhs, float rhs)
{
    // Calculate the difference.
    float diff = fabs(lhs - rhs);
    lhs = fabs(lhs);
    rhs = fabs(rhs);

    // Find the largest
    float largest = (rhs > lhs) ? rhs : lhs;
    return (diff <= largest * 0.0001);
}

// double needs a specialised compare to ensure its close enough
template<>
bool compareData<double>(double lhs, double rhs)
{
    // Calculate the difference.
    double diff = fabs(lhs - rhs);
    lhs = fabs(lhs);
    rhs = fabs(rhs);

    // Find the largest
    double largest = (rhs > lhs) ? rhs : lhs;
    return (diff <= largest * 0.0001);
}

template<typename T>
bool compareVector(const vector<T> &lhs, const vector<T> &rhs)
{
    // just to help debug
    REQUIRE(lhs == rhs);
    return lhs == rhs;
}

template<>
bool compareVector<float>(const vector<float> &lhs, const vector<float> &rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (unsigned int i = 0; i < lhs.size(); i++)
        if (!compareData(lhs[i], rhs[i]))
            return false;

    return true;
}

template<>
bool compareVector<double>(const vector<double> &lhs, const vector<double> &rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (unsigned int i = 0; i < lhs.size(); i++)
        if (!compareData(lhs[i], rhs[i]))
            return false;

    return true;
}

// taking the original data as a reference, this function loads the last line of data and compares
// it to the reference data as a test
template<typename T>
void checkStoreTestEventData(
    pqxx::connection &test_conn, const AttributeTraits &traits, const tuple<vector<T>, vector<T>> &data)
{
    pqxx::work tx {test_conn};

    auto data_row(tx.exec1(
        "SELECT * FROM " + TestQueryBuilder.tableName(traits) + " ORDER BY " + DAT_COL_DATA_TIME + " LIMIT 1"));

    auto attr_row(tx.exec1("SELECT * FROM " + CONF_TABLE_NAME));
    tx.commit();

    REQUIRE(data_row.at(DAT_COL_ID).as<int>() == attr_row.at(CONF_COL_ID).as<int>());

    if (traits.isScalar() && traits.hasReadData())
    {
        REQUIRE(data_row.at(DAT_COL_VALUE_R).size() > 0);
        REQUIRE(compareData(data_row.at(DAT_COL_VALUE_R).as<T>(), get<0>(data)[0]) == true);
    }
    else if (traits.isArray() && traits.hasReadData())
    {
        REQUIRE(data_row.at(DAT_COL_VALUE_R).size() > 0);
        REQUIRE(compareVector(data_row.at(DAT_COL_VALUE_R).as<vector<T>>(), get<0>(data)) == true);
    }

    if (traits.isScalar() && traits.hasWriteData())
    {
        REQUIRE(data_row.at(DAT_COL_VALUE_W).size() > 0);
        REQUIRE(compareData(data_row.at(DAT_COL_VALUE_W).as<T>(), get<1>(data)[0]) == true);
    }
    else if (traits.isArray() && traits.hasWriteData())
    {
        REQUIRE(data_row.at(DAT_COL_VALUE_W).size() > 0);
        REQUIRE(compareVector(data_row.at(DAT_COL_VALUE_W).as<vector<T>>(), get<1>(data)) == true);
    }
}
}; // namespace psql_conn_test

SCENARIO("The DbConnection class can open a valid connection to a postgres node",
    "[db-access][hdbpp-db-access][db-connection][psql]")
{
    GIVEN("An unconnected DbConnection object")
    {
        DbConnection conn;

        WHEN("Requesting a connection with a given connect string")
        {
            REQUIRE_NOTHROW(conn.connect(postgres_db::ConnectionString));

            THEN("A connection is opened and reported as open") { REQUIRE(conn.isOpen()); }
            AND_WHEN("The connection is disconnected")
            {
                REQUIRE_NOTHROW(conn.disconnect());

                THEN("The connection reports closed") { REQUIRE(conn.isClosed()); }
            }
        }
    }
}

SCENARIO("The DbConnection class handles a bad connection attempt with an exception",
    "[db-access][hdbpp-db-access][db-connection][psql]")
{
    GIVEN("An unconnected DbConnection object")
    {
        DbConnection conn;

        WHEN("Requesting a connection with an invalid host")
        {
            THEN("A connection_error is thrown")
            {
                REQUIRE_THROWS_AS(conn.connect("user=postgres password=password host=unknown"), Tango::DevFailed);
            }
        }
        WHEN("Requesting a connection with an invalid user")
        {
            THEN("A connection_error is thrown")
            {
                REQUIRE_THROWS_AS(conn.connect("user=invalid password=password host=hdb1"), Tango::DevFailed);
            }
        }
        WHEN("Requesting a connection with an invalid password")
        {
            THEN("A connection_error is thrown")
            {
                REQUIRE_THROWS_AS(conn.connect("user=postgres password=invalid host=hdb1"), Tango::DevFailed);
            }
        }
    }
}

SCENARIO("Storing Attributes in the database", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);
    psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);

    AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};

    GIVEN("A valid DbConnection connected to a hdbpp database")
    {
        WHEN("Storing a test attribute data set to the database")
        {
            psql_conn_test::storeTestAttribute(conn, traits);

            THEN("The data exists in the database, and can be read back and verified")
            {
                {
                    pqxx::work tx {test_conn};
                    auto attr_row(tx.exec1("SELECT * FROM " + CONF_TABLE_NAME));

                    auto type_row(tx.exec1("SELECT " + CONF_TYPE_COL_TYPE_ID + " FROM " + CONF_TYPE_TABLE_NAME +
                        " WHERE " + CONF_TYPE_COL_TYPE_NUM + " = " + std::to_string(traits.type())));

                    auto format_row(tx.exec1("SELECT " + CONF_FORMAT_COL_FORMAT_ID + " FROM " + CONF_FORMAT_TABLE_NAME +
                        " WHERE " + CONF_FORMAT_COL_FORMAT_NUM + " = " + std::to_string(traits.formatType())));

                    auto access_row(tx.exec1("SELECT " + CONF_WRITE_COL_WRITE_ID + " FROM " + CONF_WRITE_TABLE_NAME +
                        " WHERE " + CONF_WRITE_COL_WRITE_NUM + " = " + std::to_string(traits.writeType())));

                    tx.commit();

                    REQUIRE(attr_row.at(CONF_COL_NAME).as<string>() == TestAttrFQDName);
                    REQUIRE(attr_row.at(CONF_COL_CS_NAME).as<string>() == TestAttrCs);
                    REQUIRE(attr_row.at(CONF_COL_DOMAIN).as<string>() == TestAttrDomain);
                    REQUIRE(attr_row.at(CONF_COL_FAMILY).as<string>() == TestAttrFamily);
                    REQUIRE(attr_row.at(CONF_COL_MEMBER).as<string>() == TestAttrMember);
                    REQUIRE(attr_row.at(CONF_COL_LAST_NAME).as<string>() == TestAttrName);
                    REQUIRE(attr_row.at(CONF_COL_TABLE_NAME).as<string>() == QueryBuilder().tableName(traits));
                    REQUIRE(attr_row.at(CONF_COL_TYPE_ID).as<int>() == type_row.at(CONF_TYPE_COL_TYPE_ID).as<int>());

                    REQUIRE(attr_row.at(CONF_COL_FORMAT_TYPE_ID).as<int>() ==
                        format_row.at(CONF_FORMAT_COL_FORMAT_ID).as<int>());

                    REQUIRE(attr_row.at(CONF_COL_WRITE_TYPE_ID).as<int>() ==
                        access_row.at(CONF_WRITE_COL_WRITE_ID).as<int>());
                }
            }
            AND_WHEN("Trying to store the attribute again")
            {
                THEN("An exception is throw as the entry already exists in the database")
                {
                    REQUIRE_THROWS_AS(conn.storeAttribute(TestAttrFinalName,
                                          TestAttrCs,
                                          TestAttrDomain,
                                          TestAttrFamily,
                                          TestAttrMember,
                                          TestAttrName,
                                          traits),
                        Tango::DevFailed);
                }
            }
        }
        WHEN("Storing a test attribute data set to the database in uppercase")
        {
            auto param_to_upper = [](auto param) {
                locale loc;
                string tmp;

                for (string::size_type i = 0; i < param.length(); ++i)
                    tmp += toupper(param[i], loc);

                return tmp;
            };

            conn.storeAttribute(
                param_to_upper(TestAttrFinalName), 
                param_to_upper(TestAttrCs), 
                param_to_upper(TestAttrDomain), 
                param_to_upper(TestAttrFamily), 
                param_to_upper(TestAttrMember), 
                param_to_upper(TestAttrName), traits);

            THEN("The data exists in the database, and can be read back and verified")
            {
                {
                    pqxx::work tx {test_conn};
                    auto attr_row(tx.exec1("SELECT * FROM " + CONF_TABLE_NAME));

                    auto type_row(tx.exec1("SELECT " + CONF_TYPE_COL_TYPE_ID + " FROM " + CONF_TYPE_TABLE_NAME +
                        " WHERE " + CONF_TYPE_COL_TYPE_NUM + " = " + std::to_string(traits.type())));

                    auto format_row(tx.exec1("SELECT " + CONF_FORMAT_COL_FORMAT_ID + " FROM " + CONF_FORMAT_TABLE_NAME +
                        " WHERE " + CONF_FORMAT_COL_FORMAT_NUM + " = " + std::to_string(traits.formatType())));

                    auto access_row(tx.exec1("SELECT " + CONF_WRITE_COL_WRITE_ID + " FROM " + CONF_WRITE_TABLE_NAME +
                        " WHERE " + CONF_WRITE_COL_WRITE_NUM + " = " + std::to_string(traits.writeType())));

                    tx.commit();

                    REQUIRE(attr_row.at(CONF_COL_NAME).as<string>() == param_to_upper(TestAttrFQDName));
                    REQUIRE(attr_row.at(CONF_COL_CS_NAME).as<string>() == param_to_upper(TestAttrCs));
                    REQUIRE(attr_row.at(CONF_COL_DOMAIN).as<string>() == param_to_upper(TestAttrDomain));
                    REQUIRE(attr_row.at(CONF_COL_FAMILY).as<string>() == param_to_upper(TestAttrFamily));
                    REQUIRE(attr_row.at(CONF_COL_MEMBER).as<string>() == param_to_upper(TestAttrMember));
                    REQUIRE(attr_row.at(CONF_COL_LAST_NAME).as<string>() == param_to_upper(TestAttrName));
                    REQUIRE(attr_row.at(CONF_COL_TABLE_NAME).as<string>() == QueryBuilder().tableName(traits));

                    REQUIRE(attr_row.at(CONF_COL_TYPE_ID).as<int>() == type_row.at(CONF_TYPE_COL_TYPE_ID).as<int>());

                    REQUIRE(attr_row.at(CONF_COL_FORMAT_TYPE_ID).as<int>() ==
                        format_row.at(CONF_FORMAT_COL_FORMAT_ID).as<int>());

                    REQUIRE(attr_row.at(CONF_COL_WRITE_TYPE_ID).as<int>() ==
                        access_row.at(CONF_WRITE_COL_WRITE_ID).as<int>());
                }
            }
            AND_WHEN("Trying to store the attribute again")
            {
                THEN("An exception is throw as the entry already exists in the database")
                {
                    REQUIRE_THROWS_AS(            conn.storeAttribute(
                param_to_upper(TestAttrFinalName), 
                param_to_upper(TestAttrCs), 
                param_to_upper(TestAttrDomain), 
                param_to_upper(TestAttrFamily), 
                param_to_upper(TestAttrMember), 
                param_to_upper(TestAttrName), traits),
                        Tango::DevFailed);
                }
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("Storing Attributes in a disconnected state", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);
    psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);

    AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};

    GIVEN("A valid DbConnection connected to a hdbpp database")
    {
        WHEN("Disconnecting from the database and trying to store event")
        {
            conn.disconnect();

            THEN("An exception is throw as the database connection is down")
            {
                REQUIRE_THROWS_AS(conn.storeAttribute(TestAttrFinalName,
                                      TestAttrCs,
                                      TestAttrDomain,
                                      TestAttrFamily,
                                      TestAttrMember,
                                      TestAttrName,
                                      traits),
                    Tango::DevFailed);
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("Storing History Events in the database", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);

    GIVEN("A valid DbConnection connected to a hdbpp database with an attribute stored in it")
    {
        psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_EVENT_TABLE_NAME);

        AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};
        psql_conn_test::storeTestAttribute(conn, traits);

        WHEN("Storing a new history event in the database")
        {
            REQUIRE_NOTHROW(conn.storeHistoryEvent(TestAttrFQDName, events::PauseEvent));

            THEN("Then both the event and history event exists in the database, and can be read back and verified")
            {
                {
                    pqxx::work tx {test_conn};
                    auto event_row(tx.exec1("SELECT * FROM " + HISTORY_EVENT_TABLE_NAME));
                    auto history_row(tx.exec1("SELECT * FROM " + HISTORY_TABLE_NAME));
                    auto attr_row(tx.exec1("SELECT * FROM " + CONF_TABLE_NAME));
                    tx.commit();

                    // check event type
                    REQUIRE(event_row.at(HISTORY_EVENT_COL_EVENT).as<string>() == events::PauseEvent);

                    // check event id matches event table id
                    REQUIRE(event_row.at(HISTORY_EVENT_COL_EVENT_ID).as<int>() ==
                        history_row.at(HISTORY_COL_EVENT_ID).as<int>());

                    // check attribute id match
                    REQUIRE(attr_row.at(CONF_COL_ID).as<int>() == history_row.at(HISTORY_COL_ID).as<int>());
                }
            }
            AND_WHEN("Trying to store a second history event with the same event")
            {
                REQUIRE_NOTHROW(conn.storeHistoryEvent(TestAttrFQDName, events::PauseEvent));

                THEN("A second history event is added to the database")
                {
                    {
                        pqxx::work tx {test_conn};
                        auto event_result(tx.exec1("SELECT * FROM " + HISTORY_EVENT_TABLE_NAME));
                        auto history_row(tx.exec_n(2, "SELECT * FROM " + HISTORY_TABLE_NAME));
                        auto attr_row(tx.exec1("SELECT * FROM " + CONF_TABLE_NAME));
                        tx.commit();

                        REQUIRE(event_result.at(HISTORY_EVENT_COL_EVENT).as<string>() == events::PauseEvent);

                        // check event type
                        for (const auto &row : history_row)
                        {
                            REQUIRE(attr_row.at(CONF_COL_ID).as<int>() == row.at(HISTORY_COL_ID).as<int>());

                            // check event id matches event table id
                            REQUIRE(row.at(HISTORY_COL_EVENT_ID).as<int>() ==
                                event_result.at(HISTORY_COL_EVENT_ID).as<int>());
                        }
                    }
                }
            }
        }
        WHEN("Storing a two different history event in the database in a row")
        {
            vector<string> events {events::StartEvent, events::PauseEvent};

            REQUIRE_NOTHROW(conn.storeHistoryEvent(TestAttrFQDName, events[0]));
            REQUIRE_NOTHROW(conn.storeHistoryEvent(TestAttrFQDName, events[1]));

            THEN("Then both the events exists in the history event table, and can be read back and verified")
            {
                {
                    pqxx::work tx {test_conn};
                    auto result(tx.exec_n(2, "SELECT * FROM " + HISTORY_EVENT_TABLE_NAME));
                    tx.commit();

                    int i = 0;

                    // check event type
                    for (auto row : result)
                        REQUIRE(row.at(HISTORY_EVENT_COL_EVENT).as<string>() == events[i++]);
                }
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("Storing History Events unrelated to any known Attribute", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);

    GIVEN("A valid DbConnection connected to a hdbpp database with no attribute stored in it")
    {
        psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_EVENT_TABLE_NAME);

        WHEN("Storing a new history event in the database")
        {
            THEN("An exception is raised")
            {
                REQUIRE_THROWS_AS(conn.storeHistoryEvent(TestAttrFQDName, events::PauseEvent), Tango::DevFailed);
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("Storing History Events in a disconnected state", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);

    GIVEN("A valid DbConnection connected to a hdbpp database with an attribute stored in it")
    {
        psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_EVENT_TABLE_NAME);

        AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};
        psql_conn_test::storeTestAttribute(conn, traits);

        WHEN("Disconnecting from the database and trying again")
        {
            REQUIRE_NOTHROW(conn.disconnect());

            THEN("An exception is throw as the database connection is down")
            {
                REQUIRE_THROWS_AS(conn.storeHistoryEvent(TestAttrFQDName, events::PauseEvent), Tango::DevFailed);
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("Storing Parameter Events in the database", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    struct timeval tv
    {};
    gettimeofday(&tv, nullptr);
    double event_time = tv.tv_sec + tv.tv_usec / 1.0e6;

    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);

    GIVEN("A valid DbConnection connected to a hdbpp database with an attribute stored in it")
    {
        psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, PARAM_TABLE_NAME);

        AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};
        psql_conn_test::storeTestAttribute(conn, traits);

        WHEN("Storing a new parameter event in the database")
        {
            REQUIRE_NOTHROW(conn.storeParameterEvent(TestAttrFinalName,
                event_time,
                AttrInfoLabel,
                AttrInfoUnit,
                AttrInfoStandardUnit,
                AttrInfoDisplayUnit,
                AttrInfoFormat,
                AttrInfoRel,
                AttrInfoAbs,
                AttrInfoPeriod,
                AttrInfoDescription));

            THEN("The data exists in the database, and can be read back and verified")
            {
                {
                    pqxx::work tx {test_conn};
                    auto attr_row(tx.exec1("SELECT * FROM " + CONF_TABLE_NAME));
                    auto param_row(tx.exec1("SELECT * FROM " + PARAM_TABLE_NAME));
                    tx.commit();

                    // TODO check event time
                    //REQUIRE(param_row.at(PARAM_COL_EV_TIME).as<double>() == event_time);
                    REQUIRE(param_row.at(PARAM_COL_LABEL).as<string>() == AttrInfoLabel);
                    REQUIRE(param_row.at(PARAM_COL_UNIT).as<string>() == AttrInfoUnit);
                    REQUIRE(param_row.at(PARAM_COL_STANDARDUNIT).as<string>() == AttrInfoStandardUnit);
                    REQUIRE(param_row.at(PARAM_COL_DISPLAYUNIT).as<string>() == AttrInfoDisplayUnit);
                    REQUIRE(param_row.at(PARAM_COL_FORMAT).as<string>() == AttrInfoFormat);
                    REQUIRE(param_row.at(PARAM_COL_ARCHIVERELCHANGE).as<string>() == AttrInfoRel);
                    REQUIRE(param_row.at(PARAM_COL_ARCHIVEABSCHANGE).as<string>() == AttrInfoAbs);
                    REQUIRE(param_row.at(PARAM_COL_ARCHIVEPERIOD).as<string>() == AttrInfoPeriod);
                    REQUIRE(param_row.at(PARAM_COL_DESCRIPTION).as<string>() == AttrInfoDescription);

                    // check attribute id match
                    REQUIRE(attr_row.at(CONF_COL_ID).as<int>() == param_row.at(PARAM_COL_ID).as<int>());
                }
            }
            AND_WHEN("Trying to store another parameter event for the same attribute")
            {
                REQUIRE_NOTHROW(conn.storeParameterEvent(TestAttrFinalName,
                    event_time,
                    AttrInfoLabel,
                    AttrInfoUnit,
                    AttrInfoStandardUnit,
                    AttrInfoDisplayUnit,
                    AttrInfoFormat,
                    AttrInfoRel,
                    AttrInfoAbs,
                    AttrInfoPeriod,
                    AttrInfoDescription));

                THEN("A second parameter event is added to the database")
                {
                    {
                        pqxx::work tx {test_conn};
                        auto result(tx.exec_n(2, "SELECT * FROM " + PARAM_TABLE_NAME));
                        tx.commit();

                        REQUIRE(result.size() == 2);
                    }
                }
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("Storing Parameter Events in a disconnected state", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    struct timeval tv
    {};
    gettimeofday(&tv, nullptr);
    double event_time = tv.tv_sec + tv.tv_usec / 1.0e6;

    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);

    GIVEN("A valid DbConnection connected to a hdbpp database with an attribute stored in it")
    {
        psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_EVENT_TABLE_NAME);

        AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};
        psql_conn_test::storeTestAttribute(conn, traits);

        WHEN("Disconnecting from the database and trying again")
        {
            conn.disconnect();

            THEN("An exception is throw as the database connection is down")
            {
                REQUIRE_THROWS_AS(conn.storeParameterEvent(TestAttrFinalName,
                                      event_time,
                                      AttrInfoLabel,
                                      AttrInfoUnit,
                                      AttrInfoStandardUnit,
                                      AttrInfoDisplayUnit,
                                      AttrInfoFormat,
                                      AttrInfoRel,
                                      AttrInfoAbs,
                                      AttrInfoPeriod,
                                      AttrInfoDescription),
                    Tango::DevFailed);
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("Storing event data which is invalid", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    struct timeval tv
    {};
    gettimeofday(&tv, nullptr);
    double event_time = tv.tv_sec + tv.tv_usec / 1.0e6;

    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);

    GIVEN("A valid DbConnection connected to a hdbpp database with an attribute stored in it")
    {
        psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);

        WHEN("Storing a read only scalar data event with no data")
        {
            AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};
            psql_conn_test::storeTestAttribute(conn, traits);

            REQUIRE_NOTHROW(conn.storeDataEvent(TestAttrFinalName,
                event_time,
                Tango::ATTR_VALID,
                move(make_unique<std::vector<double>>()),
                move(make_unique<std::vector<double>>()),
                traits));

            THEN("The event is stored, with no data, and can be read back")
            {
                {
                    pqxx::work tx {test_conn};
                    auto data_row(tx.exec1("SELECT * FROM " + psql_conn_test::TestQueryBuilder.tableName(traits) +
                        " ORDER BY " + DAT_COL_DATA_TIME + " LIMIT 1"));
                    auto attr_row(tx.exec1("SELECT * FROM " + CONF_TABLE_NAME));
                    tx.commit();

                    REQUIRE(data_row.at(DAT_COL_ID).as<int>() == attr_row.at(CONF_COL_ID).as<int>());
                    REQUIRE(data_row.at(DAT_COL_VALUE_R).is_null() == true);
                }
            }
        }
        WHEN("Storing a read/write spectrum data event with no data")
        {
            AttributeTraits traits {Tango::READ_WRITE, Tango::SPECTRUM, Tango::DEV_DOUBLE};
            psql_conn_test::storeTestAttribute(conn, traits);

            REQUIRE_NOTHROW(conn.storeDataEvent(TestAttrFinalName,
                event_time,
                Tango::ATTR_VALID,
                move(make_unique<std::vector<double>>()),
                move(make_unique<std::vector<double>>()),
                traits));

            THEN("The event is stored, with no data, and can be read back")
            {
                {
                    pqxx::work tx {test_conn};
                    auto data_row(tx.exec1("SELECT * FROM " + psql_conn_test::TestQueryBuilder.tableName(traits) +
                        " ORDER BY " + DAT_COL_DATA_TIME + " LIMIT 1"));
                    auto attr_row(tx.exec1("SELECT * FROM " + CONF_TABLE_NAME));
                    tx.commit();

                    REQUIRE(data_row.at(DAT_COL_ID).as<int>() == attr_row.at(CONF_COL_ID).as<int>());
                    REQUIRE(data_row.at(DAT_COL_VALUE_R).is_null() == true);
                    REQUIRE(data_row.at(DAT_COL_VALUE_W).is_null() == true);
                }
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

TEST_CASE("Storing event data of all Tango type combinations in the database",
    "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);
    psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);

    vector<Tango::CmdArgType> types {
        Tango::DEV_BOOLEAN,
        Tango::DEV_DOUBLE,
        Tango::DEV_FLOAT,
        Tango::DEV_STRING,
        Tango::DEV_LONG,
        Tango::DEV_ULONG,
        Tango::DEV_LONG64,
        Tango::DEV_ULONG64,
        Tango::DEV_SHORT,
        Tango::DEV_USHORT,
        Tango::DEV_UCHAR,
        Tango::DEV_STATE,
        //Tango::DEV_ENCODED, Tango::DEV_ENUM
    };

    vector<Tango::AttrWriteType> write_types {Tango::READ, Tango::WRITE, Tango::READ_WRITE, Tango::READ_WITH_WRITE};
    vector<Tango::AttrDataFormat> format_types {Tango::SCALAR, Tango::SPECTRUM};

    // loop for every combination of type in Tango
    for (auto &type : types)
    {
        for (auto &format : format_types)
        {
            for (auto &write : write_types)
            {
                AttributeTraits traits {write, format, type};

                DYNAMIC_SECTION("Storing a traits: " << traits)
                {
                    psql_conn_test::storeTestAttribute(conn, traits);

                    switch (traits.type())
                    {
                        case Tango::DEV_BOOLEAN:
                            psql_conn_test::checkStoreTestEventData(test_conn,
                                traits,
                                psql_conn_test::storeTestEventData<Tango::DEV_BOOLEAN>(conn, traits));

                            break;

                        case Tango::DEV_SHORT:
                            psql_conn_test::checkStoreTestEventData(
                                test_conn, traits, psql_conn_test::storeTestEventData<Tango::DEV_SHORT>(conn, traits));

                            break;

                        case Tango::DEV_LONG:
                            psql_conn_test::checkStoreTestEventData(
                                test_conn, traits, psql_conn_test::storeTestEventData<Tango::DEV_LONG>(conn, traits));
                            break;

                        case Tango::DEV_LONG64:
                            psql_conn_test::checkStoreTestEventData(
                                test_conn, traits, psql_conn_test::storeTestEventData<Tango::DEV_LONG64>(conn, traits));

                            break;

                        case Tango::DEV_FLOAT:
                            psql_conn_test::checkStoreTestEventData(
                                test_conn, traits, psql_conn_test::storeTestEventData<Tango::DEV_FLOAT>(conn, traits));

                            break;

                        case Tango::DEV_DOUBLE:
                            psql_conn_test::checkStoreTestEventData(
                                test_conn, traits, psql_conn_test::storeTestEventData<Tango::DEV_DOUBLE>(conn, traits));

                            break;

                        case Tango::DEV_UCHAR:
                            psql_conn_test::checkStoreTestEventData(
                                test_conn, traits, psql_conn_test::storeTestEventData<Tango::DEV_UCHAR>(conn, traits));

                            break;

                        case Tango::DEV_USHORT:
                            psql_conn_test::checkStoreTestEventData(
                                test_conn, traits, psql_conn_test::storeTestEventData<Tango::DEV_USHORT>(conn, traits));

                            break;

                        case Tango::DEV_ULONG:
                            psql_conn_test::checkStoreTestEventData(
                                test_conn, traits, psql_conn_test::storeTestEventData<Tango::DEV_ULONG>(conn, traits));

                            break;

                        case Tango::DEV_ULONG64:
                            psql_conn_test::checkStoreTestEventData(test_conn,
                                traits,
                                psql_conn_test::storeTestEventData<Tango::DEV_ULONG64>(conn, traits));

                            break;

                        case Tango::DEV_STRING:
                            psql_conn_test::checkStoreTestEventData(
                                test_conn, traits, psql_conn_test::storeTestEventData<Tango::DEV_STRING>(conn, traits));

                            break;

                        case Tango::DEV_STATE:
                            psql_conn_test::checkStoreTestEventData(
                                test_conn, traits, psql_conn_test::storeTestEventData<Tango::DEV_STATE>(conn, traits));

                            break;

                            //case Tango::DEV_ENUM:
                            //psql_conn_test::checkStoreTestEventData(
                            //test_conn, traits, psql_conn_test::storeTestEventData(conn, traits));

                            //break;

                            //case Tango::DEV_ENCODED:
                            //psql_conn_test::checkStoreTestEventData(
                            //test_conn, traits, psql_conn_test::storeTestEventData<hdbpp_encoded_t>(conn, traits));

                            //break;

                        default: throw "Should not be here!";
                    }
                }
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("Storing data events in a disconnected state", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    struct timeval tv
    {};
    gettimeofday(&tv, nullptr);
    double event_time = tv.tv_sec + tv.tv_usec / 1.0e6;

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);

    GIVEN("A valid DbConnection connected to a hdbpp database with an attribute stored in it")
    {
        psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);
        AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};
        psql_conn_test::storeTestAttribute(conn, traits);

        WHEN("Disconnecting from the database and trying again")
        {
            REQUIRE_NOTHROW(conn.disconnect());

            THEN("An exception is throw as the database connection is down")
            {
                REQUIRE_THROWS_AS(conn.storeDataEvent(TestAttrFinalName,
                                      event_time,
                                      Tango::ATTR_VALID,
                                      move(make_unique<std::vector<double>>()),
                                      move(make_unique<std::vector<double>>()),
                                      traits),
                    Tango::DevFailed);
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("Storing data events as errors", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    string error_msg = "A Test Error, 'Message'";

    struct timeval tv
    {};
    gettimeofday(&tv, nullptr);
    double event_time = tv.tv_sec + tv.tv_usec / 1.0e6;

    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);

    GIVEN("A valid DbConnection connected to a hdbpp database with an attribute stored in it")
    {
        psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, ERR_TABLE_NAME);

        AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};
        psql_conn_test::storeTestAttribute(conn, traits);

        WHEN("Storing a new error message in the database")
        {
            REQUIRE_NOTHROW(
                conn.storeDataEventError(TestAttrFinalName, event_time, Tango::ATTR_VALID, error_msg, traits));

            THEN("Then both the event and history event exists in the database, and can be read back and verified")
            {
                {
                    pqxx::work tx {test_conn};

                    auto data_row(tx.exec1("SELECT * FROM " + psql_conn_test::TestQueryBuilder.tableName(traits) +
                        " ORDER BY " + DAT_COL_DATA_TIME + " LIMIT 1"));

                    auto attr_row(tx.exec1("SELECT * FROM " + CONF_TABLE_NAME));
                    auto error_row(tx.exec1("SELECT * FROM " + ERR_TABLE_NAME));
                    tx.commit();

                    REQUIRE(data_row.at(DAT_COL_ID).as<int>() == attr_row.at(CONF_COL_ID).as<int>());
                    REQUIRE(data_row.at(DAT_COL_ERROR_DESC_ID).as<int>() == error_row.at(ERR_COL_ID).as<int>());

                    REQUIRE(error_row.at(ERR_COL_ERROR_DESC).as<string>() == error_msg);
                }
            }
            AND_WHEN("A second error is stored with the same message")
            {
                gettimeofday(&tv, nullptr);
                event_time = tv.tv_sec + tv.tv_usec / 1.0e6;

                REQUIRE_NOTHROW(
                    conn.storeDataEventError(TestAttrFinalName, event_time, Tango::ATTR_VALID, error_msg, traits));

                THEN("The same error id is used in the event data")
                {
                    {
                        pqxx::work tx {test_conn};

                        auto data_row(tx.exec1("SELECT * FROM " + psql_conn_test::TestQueryBuilder.tableName(traits) +
                            " ORDER BY " + DAT_COL_DATA_TIME + " LIMIT 1"));

                        auto attr_row(tx.exec1("SELECT * FROM " + CONF_TABLE_NAME));
                        auto error_row(tx.exec1("SELECT * FROM " + ERR_TABLE_NAME));
                        tx.commit();

                        REQUIRE(data_row.at(DAT_COL_ID).as<int>() == attr_row.at(CONF_COL_ID).as<int>());
                        REQUIRE(data_row.at(DAT_COL_ERROR_DESC_ID).as<int>() == error_row.at(ERR_COL_ID).as<int>());

                        REQUIRE(error_row.at(ERR_COL_ERROR_DESC).as<string>() == error_msg);
                    }
                }
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("Fetching the last event after it has just been stored", "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);

    GIVEN("A valid DbConnection connected to a hdbpp database with an attribute and history event stored in it")
    {
        psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_EVENT_TABLE_NAME);

        AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};
        psql_conn_test::storeTestAttribute(conn, traits);

        REQUIRE_NOTHROW(conn.storeHistoryEvent(TestAttrFQDName, events::PauseEvent));

        WHEN("Fetching the last history event for the attribute")
        {
            string event;
            REQUIRE_NOTHROW(event = conn.fetchLastHistoryEvent(TestAttrFQDName));

            THEN("It is equal to the event just stored") { REQUIRE(event == events::PauseEvent); }
            AND_WHEN("Storing a second event and fetching it")
            {
                REQUIRE_NOTHROW(conn.storeHistoryEvent(TestAttrFQDName, events::StartEvent));

                string event;
                REQUIRE_NOTHROW(event = conn.fetchLastHistoryEvent(TestAttrFQDName));

                THEN("It is equal to the event just stored") { REQUIRE(event == events::StartEvent); }
            }
        }
    }

    if (conn.isOpen())
        REQUIRE_NOTHROW(conn.disconnect());

    if (test_conn.is_open())
        test_conn.disconnect();
}

SCENARIO("When no events have been stored, no error is thrown requesting the last event",
    "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);

    GIVEN("A valid DbConnection connected to a hdbpp database with no attribute nor history event stored in it")
    {
        psql_conn_test::clearTable(test_conn, HISTORY_TABLE_NAME);
        psql_conn_test::clearTable(test_conn, HISTORY_EVENT_TABLE_NAME);

        WHEN("Requesting the last event")
        {
            string event;

            THEN("No error occurs, and no event is returned")
            {
                REQUIRE_NOTHROW(event = conn.fetchLastHistoryEvent(TestAttrFQDName));
                REQUIRE(event.empty());
            }
        }
    }
}

SCENARIO("The archive of an attribute can be determined by fetchAttributeArchived()",
    "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);
    psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);

    GIVEN("A valid DbConnection connected to a hdbpp database with no attribute in it")
    {
        WHEN("Requesting the archive state of the test attribute")
        {
            THEN("The archive state is false") { REQUIRE(!conn.fetchAttributeArchived(TestAttrFQDName)); }
        }
        WHEN("Storing the test attribute and checking its archive state")
        {
            AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};
            psql_conn_test::storeTestAttribute(conn, traits);

            THEN("The archive state is true") { REQUIRE(conn.fetchAttributeArchived(TestAttrFQDName)); }
        }
    }
}

SCENARIO("The type traits of an archived attribute can be returned by fetchAttributeTraits()",
    "[db-access][hdbpp-db-access][db-connection][psql]")
{
    DbConnection conn;
    REQUIRE_NOTHROW(conn.connect(postgres_db::HdbppConnectionString));

    // used for verification
    pqxx::connection test_conn(postgres_db::HdbppConnectionString);
    psql_conn_test::clearTable(test_conn, CONF_TABLE_NAME);

    GIVEN("A valid DbConnection connected to a hdbpp database with a attribute in it")
    {
        WHEN("Requesting the attribute type traits state of the test attribute")
        {
            THEN("An exception is thrown") { REQUIRE_THROWS(conn.fetchAttributeTraits(TestAttrFQDName)); }
        }
        WHEN("Storing the test attribute and checking its type traits")
        {
            AttributeTraits traits {Tango::READ, Tango::SCALAR, Tango::DEV_DOUBLE};
            psql_conn_test::storeTestAttribute(conn, traits);

            THEN("The returned traits match those it was stored with")
            {
                REQUIRE(conn.fetchAttributeTraits(TestAttrFQDName) == traits);
            }
        }
    }
}