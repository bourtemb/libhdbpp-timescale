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

#ifndef _PQXX_EXTENSION_HPP
#define _PQXX_EXTENSION_HPP

#include <algorithm>
#include <iostream>
#include <pqxx/pqxx>
#include <pqxx/strconv>
#include <vector>

// why is it OmniORB (via Tango)) and Pqxx define these types in different ways? Perhaps
// its the autotools used to configure them? Either way, we do not use tango, just need its
// types, so undef and allow the Pqxx defines to take precedent
#undef HAVE_UNISTD_H
#undef HAVE_SYS_TYPES_H
#undef HAVE_SYS_TIME_H
#undef HAVE_POLL

#include <tango.h>

namespace pqxx
{
namespace internal
{
    template<>
    struct type_name<uint8_t>
    {
        static constexpr const char *value = "uint8_t";
    };

    template<>
    struct type_name<Tango::DevState>
    {
        static constexpr const char *value = "Tango::DevState";
    };

    template<>
    struct type_name<std::vector<double>>
    {
        static constexpr const char *value = "vector<double>";
    };

    template<>
    struct type_name<std::vector<float>>
    {
        static constexpr const char *value = "vector<float>";
    };

    template<>
    struct type_name<std::vector<int32_t>>
    {
        static constexpr const char *value = "vector<int32_t>";
    };

    template<>
    struct type_name<std::vector<uint32_t>>
    {
        static constexpr const char *value = "vector<uint32_t>";
    };

    template<>
    struct type_name<std::vector<int64_t>>
    {
        static constexpr const char *value = "vector<int64_t>";
    };

    template<>
    struct type_name<std::vector<uint64_t>>
    {
        static constexpr const char *value = "vector<uint64_t>";
    };

    template<>
    struct type_name<std::vector<int16_t>>
    {
        static constexpr const char *value = "vector<int16_t>";
    };

    template<>
    struct type_name<std::vector<uint16_t>>
    {
        static constexpr const char *value = "vector<uint16_t>";
    };

    template<>
    struct type_name<std::vector<uint8_t>>
    {
        static constexpr const char *value = "vector<uint8_t>";
    };

    template<>
    struct type_name<std::vector<bool>>
    {
        static constexpr const char *value = "vector<bool>";
    };

    template<>
    struct type_name<std::vector<std::string>>
    {
        static constexpr const char *value = "vector<std::string>";
    };
} // namespace internal

// this is an extension to the pqxx strconv types to allow our engine to
// handle vectors. This allows us to convert the value from Tango directly
// into a postgres array string ready for storage
template<typename T>
struct string_traits<std::vector<T>>
{
public:
    static constexpr const char *name() noexcept { return internal::type_name<T>::value; }
    static constexpr bool has_null() noexcept { return false; }
    static bool is_null(const std::vector<T> &) { return false; }
    [[noreturn]] static std::vector<T> null() { internal::throw_null_conversion(name()); }

    static void from_string(const char str[], std::vector<T> &value)
    {
        if (str == nullptr)
            internal::throw_null_conversion(name());

        if (str[0] != '{' || str[strlen(str) - 1] != '}')
            throw pqxx::conversion_error("Invalid array format");

        value.clear();

        // not the best solution right now, but we are using this for testing only
        // currently. Copy the str into a std::string so we can work with it more easily.
        std::string in(str + 1, str + (strlen(str) - 1));

        // count commas and add one to deduce elements in the string,
        // note we reduce string size to remove the brace at each end
        auto items = std::count(in.begin(), in.end(), ',');
        value.clear();

        // preallocate all the items in the vector, we can then
        // simply set each in turn
        value.resize(items + 1);

        auto element = 0;
        std::string::size_type comma = 0;

        // loop and copy out each value from between the separators
        while ((comma = in.find_first_not_of(',', comma)) != std::string::npos)
        {
            auto next_comma = in.find_first_of(',', comma);
            string_traits<T>::from_string(in.substr(comma, next_comma - comma).c_str(), value[element++]);
            comma = next_comma;
        }
    }

    static std::string to_string(const std::vector<T> &value)
    {
        if (value.empty())
            return {};

        // simply use the pqxx utilities for this, rather than reinvent the wheel...
        return "{" + separated_list(",", value.begin(), value.end()) + "}";
    }
};

// This specialisation is for string types. Unlike other types the string type requires
// the use of the ARRAY notation and dollar quoting to ensure the strings are stored
// without escape characters.
template<>
struct string_traits<std::vector<std::string>>
{
public:
    static constexpr const char *name() noexcept { return "vector<string>"; }
    static constexpr bool has_null() noexcept { return false; }
    static bool is_null(const std::vector<std::string> &) { return false; }
    [[noreturn]] static std::vector<std::string> null() { internal::throw_null_conversion(name()); }

    static void from_string(const char str[], std::vector<std::string> &value)
    {
        if (str == nullptr)
            internal::throw_null_conversion(name());

        if (str[0] != '{' || str[strlen(str) - 1] != '}')
            throw pqxx::conversion_error("Invalid array format");

        value.clear();

        std::pair<array_parser::juncture, std::string> output;

        // use pqxx array parser features to get each element from the array
        array_parser parser(str);
        output = parser.get_next();

        if (output.first == array_parser::juncture::row_start)
        {
            output = parser.get_next();

            // loop and extract each string in turn
            while (output.first == array_parser::juncture::string_value)
            {
                value.push_back(output.second);
                output = parser.get_next();

                if (output.first == array_parser::juncture::row_end)
                    break;

                if (output.first == array_parser::juncture::done)
                    break;
            }
        }
    }

    static std::string to_string(const std::vector<std::string> &value)
    {
        // This function should not be used, so we do a simple basic conversion
        // for testing only
        return "{" + separated_list(",", value.begin(), value.end()) + "}";
    }
};

// This specialisation is for bool, since it is not a normal container class, but
// rather some kind of alien bitfield. We have to adjust the from_string to take into
// account we can not use container element access
template<>
struct string_traits<std::vector<bool>>
{
public:
    static constexpr const char *name() noexcept { return "std::vector<bool>"; }
    static constexpr bool has_null() noexcept { return false; }
    static bool is_null(const std::vector<bool> &) { return false; }
    [[noreturn]] static std::vector<bool> null() { internal::throw_null_conversion(name()); }

    static void from_string(const char str[], std::vector<bool> &value)
    {
        if (str == nullptr)
            internal::throw_null_conversion(name());

        if (str[0] != '{' || str[strlen(str) - 1] != '}')
            throw pqxx::conversion_error("Invalid array format");

        value.clear();

        // not the best solution right now, but we are using this for
        // testing only. Copy the str into a std::string so we can work
        // with it more easily.
        std::string in(str + 1, str + (strlen(str) - 1));
        std::string::size_type comma = 0;

        // loop and copy out each value from between the separators
        while ((comma = in.find_first_not_of(',', comma)) != std::string::npos)
        {
            auto next_comma = in.find_first_of(',', comma);

            // we can not pass an element of the vector, since vector<bool> is not
            // in fact a container, but some kind of bit field. In this case, we
            // have to create a local variable to read the value into, then push this
            // back onto the vector
            bool field;
            string_traits<bool>::from_string(in.substr(comma, next_comma - comma).c_str(), field);
            value.push_back(field);

            comma = next_comma;
        }
    }

    static std::string to_string(const std::vector<bool> &value)
    {
        if (value.empty())
            return {};

        // simply use the pqxx utilities for this, rather than reinvent the wheel
        return "{" + separated_list(",", value.begin(), value.end()) + "}";
    }
};

// Specialization for unsigned char, which was not included in pqxx,
// this becomes an int16_t in the database
template<>
struct string_traits<uint8_t> : internal::builtin_traits<uint8_t>
{};

// Specialization for Tango::DevState, its stored as an init32_t
template<>
struct string_traits<Tango::DevState> : internal::builtin_traits<Tango::DevState>
{};
} // namespace pqxx
#endif // _PQXX_EXTENSION_HPP
