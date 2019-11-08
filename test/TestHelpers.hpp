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

#ifndef _TEST_HELPERS_HPP
#define _TEST_HELPERS_HPP

#include "AttributeTraits.hpp"
#include "HdbppDefines.hpp"

#include <complex>
#include <experimental/random>
#include <memory>
#include <random>
#include <string>

namespace hdbpp_test
{
namespace psql_conn_test
{
    namespace postgres_db
    {
        // connection strings
        const std::string ConnectionString =
            "user=postgres host=localhost port=5432 dbname=hdb password=password";

        const std::string HdbppConnectionString =
            "user=postgres host=localhost port=5432 dbname=hdb password=password";
    } // namespace postgres_db
} // namespace psql_conn_test

namespace attr_name
{
    // mock test data
    const std::string TestAttrFQDName = "tango://localhost.server.com:10000/test-domain/test-family/test-member/test";

    const std::string TestAttrFQDNameNoTangoQual =
        "localhost.server.com:10000/test-domain/test-family/test-member/test";

    const std::string TestAttrFQDNameNoDomain = "tango://localhost:10000/test-domain/test-family/test-member/test";

    const std::string TestAttrCs = "new_cs";
    const std::string TestAttrFullAttrName = "test-domain/test-family/test-member/test";
    const std::string TestAttrDomain = "test-domain";
    const std::string TestAttrFamily = "test-family";
    const std::string TestAttrMember = "test-member";
    const std::string TestAttrName = "test";

    const std::string TestAttrTangoHost = "localhost.server.com:10000";
    const std::string TestAttrTangoHostWithDomain = "localhost.server.com:10000";

    // represents the name saved into the database, the combination
    // of (tango://)(TestAttrTangoHostWithDomain)(TestAttrFullAttrName)
    const std::string TestAttrFinalName = "tango://" + TestAttrTangoHostWithDomain + "/" + TestAttrFullAttrName;
} // namespace attr_name

namespace attr_info
{
    const std::string AttrInfoDescription =
        "Description about attribute, its \"quoted\",  and 'quoted', yet does it work?";
        
    const std::string AttrInfoLabel = "Label";
    const std::string AttrInfoUnit = "Unit %";
    const std::string AttrInfoStandardUnit = "Standard Unit";
    const std::string AttrInfoDisplayUnit = "Display Unit $";
    const std::string AttrInfoFormat = "Format";
    const std::string AttrInfoRel = "Rel";
    const std::string AttrInfoAbs = "Abs";
    const std::string AttrInfoPeriod = "Period";
} // namespace attr_info

namespace data_gen
{
    template<Tango::CmdArgType Type>
    struct TangoTypeTraits
    {};

    template<>
    struct TangoTypeTraits<Tango::DEV_BOOLEAN>
    {
        using type = bool;
        using array = std::unique_ptr<std::vector<bool>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_SHORT>
    {
        using type = int16_t;
        using array = std::unique_ptr<std::vector<int16_t>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_LONG>
    {
        using type = int32_t;
        using array = std::unique_ptr<std::vector<int32_t>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_LONG64>
    {
        using type = int64_t;
        using array = std::unique_ptr<std::vector<int64_t>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_FLOAT>
    {
        using type = float;
        using array = std::unique_ptr<std::vector<float>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_DOUBLE>
    {
        using type = double;
        using array = std::unique_ptr<std::vector<double>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_UCHAR>
    {
        using type = uint8_t;
        using array = std::unique_ptr<std::vector<uint8_t>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_USHORT>
    {
        using type = uint16_t;
        using array = std::unique_ptr<std::vector<uint16_t>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_ULONG>
    {
        using type = uint32_t;
        using array = std::unique_ptr<std::vector<uint32_t>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_ULONG64>
    {
        using type = uint64_t;
        using array = std::unique_ptr<std::vector<uint64_t>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_STRING>
    {
        using type = std::string;
        using array = std::unique_ptr<std::vector<std::string>>;
    };

    template<>
    struct TangoTypeTraits<Tango::DEV_STATE>
    {
        using type = Tango::DevState;
        using array = std::unique_ptr<std::vector<Tango::DevState>>;
    };

    template<Tango::CmdArgType Type>
    typename TangoTypeTraits<Type>::array data(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_BOOLEAN>::array data<Tango::DEV_BOOLEAN>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_SHORT>::array data<Tango::DEV_SHORT>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_LONG>::array data<Tango::DEV_LONG>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_LONG64>::array data<Tango::DEV_LONG64>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_FLOAT>::array data<Tango::DEV_FLOAT>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_DOUBLE>::array data<Tango::DEV_DOUBLE>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_UCHAR>::array data<Tango::DEV_UCHAR>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_USHORT>::array data<Tango::DEV_USHORT>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_ULONG>::array data<Tango::DEV_ULONG>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_ULONG64>::array data<Tango::DEV_ULONG64>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_STRING>::array data<Tango::DEV_STRING>(int size);

    template<>
    typename TangoTypeTraits<Tango::DEV_STATE>::array data<Tango::DEV_STATE>(int size);

    template<typename T>
    std::unique_ptr<std::vector<T>> genericData(int size)
    {
        std::unique_ptr<std::vector<T>> value = std::make_unique<std::vector<T>>();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<T> d;

        for (int i = 0; i < size; i++)
            value->push_back(d(gen));

        return std::move(value);
    }

    template<Tango::CmdArgType Type>
    typename TangoTypeTraits<Type>::array generateScalarData(bool empty_data = false)
    {
        return std::move(data<Type>(empty_data ? 0 : 1));
    }

    template<Tango::CmdArgType Type>
    typename TangoTypeTraits<Type>::array generateSpectrumData(bool empty_data = false, int size = 1024)
    {
        return std::move(data<Type>(empty_data ? 0 : size));
    }

    template<Tango::CmdArgType Type>
    typename TangoTypeTraits<Type>::array generateData(
        const hdbpp_internal::AttributeTraits &traits, bool empty_data = false)
    {
        if (traits.isArray())
            return generateSpectrumData<Type>(empty_data);

        return generateScalarData<Type>(empty_data);
    }
} // namespace data_gen
} // namespace hdbpp_test
#endif // _TEST_HELPERS_HPP