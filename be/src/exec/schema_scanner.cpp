// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "exec/schema_scanner.h"

#include "exec/schema_scanner/schema_backends_scanner.h"
#include "exec/schema_scanner/schema_charsets_scanner.h"
#include "exec/schema_scanner/schema_collations_scanner.h"
#include "exec/schema_scanner/schema_columns_scanner.h"
#include "exec/schema_scanner/schema_dummy_scanner.h"
#include "exec/schema_scanner/schema_files_scanner.h"
#include "exec/schema_scanner/schema_partitions_scanner.h"
#include "exec/schema_scanner/schema_rowsets_scanner.h"
#include "exec/schema_scanner/schema_schema_privileges_scanner.h"
#include "exec/schema_scanner/schema_schemata_scanner.h"
#include "exec/schema_scanner/schema_table_privileges_scanner.h"
#include "exec/schema_scanner/schema_tables_scanner.h"
#include "exec/schema_scanner/schema_user_privileges_scanner.h"
#include "exec/schema_scanner/schema_variables_scanner.h"
#include "exec/schema_scanner/schema_views_scanner.h"
#include "runtime/define_primitive_type.h"
#include "vec/columns/column.h"
#include "vec/common/string_ref.h"
#include "vec/core/block.h"

namespace doris {

DorisServer* SchemaScanner::_s_doris_server;

SchemaScanner::SchemaScanner(const std::vector<ColumnDesc>& columns)
        : _is_init(false),
          _param(nullptr),
          _columns(columns),
          _schema_table_type(TSchemaTableType::SCH_INVALID) {}

SchemaScanner::SchemaScanner(const std::vector<ColumnDesc>& columns, TSchemaTableType::type type)
        : _is_init(false), _param(nullptr), _columns(columns), _schema_table_type(type) {}

SchemaScanner::~SchemaScanner() {}

Status SchemaScanner::start(RuntimeState* state) {
    if (!_is_init) {
        return Status::InternalError("call Start before Init.");
    }

    return Status::OK();
}

Status SchemaScanner::get_next_block(vectorized::Block* block, bool* eos) {
    if (!_is_init) {
        return Status::InternalError("used before initialized.");
    }

    if (nullptr == block || nullptr == eos) {
        return Status::InternalError("input pointer is nullptr.");
    }

    *eos = true;
    return Status::OK();
}

Status SchemaScanner::init(SchemaScannerParam* param, ObjectPool* pool) {
    if (_is_init) {
        return Status::OK();
    }
    if (nullptr == param || nullptr == pool) {
        return Status::InternalError("invalid parameter");
    }

    if (_columns.empty()) {
        return Status::InternalError("invalid parameter");
    }

    _param = param;
    _is_init = true;

    return Status::OK();
}

SchemaScanner* SchemaScanner::create(TSchemaTableType::type type) {
    switch (type) {
    case TSchemaTableType::SCH_TABLES:
        return new (std::nothrow) SchemaTablesScanner();
    case TSchemaTableType::SCH_SCHEMATA:
        return new (std::nothrow) SchemaSchemataScanner();
    case TSchemaTableType::SCH_COLUMNS:
        return new (std::nothrow) SchemaColumnsScanner();
    case TSchemaTableType::SCH_CHARSETS:
        return new (std::nothrow) SchemaCharsetsScanner();
    case TSchemaTableType::SCH_COLLATIONS:
        return new (std::nothrow) SchemaCollationsScanner();
    case TSchemaTableType::SCH_GLOBAL_VARIABLES:
        return new (std::nothrow) SchemaVariablesScanner(TVarType::GLOBAL);
    case TSchemaTableType::SCH_SESSION_VARIABLES:
    case TSchemaTableType::SCH_VARIABLES:
        return new (std::nothrow) SchemaVariablesScanner(TVarType::SESSION);
    case TSchemaTableType::SCH_VIEWS:
        return new (std::nothrow) SchemaViewsScanner();
    case TSchemaTableType::SCH_TABLE_PRIVILEGES:
        return new (std::nothrow) SchemaTablePrivilegesScanner();
    case TSchemaTableType::SCH_SCHEMA_PRIVILEGES:
        return new (std::nothrow) SchemaSchemaPrivilegesScanner();
    case TSchemaTableType::SCH_USER_PRIVILEGES:
        return new (std::nothrow) SchemaUserPrivilegesScanner();
    case TSchemaTableType::SCH_FILES:
        return new (std::nothrow) SchemaFilesScanner();
    case TSchemaTableType::SCH_PARTITIONS:
        return new (std::nothrow) SchemaPartitionsScanner();
    case TSchemaTableType::SCH_ROWSETS:
        return new (std::nothrow) SchemaRowsetsScanner();
    case TSchemaTableType::SCH_BACKENDS:
        return new (std::nothrow) SchemaBackendsScanner();
    default:
        return new (std::nothrow) SchemaDummyScanner();
        break;
    }
}

Status SchemaScanner::fill_dest_column(vectorized::Block* block, void* data,
                                       const ColumnDesc& col_desc) {
    if (!block->has(col_desc.name)) {
        return Status::OK();
    }
    vectorized::MutableColumnPtr column_ptr =
            std::move(*block->get_by_name(col_desc.name).column).assume_mutable();
    vectorized::IColumn* col_ptr = column_ptr.get();

    if (data == nullptr) {
        auto* nullable_column = reinterpret_cast<vectorized::ColumnNullable*>(col_ptr);
        nullable_column->insert_data(nullptr, 0);
        return Status::OK();
    }
    auto* nullable_column = reinterpret_cast<vectorized::ColumnNullable*>(col_ptr);
    nullable_column->get_null_map_data().push_back(0);
    col_ptr = &nullable_column->get_nested_column();
    switch (col_desc.type) {
    case TYPE_HLL: {
        HyperLogLog* hll_slot = reinterpret_cast<HyperLogLog*>(data);
        reinterpret_cast<vectorized::ColumnHLL*>(col_ptr)->get_data().emplace_back(*hll_slot);
        break;
    }
    case TYPE_VARCHAR:
    case TYPE_CHAR:
    case TYPE_STRING: {
        StringRef* str_slot = reinterpret_cast<StringRef*>(data);
        reinterpret_cast<vectorized::ColumnString*>(col_ptr)->insert_data(str_slot->data,
                                                                          str_slot->size);
        break;
    }

    case TYPE_BOOLEAN: {
        uint8_t num = *reinterpret_cast<bool*>(data);
        reinterpret_cast<vectorized::ColumnVector<vectorized::UInt8>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_TINYINT: {
        int8_t num = *reinterpret_cast<int8_t*>(data);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int8>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_SMALLINT: {
        int16_t num = *reinterpret_cast<int16_t*>(data);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int16>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_INT: {
        int32_t num = *reinterpret_cast<int32_t*>(data);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int32>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_BIGINT: {
        int64_t num = *reinterpret_cast<int64_t*>(data);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int64>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_LARGEINT: {
        __int128 num;
        memcpy(&num, data, sizeof(__int128));
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int128>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_FLOAT: {
        float num = *reinterpret_cast<float*>(data);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Float32>*>(col_ptr)->insert_value(
                num);
        break;
    }

    case TYPE_DOUBLE: {
        double num = *reinterpret_cast<double*>(data);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Float64>*>(col_ptr)->insert_value(
                num);
        break;
    }

    case TYPE_DATE: {
        vectorized::VecDateTimeValue value;
        DateTimeValue* ts_slot = reinterpret_cast<DateTimeValue*>(data);
        value.convert_dt_to_vec_dt(ts_slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int64>*>(col_ptr)->insert_data(
                reinterpret_cast<char*>(&value), 0);
        break;
    }

    case TYPE_DATEV2: {
        uint32_t num = *reinterpret_cast<uint32_t*>(data);
        reinterpret_cast<vectorized::ColumnVector<vectorized::UInt32>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_DATETIME: {
        vectorized::VecDateTimeValue value;
        DateTimeValue* ts_slot = reinterpret_cast<DateTimeValue*>(data);
        value.convert_dt_to_vec_dt(ts_slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int64>*>(col_ptr)->insert_data(
                reinterpret_cast<char*>(&value), 0);
        break;
    }

    case TYPE_DATETIMEV2: {
        uint32_t num = *reinterpret_cast<uint64_t*>(data);
        reinterpret_cast<vectorized::ColumnVector<vectorized::UInt64>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_DECIMALV2: {
        const vectorized::Int128 num = (reinterpret_cast<PackedInt128*>(data))->value;
        reinterpret_cast<vectorized::ColumnDecimal128*>(col_ptr)->insert_data(
                reinterpret_cast<const char*>(&num), 0);
        break;
    }
    case TYPE_DECIMAL128I: {
        const vectorized::Int128 num = (reinterpret_cast<PackedInt128*>(data))->value;
        reinterpret_cast<vectorized::ColumnDecimal128I*>(col_ptr)->insert_data(
                reinterpret_cast<const char*>(&num), 0);
        break;
    }

    case TYPE_DECIMAL32: {
        const int32_t num = *reinterpret_cast<int32_t*>(data);
        reinterpret_cast<vectorized::ColumnDecimal32*>(col_ptr)->insert_data(
                reinterpret_cast<const char*>(&num), 0);
        break;
    }

    case TYPE_DECIMAL64: {
        const int64_t num = *reinterpret_cast<int64_t*>(data);
        reinterpret_cast<vectorized::ColumnDecimal64*>(col_ptr)->insert_data(
                reinterpret_cast<const char*>(&num), 0);
        break;
    }

    default: {
        DCHECK(false) << "bad slot type: " << col_desc.type;
        std::stringstream ss;
        ss << "Fail to convert schema type:'" << col_desc.type << " on column:`"
           << std::string(col_desc.name) + "`";
        return Status::InternalError(ss.str());
    }
    }

    return Status::OK();
}

} // namespace doris
