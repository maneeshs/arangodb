////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2021 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <rocksdb/options.h>

#include <velocypack/Iterator.h>
#include <velocypack/Slice.h>

#include "Indexes/Index.h"
#include "Indexes/IndexIterator.h"
#include "RocksDBEngine/RocksDBKeyBounds.h"

namespace rocksdb {
class Iterator;
class Comparator;
class TransactionDB;
}  // namespace rocksdb

namespace arangodb {
class RocksDBCollection;
class RocksDBPrimaryIndex;

/// @brief iterator over all documents in the collection
/// basically sorted after LocalDocumentId
class RocksDBAllIndexIterator final : public IndexIterator {
 public:
  RocksDBAllIndexIterator(LogicalCollection* collection, transaction::Methods* trx, ReadOwnWrites readOwnWrites);
  ~RocksDBAllIndexIterator() = default;

  char const* typeName() const override { return "all-index-iterator"; }
  
  /// @brief index does not support rearming
  bool canRearm() const override { return false; }

  bool nextImpl(LocalDocumentIdCallback const& cb, size_t limit) override;
  bool nextDocumentImpl(DocumentCallback const& cb, size_t limit) override;
  void skipImpl(uint64_t count, uint64_t& skipped) override;
  void resetImpl() override;

 private:
  bool outOfRange() const;
  void ensureIterator();

  RocksDBKeyBounds const _bounds;
  rocksdb::Slice const _upperBound;  // used for iterate_upper_bound
  std::unique_ptr<rocksdb::Iterator> _iterator;
  rocksdb::Comparator const* _cmp;
  // we use _mustSeek to save repeated seeks for the same start key
  bool _mustSeek;
  bool const _mustCheckBounds;
};

class RocksDBAnyIndexIterator final : public IndexIterator {
 public:
  RocksDBAnyIndexIterator(LogicalCollection* collection, transaction::Methods* trx);
  ~RocksDBAnyIndexIterator() = default;

  char const* typeName() const override { return "any-index-iterator"; }

  bool nextImpl(LocalDocumentIdCallback const& cb, size_t limit) override;
  bool nextDocumentImpl(DocumentCallback const& cb, size_t limit) override;
  // cppcheck-suppress virtualCallInConstructor ; desired impl
  void resetImpl() override;

 private:
  template <typename Func>
  bool doNext(size_t limit, Func const& func);
  
  bool outOfRange() const;
  bool checkIter();

  rocksdb::Comparator const* _cmp;
  std::unique_ptr<rocksdb::Iterator> _iterator;
  uint64_t const _objectId;
  RocksDBKeyBounds const _bounds;

  uint64_t _total;
  uint64_t _returned;
  bool _forward;
};
  
  
/// @brief return false to stop iteration
typedef std::function<bool(rocksdb::Slice const& key, rocksdb::Slice const& value)> GenericCallback;

/// @brief a forward-only iterator over the primary index, only reading from the
/// database, not taking into account changes done in the current transaction
class RocksDBGenericIterator {
 public:
  RocksDBGenericIterator(rocksdb::TransactionDB* db, rocksdb::ReadOptions& options,
                         RocksDBKeyBounds const& bounds);
  RocksDBGenericIterator(RocksDBGenericIterator&&) = default;

  ~RocksDBGenericIterator() = default;

  //* The following functions returns true if the iterator is valid within bounds on return.
  //  @param limit - number of documents the callback should be applied to
  bool next(GenericCallback const& cb, size_t limit);

  bool seek(rocksdb::Slice const& key);
  bool hasMore() const;

  // return bounds
  RocksDBKeyBounds const& bounds() const { return _bounds; }

 private:
  bool outOfRange() const;

  RocksDBKeyBounds const _bounds;
  rocksdb::ReadOptions const _options;
  std::unique_ptr<rocksdb::Iterator> _iterator;
  rocksdb::Comparator const* _cmp;
};

RocksDBGenericIterator createPrimaryIndexIterator(transaction::Methods* trx,
                                                  LogicalCollection* col);
}  // namespace arangodb
