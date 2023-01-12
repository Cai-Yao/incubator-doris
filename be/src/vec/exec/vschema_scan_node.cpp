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

#include "vec/exec/vschema_scan_node.h"

#include "common/status.h"
#include "exec/text_converter.h"
#include "exec/text_converter.hpp"
#include "gen_cpp/PlanNodes_types.h"
#include "runtime/runtime_state.h"
#include "util/runtime_profile.h"
#include "util/types.h"
#include "vec/columns/column.h"
#include "vec/common/string_ref.h"
#include "vec/core/types.h"
namespace doris::vectorized {

VSchemaScanNode::VSchemaScanNode(ObjectPool* pool, const TPlanNode& tnode,
                                 const DescriptorTbl& descs)
        : ScanNode(pool, tnode, descs),
          _is_init(false),
          _table_name(tnode.schema_scan_node.table_name),
          _tuple_id(tnode.schema_scan_node.tuple_id),
          _src_tuple_desc(nullptr),
          _dest_tuple_desc(nullptr),
          _tuple_idx(0),
          _slot_num(0),
          _tuple_pool(nullptr),
          _schema_scanner(nullptr),
          _src_tuple(nullptr),
          _src_single_tuple(nullptr),
          _dest_single_tuple(nullptr) {}

VSchemaScanNode::~VSchemaScanNode() {
    delete[] reinterpret_cast<char*>(_src_tuple);
    _src_tuple = nullptr;

    delete[] reinterpret_cast<char*>(_src_single_tuple);
    _src_single_tuple = nullptr;

    delete[] reinterpret_cast<char*>(_dest_single_tuple);
    _dest_single_tuple = nullptr;
}

Status VSchemaScanNode::init(const TPlanNode& tnode, RuntimeState* state) {
    RETURN_IF_ERROR(ExecNode::init(tnode, state));
    if (tnode.schema_scan_node.__isset.db) {
        _scanner_param.db = _pool->add(new std::string(tnode.schema_scan_node.db));
    }

    if (tnode.schema_scan_node.__isset.table) {
        _scanner_param.table = _pool->add(new std::string(tnode.schema_scan_node.table));
    }

    if (tnode.schema_scan_node.__isset.wild) {
        _scanner_param.wild = _pool->add(new std::string(tnode.schema_scan_node.wild));
    }

    if (tnode.schema_scan_node.__isset.current_user_ident) {
        _scanner_param.current_user_ident =
                _pool->add(new TUserIdentity(tnode.schema_scan_node.current_user_ident));
    } else {
        if (tnode.schema_scan_node.__isset.user) {
            _scanner_param.user = _pool->add(new std::string(tnode.schema_scan_node.user));
        }
        if (tnode.schema_scan_node.__isset.user_ip) {
            _scanner_param.user_ip = _pool->add(new std::string(tnode.schema_scan_node.user_ip));
        }
    }

    if (tnode.schema_scan_node.__isset.ip) {
        _scanner_param.ip = _pool->add(new std::string(tnode.schema_scan_node.ip));
    }
    if (tnode.schema_scan_node.__isset.port) {
        _scanner_param.port = tnode.schema_scan_node.port;
    }

    if (tnode.schema_scan_node.__isset.thread_id) {
        _scanner_param.thread_id = tnode.schema_scan_node.thread_id;
    }

    if (tnode.schema_scan_node.__isset.table_structure) {
        _scanner_param.table_structure = _pool->add(
                new std::vector<TSchemaTableStructure>(tnode.schema_scan_node.table_structure));
    }

    if (tnode.schema_scan_node.__isset.catalog) {
        _scanner_param.catalog = _pool->add(new std::string(tnode.schema_scan_node.catalog));
    }
    return Status::OK();
}

Status VSchemaScanNode::open(RuntimeState* state) {
    if (nullptr == state) {
        return Status::InternalError("input pointer is nullptr.");
    }

    START_AND_SCOPE_SPAN(state->get_tracer(), span, "VSchemaScanNode::open");
    if (!_is_init) {
        span->SetStatus(opentelemetry::trace::StatusCode::kError, "Open before Init.");
        return Status::InternalError("Open before Init.");
    }

    if (nullptr == state) {
        span->SetStatus(opentelemetry::trace::StatusCode::kError, "input pointer is nullptr.");
        return Status::InternalError("input pointer is nullptr.");
    }

    SCOPED_TIMER(_runtime_profile->total_time_counter());
    RETURN_IF_CANCELLED(state);
    RETURN_IF_ERROR(ExecNode::open(state));

    if (_scanner_param.user) {
        TSetSessionParams param;
        param.__set_user(*_scanner_param.user);
        //TStatus t_status;
        //RETURN_IF_ERROR(SchemaJniHelper::set_session(param, &t_status));
        //RETURN_IF_ERROR(Status(t_status));
    }

    return _schema_scanner->start(state);
}

Status VSchemaScanNode::prepare(RuntimeState* state) {
    if (_is_init) {
        return Status::OK();
    }

    if (nullptr == state) {
        return Status::InternalError("state pointer is nullptr.");
    }
    START_AND_SCOPE_SPAN(state->get_tracer(), span, "VSchemaScanNode::prepare");
    RETURN_IF_ERROR(ScanNode::prepare(state));

    // new one mem pool
    _tuple_pool.reset(new (std::nothrow) MemPool());

    if (nullptr == _tuple_pool) {
        return Status::InternalError("Allocate MemPool failed.");
    }

    // get dest tuple desc
    _dest_tuple_desc = state->desc_tbl().get_tuple_descriptor(_tuple_id);

    if (nullptr == _dest_tuple_desc) {
        return Status::InternalError("Failed to get tuple descriptor.");
    }

    _slot_num = _dest_tuple_desc->slots().size();
    // get src tuple desc
    const SchemaTableDescriptor* schema_table =
            static_cast<const SchemaTableDescriptor*>(_dest_tuple_desc->table_desc());

    if (nullptr == schema_table) {
        return Status::InternalError("Failed to get schema table descriptor.");
    }

    // new one scanner
    _schema_scanner.reset(SchemaScanner::create(schema_table->schema_table_type()));

    if (nullptr == _schema_scanner) {
        return Status::InternalError("schema scanner get nullptr pointer.");
    }

    RETURN_IF_ERROR(_schema_scanner->init(&_scanner_param, _pool));
    // get column info from scanner
    _src_tuple_desc = _schema_scanner->tuple_desc();

    if (nullptr == _src_tuple_desc) {
        return Status::InternalError("failed to get src schema tuple desc.");
    }

    _src_tuple =
            reinterpret_cast<doris::Tuple*>(new (std::nothrow) char[_src_tuple_desc->byte_size()]);

    if (nullptr == _src_tuple) {
        return Status::InternalError("new src tuple failed.");
    }

    // if src tuple desc slots is zero, it's the dummy slots.
    if (0 == _src_tuple_desc->slots().size()) {
        _slot_num = 0;
    }

    // check if type is ok.
    if (_slot_num > 0) {
        _index_map.resize(_slot_num);
    }
    for (int i = 0; i < _slot_num; ++i) {
        // TODO(zhaochun): Is this slow?
        int j = 0;
        for (; j < _src_tuple_desc->slots().size(); ++j) {
            if (boost::iequals(_dest_tuple_desc->slots()[i]->col_name(),
                               _src_tuple_desc->slots()[j]->col_name())) {
                break;
            }
        }

        if (j >= _src_tuple_desc->slots().size()) {
            LOG(WARNING) << "no match column for this column("
                         << _dest_tuple_desc->slots()[i]->col_name() << ")";
            return Status::InternalError("no match column for this column.");
        }

        if (_src_tuple_desc->slots()[j]->type().type != _dest_tuple_desc->slots()[i]->type().type) {
            LOG(WARNING) << "schema not match. input is " << _src_tuple_desc->slots()[j]->col_name()
                         << "(" << _src_tuple_desc->slots()[j]->type() << ") and output is "
                         << _dest_tuple_desc->slots()[i]->col_name() << "("
                         << _dest_tuple_desc->slots()[i]->type() << ")";
            return Status::InternalError("schema not match.");
        }
        _index_map[i] = j;
    }

    // TODO(marcel): add int _tuple_idx indexed by TupleId somewhere in runtime_state.h
    _tuple_idx = 0;
    _is_init = true;

    _src_single_tuple =
            reinterpret_cast<doris::Tuple*>(new (std::nothrow) char[_src_tuple_desc->byte_size()]);
    if (nullptr == _src_single_tuple) {
        return Status::InternalError("new src single tuple failed.");
    }

    _dest_single_tuple =
            reinterpret_cast<doris::Tuple*>(new (std::nothrow) char[_dest_tuple_desc->byte_size()]);
    if (nullptr == _dest_single_tuple) {
        return Status::InternalError("new desc single tuple failed.");
    }

    return Status::OK();
}

Status VSchemaScanNode::get_next(RuntimeState* state, vectorized::Block* block, bool* eos) {
    if (state == nullptr || block == nullptr || eos == nullptr) {
        return Status::InternalError("input is NULL pointer");
    }
    INIT_AND_SCOPE_GET_NEXT_SPAN(state->get_tracer(), _get_next_span, "VSchemaScanNode::get_next");
    SCOPED_TIMER(_runtime_profile->total_time_counter());

    VLOG_CRITICAL << "VSchemaScanNode::GetNext";
    if (!_is_init) {
        return Status::InternalError("used before initialize.");
    }
    RETURN_IF_CANCELLED(state);
    bool schema_eos = false;

    block->clear();

    for (int i = 0; i < _slot_num; ++i) {
        int j = _index_map[i];
        const auto slot_desc = _src_tuple_desc->slots()[j];
        block->insert(ColumnWithTypeAndName(slot_desc->get_empty_mutable_column(),
                                            slot_desc->get_data_type_ptr(), slot_desc->col_name()));
    }

    do {
        while (true) {
            RETURN_IF_CANCELLED(state);

            // get all slots from schema table.
            RETURN_IF_ERROR(_schema_scanner->get_next_block(block, &schema_eos));

            if (schema_eos) {
                *eos = true;
                break;
            }

            if (block->rows() >= state->batch_size()) {
                break;
            }
        }

        if (block->rows()) {
            RETURN_IF_ERROR(VExprContext::filter_block(_vconjunct_ctx_ptr, block,
                                                       _dest_tuple_desc->slots().size()));
            VLOG_ROW << "VSchemaScanNode output rows: " << block->rows();
        }
    } while (block->rows() == 0 && !(*eos));

    reached_limit(block, eos);
    return Status::OK();
}

Status VSchemaScanNode::write_slot_to_vectorized_column(void* slot, SlotDescriptor* slot_desc,
                                                        vectorized::MutableColumnPtr* column_ptr) {
    vectorized::IColumn* col_ptr = column_ptr->get();
    if (slot_desc->is_nullable()) {
        auto* nullable_column = reinterpret_cast<vectorized::ColumnNullable*>(column_ptr->get());
        nullable_column->get_null_map_data().push_back(0);
        col_ptr = &nullable_column->get_nested_column();
    }
    switch (slot_desc->type().type) {
    case TYPE_HLL: {
        HyperLogLog* hll_slot = reinterpret_cast<HyperLogLog*>(slot);
        reinterpret_cast<vectorized::ColumnHLL*>(col_ptr)->get_data().emplace_back(*hll_slot);
        break;
    }
    case TYPE_VARCHAR:
    case TYPE_CHAR:
    case TYPE_STRING: {
        StringRef* str_slot = reinterpret_cast<StringRef*>(slot);
        reinterpret_cast<vectorized::ColumnString*>(col_ptr)->insert_data(str_slot->data,
                                                                          str_slot->size);
        break;
    }

    case TYPE_BOOLEAN: {
        uint8_t num = *reinterpret_cast<bool*>(slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::UInt8>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_TINYINT: {
        int8_t num = *reinterpret_cast<int8_t*>(slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int8>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_SMALLINT: {
        int16_t num = *reinterpret_cast<int16_t*>(slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int16>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_INT: {
        int32_t num = *reinterpret_cast<int32_t*>(slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int32>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_BIGINT: {
        int64_t num = *reinterpret_cast<int64_t*>(slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int64>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_LARGEINT: {
        __int128 num;
        memcpy(&num, slot, sizeof(__int128));
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int128>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_FLOAT: {
        float num = *reinterpret_cast<float*>(slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Float32>*>(col_ptr)->insert_value(
                num);
        break;
    }

    case TYPE_DOUBLE: {
        double num = *reinterpret_cast<double*>(slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Float64>*>(col_ptr)->insert_value(
                num);
        break;
    }

    case TYPE_DATE: {
        VecDateTimeValue value;
        DateTimeValue* ts_slot = reinterpret_cast<DateTimeValue*>(slot);
        value.convert_dt_to_vec_dt(ts_slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int64>*>(col_ptr)->insert_data(
                reinterpret_cast<char*>(&value), 0);
        break;
    }

    case TYPE_DATEV2: {
        uint32_t num = *reinterpret_cast<uint32_t*>(slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::UInt32>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_DATETIME: {
        VecDateTimeValue value;
        DateTimeValue* ts_slot = reinterpret_cast<DateTimeValue*>(slot);
        value.convert_dt_to_vec_dt(ts_slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::Int64>*>(col_ptr)->insert_data(
                reinterpret_cast<char*>(&value), 0);
        break;
    }

    case TYPE_DATETIMEV2: {
        uint32_t num = *reinterpret_cast<uint64_t*>(slot);
        reinterpret_cast<vectorized::ColumnVector<vectorized::UInt64>*>(col_ptr)->insert_value(num);
        break;
    }

    case TYPE_DECIMALV2: {
        const Int128 num = (reinterpret_cast<PackedInt128*>(slot))->value;
        reinterpret_cast<vectorized::ColumnDecimal128*>(col_ptr)->insert_data(
                reinterpret_cast<const char*>(&num), 0);
        break;
    }
    case TYPE_DECIMAL128I: {
        const Int128 num = (reinterpret_cast<PackedInt128*>(slot))->value;
        reinterpret_cast<vectorized::ColumnDecimal128I*>(col_ptr)->insert_data(
                reinterpret_cast<const char*>(&num), 0);
        break;
    }

    case TYPE_DECIMAL32: {
        const int32_t num = *reinterpret_cast<int32_t*>(slot);
        reinterpret_cast<vectorized::ColumnDecimal32*>(col_ptr)->insert_data(
                reinterpret_cast<const char*>(&num), 0);
        break;
    }

    case TYPE_DECIMAL64: {
        const int64_t num = *reinterpret_cast<int64_t*>(slot);
        reinterpret_cast<vectorized::ColumnDecimal64*>(col_ptr)->insert_data(
                reinterpret_cast<const char*>(&num), 0);
        break;
    }

    default: {
        DCHECK(false) << "bad slot type: " << slot_desc->type();
        std::stringstream ss;
        ss << "Fail to convert schema type:'" << slot_desc->type() << " on column:`"
           << slot_desc->col_name() + "`";
        return Status::InternalError(ss.str());
    }
    }

    return Status::OK();
}

void VSchemaScanNode::project_tuple() {
    memset(_dest_single_tuple, 0, _dest_tuple_desc->num_null_bytes());

    for (int i = 0; i < _slot_num; ++i) {
        if (!_dest_tuple_desc->slots()[i]->is_materialized()) {
            continue;
        }
        int j = _index_map[i];

        if (_src_single_tuple->is_null(_src_tuple_desc->slots()[j]->null_indicator_offset())) {
            _dest_single_tuple->set_null(_dest_tuple_desc->slots()[i]->null_indicator_offset());
        } else {
            void* dest_slot =
                    _dest_single_tuple->get_slot(_dest_tuple_desc->slots()[i]->tuple_offset());
            void* src_slot =
                    _src_single_tuple->get_slot(_src_tuple_desc->slots()[j]->tuple_offset());
            int slot_size = _src_tuple_desc->slots()[j]->type().get_slot_size();
            memcpy(dest_slot, src_slot, slot_size);
        }
    }
}

Status VSchemaScanNode::close(RuntimeState* state) {
    if (is_closed()) {
        return Status::OK();
    }
    START_AND_SCOPE_SPAN(state->get_tracer(), span, "VSchemaScanNode::close");
    SCOPED_TIMER(_runtime_profile->total_time_counter());

    _tuple_pool.reset();
    return ExecNode::close(state);
}

void VSchemaScanNode::debug_string(int indentation_level, std::stringstream* out) const {
    *out << string(indentation_level * 2, ' ');
    *out << "SchemaScanNode(tupleid=" << _tuple_id << " table=" << _table_name;
    *out << ")" << std::endl;

    for (int i = 0; i < _children.size(); ++i) {
        _children[i]->debug_string(indentation_level + 1, out);
    }
}

Status VSchemaScanNode::set_scan_ranges(const std::vector<TScanRangeParams>& scan_ranges) {
    return Status::OK();
}

} // namespace doris::vectorized
