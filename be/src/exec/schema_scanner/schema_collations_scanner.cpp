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

#include "exec/schema_scanner/schema_collations_scanner.h"

#include "common/status.h"
#include "runtime/primitive_type.h"
#include "vec/common/string_ref.h"

namespace doris {

SchemaScanner::ColumnDesc SchemaCollationsScanner::_s_cols_columns[] = {
        //   name,       type,          size
        {"COLLATION_NAME", TYPE_VARCHAR, sizeof(StringRef), false},
        {"CHARACTER_SET_NAME", TYPE_VARCHAR, sizeof(StringRef), false},
        {"ID", TYPE_BIGINT, sizeof(int64_t), false},
        {"IS_DEFAULT", TYPE_VARCHAR, sizeof(StringRef), false},
        {"IS_COMPILED", TYPE_VARCHAR, sizeof(StringRef), false},
        {"SORTLEN", TYPE_BIGINT, sizeof(int64_t), false},
};

SchemaCollationsScanner::CollationStruct SchemaCollationsScanner::_s_collations[] = {
        {"utf8_general_ci", "utf8", 33, "Yes", "Yes", 1},
        {nullptr, nullptr, 0, nullptr, nullptr, 0},
};

SchemaCollationsScanner::SchemaCollationsScanner()
        : SchemaScanner(_s_cols_columns,
                        sizeof(_s_cols_columns) / sizeof(SchemaScanner::ColumnDesc),
                        TSchemaTableType::SCH_COLLATIONS),
          _index(0) {}

SchemaCollationsScanner::~SchemaCollationsScanner() {}

Status SchemaCollationsScanner::fill_one_row(Tuple* tuple, MemPool* pool) {
    // COLLATION_NAME
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[0]->tuple_offset());
        StringRef* str_slot = reinterpret_cast<StringRef*>(slot);
        int len = strlen(_s_collations[_index].name);
        str_slot->data = (char*)pool->allocate(len + 1);
        if (nullptr == str_slot->data) {
            return Status::InternalError("No Memory.");
        }
        memcpy(const_cast<char*>(str_slot->data), _s_collations[_index].name, len + 1);
        str_slot->size = len;
    }
    // charset
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[1]->tuple_offset());
        StringRef* str_slot = reinterpret_cast<StringRef*>(slot);
        int len = strlen(_s_collations[_index].charset);
        str_slot->data = (char*)pool->allocate(len + 1);
        if (nullptr == str_slot->data) {
            return Status::InternalError("No Memory.");
        }
        memcpy(const_cast<char*>(str_slot->data), _s_collations[_index].charset, len + 1);
        str_slot->size = len;
    }
    // id
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[2]->tuple_offset());
        *(int64_t*)slot = _s_collations[_index].id;
    }
    // is_default
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[3]->tuple_offset());
        StringRef* str_slot = reinterpret_cast<StringRef*>(slot);
        int len = strlen(_s_collations[_index].is_default);
        str_slot->data = (char*)pool->allocate(len + 1);
        if (nullptr == str_slot->data) {
            return Status::InternalError("No Memory.");
        }
        memcpy(const_cast<char*>(str_slot->data), _s_collations[_index].is_default, len + 1);
        str_slot->size = len;
    }
    // IS_COMPILED
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[4]->tuple_offset());
        StringRef* str_slot = reinterpret_cast<StringRef*>(slot);
        int len = strlen(_s_collations[_index].is_compile);
        str_slot->data = (char*)pool->allocate(len + 1);
        if (nullptr == str_slot->data) {
            return Status::InternalError("No Memory.");
        }
        memcpy(const_cast<char*>(str_slot->data), _s_collations[_index].is_compile, len + 1);
        str_slot->size = len;
    }
    // sortlen
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[5]->tuple_offset());
        *(int64_t*)slot = _s_collations[_index].sortlen;
    }
    _index++;
    return Status::OK();
}

Status SchemaCollationsScanner::get_next_row(Tuple* tuple, MemPool* pool, bool* eos) {
    if (!_is_init) {
        return Status::InternalError("call this before initial.");
    }
    if (nullptr == tuple || nullptr == pool || nullptr == eos) {
        return Status::InternalError("invalid parameter.");
    }
    if (nullptr == _s_collations[_index].name) {
        *eos = true;
        return Status::OK();
    }

    *eos = false;
    return fill_one_row(tuple, pool);
}

Status SchemaCollationsScanner::get_next_block(vectorized::Block* block, bool* eos) {
    if (!_is_init) {
        return Status::InternalError("call this before initial.");
    }
    if (nullptr == block || nullptr == eos) {
        return Status::InternalError("invalid parameter.");
    }

    *eos = true;
    return _fill_block_impl(block);
}

Status SchemaCollationsScanner::_fill_block_impl(vectorized::Block* block) {
    auto row_num = 0;
    while (nullptr != _s_collations[row_num].name) {
        ++row_num;
    }
    // COLLATION_NAME
    for (int i = 0; i < row_num; ++i) {
        StringValue str = StringValue(_s_collations[i].name, strlen(_s_collations[i].name));
        fill_dest_column(block, &str, _tuple_desc->slots()[0]);
    }
    // charset
    for (int i = 0; i < row_num; ++i) {
        StringValue str = StringValue(_s_collations[i].charset, strlen(_s_collations[i].charset));
        fill_dest_column(block, &str, _tuple_desc->slots()[1]);
    }
    // id
    for (int i = 0; i < row_num; ++i) {
        int64_t src = _s_collations[_index].id;
        fill_dest_column(block, &src, _tuple_desc->slots()[2]);
    }
    // is_default
    for (int i = 0; i < row_num; ++i) {
        StringValue str =
                StringValue(_s_collations[i].is_default, strlen(_s_collations[i].is_default));
        fill_dest_column(block, &str, _tuple_desc->slots()[3]);
    }
    // IS_COMPILED
    for (int i = 0; i < row_num; ++i) {
        StringValue str =
                StringValue(_s_collations[i].is_compile, strlen(_s_collations[i].is_compile));
        fill_dest_column(block, &str, _tuple_desc->slots()[4]);
    }
    // sortlen
    for (int i = 0; i < row_num; ++i) {
        int64_t src = _s_collations[_index].sortlen;
        fill_dest_column(block, &src, _tuple_desc->slots()[5]);
    }
    return Status::OK();
}

} // namespace doris
