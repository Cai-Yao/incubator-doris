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

#pragma once

#include <stdint.h>

#include "exec/schema_scanner.h"

namespace doris {

class SchemaCharsetsScanner : public SchemaScanner {
public:
    SchemaCharsetsScanner();
    virtual ~SchemaCharsetsScanner();

    virtual Status get_next_row(Tuple* tuple, MemPool* pool, bool* eos);
    Status get_next_block(vectorized::Block* block, bool* eos) override;

private:
    struct CharsetStruct {
        const char* charset;
        const char* default_collation;
        const char* description;
        int64_t maxlen;
    };

    Status fill_one_row(Tuple* tuple, MemPool* pool);
    Status _fill_block_imp(vectorized::Block* block);

    int _index;
    static SchemaScanner::ColumnDesc _s_css_columns[];
    static CharsetStruct _s_charsets[];
};

} // namespace doris
