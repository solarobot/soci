//
// Copyright (C) 2011-2013 Denis Chapligin
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#define SOCI_DB2_SOURCE
#include "soci/soci-platform.h"
#include "soci/db2/soci-db2.h"
#include "soci-exchange-cast.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

using namespace soci;
using namespace soci::details;

void *db2_standard_use_type_backend::prepare_for_bind(
    SQLLEN &size, SQLSMALLINT &sqlType, SQLSMALLINT &cType)
{
    switch (type)
    {
    // simple cases
    case x_int8:
        sqlType = SQL_SMALLINT;
        cType = SQL_C_STINYINT;
        size = sizeof(int8_t);
        break;
    case x_uint8:
        sqlType = SQL_SMALLINT;
        cType = SQL_C_UTINYINT;
        size = sizeof(uint8_t);
        break;
    case x_int16:
        sqlType = SQL_SMALLINT;
        cType = SQL_C_SSHORT;
        size = sizeof(int16_t);
        break;
    case x_uint16:
        sqlType = SQL_SMALLINT;
        cType = SQL_C_USHORT;
        size = sizeof(uint16_t);
        break;
    case x_int32:
        sqlType = SQL_INTEGER;
        cType = SQL_C_SLONG;
        size = sizeof(int32_t);
        break;
    case x_uint32:
        sqlType = SQL_INTEGER;
        cType = SQL_C_ULONG;
        size = sizeof(uint32_t);
        break;
    case x_int64:
        sqlType = SQL_BIGINT;
        cType = SQL_C_SBIGINT;
        size = sizeof(int64_t);
        break;
    case x_uint64:
        sqlType = SQL_BIGINT;
        cType = SQL_C_UBIGINT;
        size = sizeof(uint64_t);
        break;
    case x_double:
        sqlType = SQL_DOUBLE;
        cType = SQL_C_DOUBLE;
        size = sizeof(double);
        break;

    // cases that require adjustments and buffer management
    case x_char:
        {
            sqlType = SQL_CHAR;
            cType = SQL_C_CHAR;
            size = sizeof(char) + 1;
            buf = new char[size];
            buf[0] = exchange_type_cast<x_char>(data);
            buf[1] = '\0';
            ind = SQL_NTS;
        }
        break;
    case x_stdstring:
    {
        // TODO: No textual value is assigned here!

        std::string const& s = exchange_type_cast<x_stdstring>(data);
        sqlType = SQL_LONGVARCHAR;
        cType = SQL_C_CHAR;
        size = static_cast<SQLINTEGER>(s.size()) + 1;
        buf = new char[size];
        strncpy(buf, s.c_str(), size);
        ind = SQL_NTS;
    }
    break;
    case x_stdtm:
        {
            sqlType = SQL_TIMESTAMP;
            cType = SQL_C_TIMESTAMP;
            buf = new char[sizeof(TIMESTAMP_STRUCT)];
            std::tm const& t = exchange_type_cast<x_stdtm>(data);
            data = buf;
            size = 19; // This number is not the size in bytes, but the number
                       // of characters in the date if it was written out
                       // yyyy-mm-dd hh:mm:ss

            TIMESTAMP_STRUCT * ts = reinterpret_cast<TIMESTAMP_STRUCT*>(buf);

            ts->year = static_cast<SQLSMALLINT>(t.tm_year + 1900);
            ts->month = static_cast<SQLUSMALLINT>(t.tm_mon + 1);
            ts->day = static_cast<SQLUSMALLINT>(t.tm_mday);
            ts->hour = static_cast<SQLUSMALLINT>(t.tm_hour);
            ts->minute = static_cast<SQLUSMALLINT>(t.tm_min);
            ts->second = static_cast<SQLUSMALLINT>(t.tm_sec);
            ts->fraction = 0;
        }
        break;

    case x_blob:
    case x_xmltype:
    case x_longstring:
        break;
    case x_statement:
    case x_rowid:
        break;
    }

    // Return either the pointer to C++ data itself or the buffer that we
    // allocated, if any.
    return buf ? buf : data;
}

void db2_standard_use_type_backend::bind_by_pos(
    int &position, void *data, exchange_type type, bool /* readOnly */)
{
    if (statement_.use_binding_method_ == details::db2::BOUND_BY_NAME)
    {
        throw soci_error("Binding for use elements must be either by position or by name.");
    }
    statement_.use_binding_method_ = details::db2::BOUND_BY_POSITION;

    this->data = data; // for future reference
    this->type = type; // for future reference
    this->position = position++;
}

void db2_standard_use_type_backend::bind_by_name(
    std::string const &name, void *data, exchange_type type, bool /* readOnly */)
{
    if (statement_.use_binding_method_ == details::db2::BOUND_BY_POSITION)
    {
        throw soci_error("Binding for use elements must be either by position or by name.");
    }
    statement_.use_binding_method_ = details::db2::BOUND_BY_NAME;

    int position = -1;
    int count = 1;

    for (auto const& s : statement_.names_)
    {
        if (s == name)
        {
            position = count;
            break;
        }
        count++;
    }

    if (position != -1)
    {
        this->data = data; // for future reference
        this->type = type; // for future reference
        this->position = position;
    }
    else
    {
        std::ostringstream ss;
        ss << "Unable to find name '" << name << "' to bind to";
        throw soci_error(ss.str());
    }
}

void db2_standard_use_type_backend::pre_use(indicator const *ind_ptr)
{
    // first deal with data
    SQLSMALLINT sqlType;
    SQLSMALLINT cType;
    SQLLEN size;

    void* const sqlData = prepare_for_bind(size, sqlType, cType);

    static const SQLLEN indNull = SQL_NULL_DATA;

    SQLRETURN cliRC = SQLBindParameter(statement_.hStmt,
                                    static_cast<SQLUSMALLINT>(position),
                                    SQL_PARAM_INPUT,
                                    cType, sqlType, size, 0, sqlData, size,
                                    ind_ptr && *ind_ptr == i_null
                                        ? const_cast<SQLLEN *>(&indNull)
                                        : &ind);

    if (cliRC != SQL_SUCCESS)
    {
        throw db2_soci_error("Error while binding value",cliRC);
    }
}

void db2_standard_use_type_backend::post_use(bool /*gotData*/, indicator* /*ind*/)
{
    clean_up();
}

void db2_standard_use_type_backend::clean_up()
{
    if (buf != NULL)
    {
        delete [] buf;
        buf = NULL;
    }
}
