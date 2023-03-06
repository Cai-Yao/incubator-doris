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

#include "exec/schema_scanner/schema_schema_privileges_scanner.h"

#include "exec/schema_scanner/schema_helper.h"
#include "runtime/primitive_type.h"
#include "runtime/string_value.h"

namespace doris {

std::vector<SchemaScanner::ColumnDesc> SchemaSchemaPrivilegesScanner::_s_tbls_columns = {
        //   name,       type,          size,     is_null
        {"GRANTEE", TYPE_VARCHAR, sizeof(StringValue), true},
        {"TABLE_CATALOG", TYPE_VARCHAR, sizeof(StringValue), true},
        {"TABLE_SCHEMA", TYPE_VARCHAR, sizeof(StringValue), false},
        {"PRIVILEGE_TYPE", TYPE_VARCHAR, sizeof(StringValue), false},
        {"IS_GRANTABLE", TYPE_VARCHAR, sizeof(StringValue), true},
};

SchemaSchemaPrivilegesScanner::SchemaSchemaPrivilegesScanner()
        : SchemaScanner(_s_tbls_columns, TSchemaTableType::SCH_SCHEMA_PRIVILEGES), _priv_index(0) {}

SchemaSchemaPrivilegesScanner::~SchemaSchemaPrivilegesScanner() {}

Status SchemaSchemaPrivilegesScanner::start(RuntimeState* state) {
    if (!_is_init) {
        return Status::InternalError("used before initialized.");
    }
    RETURN_IF_ERROR(_get_new_table());
    return Status::OK();
}

Status SchemaSchemaPrivilegesScanner::fill_one_row(Tuple* tuple, MemPool* pool) {
    // set all bit to not null
    memset((void*)tuple, 0, _tuple_desc->num_null_bytes());
    const TPrivilegeStatus& priv_status = _priv_result.privileges[_priv_index];
    // grantee
    {
        Status status = fill_one_col(&priv_status.grantee, pool,
                                     tuple->get_slot(_tuple_desc->slots()[0]->tuple_offset()));
        if (!status.ok()) {
            return status;
        }
    }
    // catalog
    // This value is always def.
    {
        std::string definer = "def";
        Status status = fill_one_col(&definer, pool,
                                     tuple->get_slot(_tuple_desc->slots()[1]->tuple_offset()));
        if (!status.ok()) {
            return status;
        }
    }
    // schema
    {
        Status status = fill_one_col(&priv_status.schema, pool,
                                     tuple->get_slot(_tuple_desc->slots()[2]->tuple_offset()));
        if (!status.ok()) {
            return status;
        }
    }
    // privilege type
    {
        Status status = fill_one_col(&priv_status.privilege_type, pool,
                                     tuple->get_slot(_tuple_desc->slots()[3]->tuple_offset()));
        if (!status.ok()) {
            return status;
        }
    }
    // is grantable
    {
        Status status = fill_one_col(&priv_status.is_grantable, pool,
                                     tuple->get_slot(_tuple_desc->slots()[4]->tuple_offset()));
        if (!status.ok()) {
            return status;
        }
    }
    _priv_index++;
    return Status::OK();
}

Status SchemaSchemaPrivilegesScanner::fill_one_col(const std::string* src, MemPool* pool,
                                                   void* slot) {
    if (nullptr == slot || nullptr == pool || nullptr == src) {
        return Status::InternalError("input pointer is nullptr.");
    }
    StringValue* str_slot = reinterpret_cast<StringValue*>(slot);
    str_slot->len = src->length();
    str_slot->ptr = (char*)pool->allocate(str_slot->len);
    if (nullptr == str_slot->ptr) {
        return Status::InternalError("Allocate memcpy failed.");
    }
    memcpy(str_slot->ptr, src->c_str(), str_slot->len);
    return Status::OK();
}

Status SchemaSchemaPrivilegesScanner::_get_new_table() {
    TGetTablesParams table_params;
    if (nullptr != _param->wild) {
        table_params.__set_pattern(*(_param->wild));
    }
    if (nullptr != _param->current_user_ident) {
        table_params.__set_current_user_ident(*(_param->current_user_ident));
    } else {
        if (nullptr != _param->user) {
            table_params.__set_user(*(_param->user));
        }
        if (nullptr != _param->user_ip) {
            table_params.__set_user_ip(*(_param->user_ip));
        }
    }

    if (nullptr != _param->ip && 0 != _param->port) {
        RETURN_IF_ERROR(SchemaHelper::list_schema_privilege_status(*(_param->ip), _param->port,
                                                                   table_params, &_priv_result));
    } else {
        return Status::InternalError("IP or port doesn't exists");
    }
    _priv_index = 0;
    return Status::OK();
}

Status SchemaSchemaPrivilegesScanner::get_next_row(Tuple* tuple, MemPool* pool, bool* eos) {
    if (!_is_init) {
        return Status::InternalError("Used before initialized.");
    }
    if (nullptr == tuple || nullptr == pool || nullptr == eos) {
        return Status::InternalError("input pointer is nullptr.");
    }
    if (_priv_index >= _priv_result.privileges.size()) {
        *eos = true;
        return Status::OK();
    }
    *eos = false;
    return fill_one_row(tuple, pool);
}

Status SchemaSchemaPrivilegesScanner::get_next_block(vectorized::Block* block, bool* eos) {
    if (!_is_init) {
        return Status::InternalError("Used before initialized.");
    }
    if (nullptr == block || nullptr == eos) {
        return Status::InternalError("input pointer is nullptr.");
    }

    *eos = true;
    if (!_priv_result.privileges.size()) {
        return Status::OK();
    }
    return _fill_block_impl(block);
}

Status SchemaSchemaPrivilegesScanner::_fill_block_impl(vectorized::Block* block) {
    SCOPED_TIMER(_fill_block_timer);
    auto privileges_num = _priv_result.privileges.size();
    std::vector<void*> datas(privileges_num);

    // grantee
    {
        StringRef strs[privileges_num];
        for (int i = 0; i < privileges_num; ++i) {
            const TPrivilegeStatus& priv_status = _priv_result.privileges[i];
            strs[i] = StringRef(priv_status.grantee.c_str(), priv_status.grantee.size());
            datas[i] = strs + i;
        }
        fill_dest_column_for_range(block, 0, datas);
    }
    // catalog
    // This value is always def.
    {
        std::string definer = "def";
        StringRef str = StringRef(definer.c_str(), definer.size());
        for (int i = 0; i < privileges_num; ++i) {
            datas[i] = &str;
        }
        fill_dest_column_for_range(block, 1, datas);
    }
    // schema
    {
        StringRef strs[privileges_num];
        for (int i = 0; i < privileges_num; ++i) {
            const TPrivilegeStatus& priv_status = _priv_result.privileges[i];
            strs[i] = StringRef(priv_status.schema.c_str(), priv_status.schema.size());
            datas[i] = strs + i;
        }
        fill_dest_column_for_range(block, 2, datas);
    }
    // privilege type
    {
        StringRef strs[privileges_num];
        for (int i = 0; i < privileges_num; ++i) {
            const TPrivilegeStatus& priv_status = _priv_result.privileges[i];
            strs[i] = StringRef(priv_status.privilege_type.c_str(),
                                priv_status.privilege_type.size());
            datas[i] = strs + i;
        }
        fill_dest_column_for_range(block, 3, datas);
    }
    // is grantable
    {
        StringRef strs[privileges_num];
        for (int i = 0; i < privileges_num; ++i) {
            const TPrivilegeStatus& priv_status = _priv_result.privileges[i];
            strs[i] = StringRef(priv_status.is_grantable.c_str(), priv_status.is_grantable.size());
            datas[i] = strs + i;
        }
        fill_dest_column_for_range(block, 4, datas);
    }
    return Status::OK();
}

} // namespace doris