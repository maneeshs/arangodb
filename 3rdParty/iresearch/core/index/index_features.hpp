////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2021 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Andrey Abramov
////////////////////////////////////////////////////////////////////////////////

#ifndef IRESEARCH_INDEX_FEATURES_H
#define IRESEARCH_INDEX_FEATURES_H

#include <functional>
#include <map>

#include "utils/bit_utils.hpp"
#include "utils/type_info.hpp"

namespace iresearch {

struct field_stats;
struct column_output;

//////////////////////////////////////////////////////////////////////////////
/// @enum IndexFeatures
/// @brief represents a set of features that can be stored in the index
//////////////////////////////////////////////////////////////////////////////
enum class IndexFeatures : byte_type {
  ////////////////////////////////////////////////////////////////////////////
  /// @brief documents
  ////////////////////////////////////////////////////////////////////////////
  NONE = 0,

  ////////////////////////////////////////////////////////////////////////////
  /// @brief frequency
  ////////////////////////////////////////////////////////////////////////////
  FREQ = 1,

  ////////////////////////////////////////////////////////////////////////////
  /// @brief positions, depends on frequency
  ////////////////////////////////////////////////////////////////////////////
  POS = 2,

  ////////////////////////////////////////////////////////////////////////////
  /// @brief offsets, depends on positions
  ////////////////////////////////////////////////////////////////////////////
  OFFS = 4,

  ////////////////////////////////////////////////////////////////////////////
  /// @brief payload, depends on positions
  ////////////////////////////////////////////////////////////////////////////
  PAY = 8,

  ////////////////////////////////////////////////////////////////////////////
  /// @brief all features
  ////////////////////////////////////////////////////////////////////////////
  ALL = FREQ | POS | OFFS | PAY
}; // IndexFeatures

ENABLE_BITMASK_ENUM(IndexFeatures);

//////////////////////////////////////////////////////////////////////////////
/// @return true if 'lhs' is a subset of 'rhs'
//////////////////////////////////////////////////////////////////////////////
FORCE_INLINE bool is_subset_of(
    IndexFeatures lhs, IndexFeatures rhs) noexcept {
  return lhs == (lhs & rhs);
}

using feature_handler_f = void(*)(
  const field_stats&,
  doc_id_t,
  std::function<column_output&(doc_id_t)>&);

using field_features_t = std::map<type_info::type_id, feature_handler_f>;

}

#endif // IRESEARCH_INDEX_FEATURES_H
