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

#include "exec/schema_scanner/schema_rowsets_scanner.h"

#include <cstddef>

#include "common/status.h"
#include "gutil/integral_types.h"
#include "olap/rowset/beta_rowset.h"
#include "olap/rowset/rowset.h"
#include "olap/rowset/segment_v2/segment.h"
#include "olap/segment_loader.h"
#include "olap/storage_engine.h"
#include "olap/tablet.h"
#include "runtime/descriptors.h"
#include "runtime/primitive_type.h"
#include "runtime/string_value.h"
namespace doris {
std::vector<SchemaScanner::ColumnDesc> SchemaRowsetsScanner::_s_tbls_columns = {
        //   name,       type,          size,     is_null
        {"BACKEND_ID", TYPE_BIGINT, sizeof(int64_t), true},
        {"ROWSET_ID", TYPE_VARCHAR, sizeof(StringValue), true},
        {"TABLET_ID", TYPE_BIGINT, sizeof(int64_t), true},
        {"ROWSET_NUM_ROWS", TYPE_BIGINT, sizeof(int64_t), true},
        {"TXN_ID", TYPE_BIGINT, sizeof(int64_t), true},
        {"NUM_SEGMENTS", TYPE_BIGINT, sizeof(int64_t), true},
        {"START_VERSION", TYPE_BIGINT, sizeof(int64_t), true},
        {"END_VERSION", TYPE_BIGINT, sizeof(int64_t), true},
        {"INDEX_DISK_SIZE", TYPE_BIGINT, sizeof(size_t), true},
        {"DATA_DISK_SIZE", TYPE_BIGINT, sizeof(size_t), true},
        {"CREATION_TIME", TYPE_BIGINT, sizeof(int64_t), true},
        {"OLDEST_WRITE_TIMESTAMP", TYPE_BIGINT, sizeof(int64_t), true},
        {"NEWEST_WRITE_TIMESTAMP", TYPE_BIGINT, sizeof(int64_t), true},

};

SchemaRowsetsScanner::SchemaRowsetsScanner()
        : SchemaScanner(_s_tbls_columns, TSchemaTableType::SCH_ROWSETS),
          backend_id_(0),
          _rowsets_idx(0) {};

Status SchemaRowsetsScanner::start(RuntimeState* state) {
    if (!_is_init) {
        return Status::InternalError("used before initialized.");
    }
    backend_id_ = state->backend_id();
    RETURN_IF_ERROR(_get_all_rowsets());
    return Status::OK();
}

Status SchemaRowsetsScanner::get_next_row(Tuple* tuple, MemPool* pool, bool* eos) {
    if (!_is_init) {
        return Status::InternalError("Used before initialized.");
    }
    if (nullptr == tuple || nullptr == pool || nullptr == eos) {
        return Status::InternalError("input pointer is nullptr.");
    }
    if (_rowsets_idx >= rowsets_.size()) {
        *eos = true;
        return Status::OK();
    }
    *eos = false;
    return fill_one_row(tuple, pool);
}

Status SchemaRowsetsScanner::_get_all_rowsets() {
    std::vector<TabletSharedPtr> tablets =
            StorageEngine::instance()->tablet_manager()->get_all_tablet();
    for (const auto& tablet : tablets) {
        // all rowset
        std::vector<std::pair<Version, RowsetSharedPtr>> all_rowsets;
        {
            std::shared_lock rowset_ldlock(tablet->get_header_lock());
            tablet->acquire_version_and_rowsets(&all_rowsets);
        }
        for (const auto& version_and_rowset : all_rowsets) {
            RowsetSharedPtr rowset = version_and_rowset.second;
            rowsets_.emplace_back(rowset);
        }
    }
    return Status::OK();
}

Status SchemaRowsetsScanner::fill_one_row(Tuple* tuple, MemPool* pool) {
    // set all bit to not null
    memset((void*)tuple, 0, _tuple_desc->num_null_bytes());
    RowsetSharedPtr rowset = rowsets_[_rowsets_idx];
    // BACKEND_ID
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[0]->tuple_offset());
        *(reinterpret_cast<int64_t*>(slot)) = backend_id_;
    }
    // ROWSET_ID
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[1]->tuple_offset());
        StringValue* str_slot = reinterpret_cast<StringValue*>(slot);
        std::string rowset_id = rowset->rowset_id().to_string();
        str_slot->ptr = (char*)pool->allocate(rowset_id.size());
        str_slot->len = rowset_id.size();
        memcpy(str_slot->ptr, rowset_id.c_str(), str_slot->len);
    }
    // TABLET_ID
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[2]->tuple_offset());
        *(reinterpret_cast<int64_t*>(slot)) = rowset->rowset_meta()->tablet_id();
    }
    // ROWSET_NUM_ROWS
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[3]->tuple_offset());
        *(reinterpret_cast<int64_t*>(slot)) = rowset->num_rows();
    }
    // TXN_ID
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[4]->tuple_offset());
        *(reinterpret_cast<int64_t*>(slot)) = rowset->txn_id();
    }
    // NUM_SEGMENTS
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[5]->tuple_offset());
        *(reinterpret_cast<int64_t*>(slot)) = rowset->num_segments();
    }
    // START_VERSION
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[6]->tuple_offset());
        *(reinterpret_cast<int64_t*>(slot)) = rowset->start_version();
    }
    // END_VERSION
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[7]->tuple_offset());
        *(reinterpret_cast<int64_t*>(slot)) = rowset->end_version();
    }
    // INDEX_DISK_SIZE
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[8]->tuple_offset());
        *(reinterpret_cast<size_t*>(slot)) = rowset->index_disk_size();
    }
    // DATA_DISK_SIZE
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[9]->tuple_offset());
        *(reinterpret_cast<size_t*>(slot)) = rowset->data_disk_size();
    }
    // CREATION_TIME
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[10]->tuple_offset());
        *(reinterpret_cast<size_t*>(slot)) = rowset->creation_time();
    }
    // OLDEST_WRITE_TIMESTAMP
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[11]->tuple_offset());
        *(reinterpret_cast<size_t*>(slot)) = rowset->oldest_write_timestamp();
    }
    // NEWEST_WRITE_TIMESTAMP
    {
        void* slot = tuple->get_slot(_tuple_desc->slots()[12]->tuple_offset());
        *(reinterpret_cast<size_t*>(slot)) = rowset->newest_write_timestamp();
    }
    ++_rowsets_idx;
    return Status::OK();
}

Status SchemaRowsetsScanner::get_next_block(vectorized::Block* block, bool* eos) {
    if (!_is_init) {
        return Status::InternalError("Used before initialized.");
    }
    if (nullptr == block || nullptr == eos) {
        return Status::InternalError("input pointer is nullptr.");
    }

    if (_rowsets_idx >= rowsets_.size()) {
        *eos = true;
        return Status::OK();
    }
    *eos = false;
    return _fill_block_impl(block);
}

Status SchemaRowsetsScanner::_fill_block_impl(vectorized::Block* block) {
    SCOPED_TIMER(_fill_block_timer);
    size_t fill_rowsets_num = std::min(1000ul, rowsets_.size() - _rowsets_idx);
    auto fill_idx_begin = _rowsets_idx;
    auto fill_idx_end = _rowsets_idx + fill_rowsets_num;
    std::vector<void*> datas(fill_rowsets_num);
    // BACKEND_ID
    {
        int64_t src = backend_id_;
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            datas[i - fill_idx_begin] = &src;
        }
        fill_dest_column_for_range(block, 0, datas);
    }
    // ROWSET_ID
    {
        std::string rowset_ids[fill_rowsets_num];
        StringRef strs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            rowset_ids[i - fill_idx_begin] = rowset->rowset_id().to_string();
            strs[i - fill_idx_begin] = StringRef(rowset_ids[i - fill_idx_begin].c_str(),
                                                 rowset_ids[i - fill_idx_begin].size());
            datas[i - fill_idx_begin] = strs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 1, datas);
    }
    // TABLET_ID
    {
        int64_t srcs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            srcs[i - fill_idx_begin] = rowset->rowset_meta()->tablet_id();
            datas[i - fill_idx_begin] = srcs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 2, datas);
    }
    // ROWSET_NUM_ROWS
    {
        int64_t srcs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            srcs[i - fill_idx_begin] = rowset->num_rows();
            datas[i - fill_idx_begin] = srcs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 3, datas);
    }
    // TXN_ID
    {
        int64_t srcs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            srcs[i - fill_idx_begin] = rowset->txn_id();
            datas[i - fill_idx_begin] = srcs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 4, datas);
    }
    // NUM_SEGMENTS
    {
        int64_t srcs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            srcs[i - fill_idx_begin] = rowset->num_segments();
            datas[i - fill_idx_begin] = srcs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 5, datas);
    }
    // START_VERSION
    {
        int64_t srcs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            srcs[i - fill_idx_begin] = rowset->start_version();
            datas[i - fill_idx_begin] = srcs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 6, datas);
    }
    // END_VERSION
    {
        int64_t srcs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            srcs[i - fill_idx_begin] = rowset->end_version();
            datas[i - fill_idx_begin] = srcs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 7, datas);
    }
    // INDEX_DISK_SIZE
    {
        size_t srcs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            srcs[i - fill_idx_begin] = rowset->index_disk_size();
            datas[i - fill_idx_begin] = srcs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 8, datas);
    }
    // DATA_DISK_SIZE
    {
        size_t srcs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            srcs[i - fill_idx_begin] = rowset->data_disk_size();
            datas[i - fill_idx_begin] = srcs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 9, datas);
    }
    // CREATION_TIME
    {
        size_t srcs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            srcs[i - fill_idx_begin] = rowset->creation_time();
            datas[i - fill_idx_begin] = srcs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 10, datas);
    }
    // NEWEST_WRITE_TIMESTAMP
    {
        size_t srcs[fill_rowsets_num];
        for (int i = fill_idx_begin; i < fill_idx_end; ++i) {
            RowsetSharedPtr rowset = rowsets_[i];
            srcs[i - fill_idx_begin] = rowset->newest_write_timestamp();
            datas[i - fill_idx_begin] = srcs + i - fill_idx_begin;
        }
        fill_dest_column_for_range(block, 11, datas);
    }
    _rowsets_idx += fill_rowsets_num;
    return Status::OK();
}
} // namespace doris
