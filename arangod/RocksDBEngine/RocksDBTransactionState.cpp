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
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#include "RocksDBTransactionState.h"

#include "ApplicationFeatures/ApplicationServer.h"
#include "Aql/QueryCache.h"
#include "Basics/Exceptions.h"
#include "Basics/Result.h"
#include "Basics/system-compiler.h"
#include "Basics/system-functions.h"
#include "Cache/CacheManagerFeature.h"
#include "Cache/Manager.h"
#include "Cache/Transaction.h"
#include "Logger/LogMacros.h"
#include "Logger/Logger.h"
#include "Logger/LoggerStream.h"
#include "Random/RandomGenerator.h"
#include "RestServer/MetricsFeature.h"
#include "RocksDBEngine/Methods/RocksDBReadOnlyMethods.h"
#include "RocksDBEngine/Methods/RocksDBTrxMethods.h"
#include "RocksDBEngine/Methods/RocksDBSingleOperationReadOnlyMethods.h"
#include "RocksDBEngine/Methods/RocksDBSingleOperationTrxMethods.h"
#include "RocksDBEngine/RocksDBCollection.h"
#include "RocksDBEngine/RocksDBColumnFamilyManager.h"
#include "RocksDBEngine/RocksDBCommon.h"
#include "RocksDBEngine/RocksDBEngine.h"
#include "RocksDBEngine/RocksDBLogValue.h"
#include "RocksDBEngine/RocksDBMethods.h"
#include "RocksDBEngine/RocksDBTransactionCollection.h"
#include "Statistics/ServerStatistics.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "StorageEngine/StorageEngine.h"
#include "StorageEngine/TransactionCollection.h"
#include "Transaction/Context.h"
#include "Transaction/Manager.h"
#include "Transaction/ManagerFeature.h"
#include "Transaction/Methods.h"
#include "Utils/ExecContext.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/ticks.h"
#include "VocBase/vocbase.h"

#include <rocksdb/options.h>
#include <rocksdb/status.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/utilities/transaction_db.h>
#include <rocksdb/utilities/write_batch_with_index.h>
#include <atomic>
#include <cstddef>
#include <memory>

using namespace arangodb;

/// @brief transaction type
RocksDBTransactionState::RocksDBTransactionState(TRI_vocbase_t& vocbase, TransactionId tid,
                                                 transaction::Options const& options)
    : TransactionState(vocbase, tid, options),
      _cacheTx(nullptr),
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
      _users(0),
#endif
      _parallel(false) {}

/// @brief free a transaction container
RocksDBTransactionState::~RocksDBTransactionState() {
  cleanupTransaction();
  _status = transaction::Status::ABORTED;
}

#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
void RocksDBTransactionState::use() noexcept {
  TRI_ASSERT(_users.fetch_add(1, std::memory_order_relaxed) == 0);
}

void RocksDBTransactionState::unuse() noexcept {
  TRI_ASSERT(_users.fetch_sub(1, std::memory_order_relaxed) == 1); 
}
#endif

/// @brief start a transaction
Result RocksDBTransactionState::beginTransaction(transaction::Hints hints) {
  LOG_TRX("0c057", TRACE, this)
      << "beginning " << AccessMode::typeString(_type) << " transaction";

  TRI_ASSERT(!hasHint(transaction::Hints::Hint::NO_USAGE_LOCK) ||
             !AccessMode::isWriteOrExclusive(_type));

  _hints = hints;  // set hints before useCollections

  auto& stats = statistics();

  Result res;
  if (isReadOnlyTransaction()) {
    // for read-only transactions there will be no locking. so we will not
    // even call TRI_microtime() to save some cycles
    res = useCollections();
  } else {
    // measure execution time of "useCollections" operation, which is
    // responsible for acquring locks as well
    double start = TRI_microtime();
    res = useCollections();
    
    double diff = TRI_microtime() - start;
    stats._lockTimeMicros += static_cast<uint64_t>(1000000.0 * diff);
    stats._lockTimes.count(diff);
  }
  
  if (res.fail()) {
    // something is wrong
    updateStatus(transaction::Status::ABORTED);
    return res;
  }

  // register with manager
  transaction::ManagerFeature::manager()->registerTransaction(id(), isReadOnlyTransaction(), hasHint(transaction::Hints::Hint::IS_FOLLOWER_TRX));
  updateStatus(transaction::Status::RUNNING);
  if (isReadOnlyTransaction()) {
    ++stats._readTransactions;
  } else {
    ++stats._transactionsStarted;
  }

  setRegistered();

  TRI_ASSERT(_cacheTx == nullptr);

  // start cache transaction
  auto* manager = vocbase().server().getFeature<CacheManagerFeature>().manager();
  if (manager != nullptr) {
    _cacheTx = manager->beginTransaction(isReadOnlyTransaction());
  }

  auto& selector = vocbase().server().getFeature<EngineSelectorFeature>();
  auto& engine = selector.engine<RocksDBEngine>();
  rocksdb::TransactionDB* db = engine.db();

  if (isReadOnlyTransaction()) {
    if (isSingleOperation()) {
      _rocksMethods = std::make_unique<RocksDBSingleOperationReadOnlyMethods>(this, db);
    } else {
      _rocksMethods = std::make_unique<RocksDBReadOnlyMethods>(this, db);
    }
  } else {
    if (isSingleOperation()) {
      _rocksMethods = std::make_unique<RocksDBSingleOperationTrxMethods>(this, db);
    } else {
      _rocksMethods = std::make_unique<RocksDBTrxMethods>(this, db);
    }
  }
  
  res = _rocksMethods->beginTransaction();
  if (res.ok()) {
    maybeDisableIndexing();
  }
  
  return res;
}

void RocksDBTransactionState::maybeDisableIndexing() {
  if (!hasHint(transaction::Hints::Hint::NO_INDEXING)) {
    return;
  }
  
  TRI_ASSERT(!isReadOnlyTransaction());
  // do not track our own writes... we can only use this in very
  // specific scenarios, i.e. when we are sure that we will have a
  // single operation transaction or we are sure we are writing
  // unique keys

  // we must check if there is a unique secondary index for any of the
  // collections we write into in case it is, we must disable NO_INDEXING
  // here, as it wouldn't be safe
  bool disableIndexing = true;

  for (auto& trxCollection : _collections) {
    if (!AccessMode::isWriteOrExclusive(trxCollection->accessType())) {
      continue;
    }
    auto indexes = trxCollection->collection()->getIndexes();
    for (auto const& idx : indexes) {
      if (idx->type() == Index::IndexType::TRI_IDX_TYPE_PRIMARY_INDEX) {
        // primary index is unique, but we can ignore it here.
        // we are only looking for secondary indexes
        continue;
      }
      if (idx->unique()) {
        // found secondary unique index. we need to turn off the
        // NO_INDEXING optimization now
        disableIndexing = false;
        break;
      }
    }
  }

  if (disableIndexing) {
    // only turn it on when safe...
    _rocksMethods->DisableIndexing();
  }
}

rocksdb::SequenceNumber RocksDBTransactionState::prepareCollections() {
  auto& engine = vocbase().server().getFeature<EngineSelectorFeature>().engine<RocksDBEngine>();
  rocksdb::TransactionDB* db = engine.db();

  rocksdb::SequenceNumber preSeq = db->GetLatestSequenceNumber();

  for (auto& trxColl : _collections) {
    auto* coll = static_cast<RocksDBTransactionCollection*>(trxColl);

    rocksdb::SequenceNumber seq = coll->prepareTransaction(id());
    preSeq = std::max(seq, preSeq);
  }

  return preSeq;
}

void RocksDBTransactionState::commitCollections(rocksdb::SequenceNumber lastWritten) {
  TRI_ASSERT(lastWritten > 0);
  for (auto& trxColl : _collections) {
    auto* coll = static_cast<RocksDBTransactionCollection*>(trxColl);
    // we need this in case of an intermediate commit. The number of
    // initial documents is adjusted and numInserts / removes is set to 0
    // index estimator updates are buffered
    coll->commitCounts(id(), lastWritten);
  }
}

void RocksDBTransactionState::cleanupCollections() {
  for (auto& trxColl : _collections) {
    auto* coll = static_cast<RocksDBTransactionCollection*>(trxColl);
    coll->abortCommit(id());
  }
}

void RocksDBTransactionState::cleanupTransaction() noexcept {
  if (_cacheTx != nullptr) {
    // note: endTransaction() will delete _cacheTrx!
    auto* manager = vocbase().server().getFeature<CacheManagerFeature>().manager();
    TRI_ASSERT(manager != nullptr);
    manager->endTransaction(_cacheTx);
    _cacheTx = nullptr;
  }
}

/// @brief commit a transaction
Result RocksDBTransactionState::commitTransaction(transaction::Methods* activeTrx) {
  LOG_TRX("5cb03", TRACE, this)
      << "committing " << AccessMode::typeString(_type) << " transaction";

  TRI_ASSERT(_status == transaction::Status::RUNNING);
  TRI_ASSERT(activeTrx->isMainTransaction());
  TRI_IF_FAILURE("TransactionWriteCommitMarker") {
    return Result(TRI_ERROR_DEBUG);
  }

  arangodb::Result res = _rocksMethods->commitTransaction();
  if (res.ok()) {
    updateStatus(transaction::Status::COMMITTED);
    cleanupTransaction();  // deletes trx
    ++statistics()._transactionsCommitted;
  } else {
    abortTransaction(activeTrx);  // deletes trx
  }
  TRI_ASSERT(!_cacheTx);

  return res;
  
}

/// @brief abort and rollback a transaction
Result RocksDBTransactionState::abortTransaction(transaction::Methods* activeTrx) {
  LOG_TRX("5b226", TRACE, this) << "aborting " << AccessMode::typeString(_type) << " transaction";
  TRI_ASSERT(_status == transaction::Status::RUNNING);
  TRI_ASSERT(activeTrx->isMainTransaction());
  
  Result result = _rocksMethods->abortTransaction();

  cleanupTransaction();  // deletes trx

  updateStatus(transaction::Status::ABORTED);
  if (hasOperations()) {
    // must clean up the query cache because the transaction
    // may have queried something via AQL that is now rolled back
    clearQueryCache();
  }
  TRI_ASSERT(!_cacheTx);
  ++statistics()._transactionsAborted;

  return result;
}

void RocksDBTransactionState::beginQuery(bool isModificationQuery) {
  auto* trxMethods = dynamic_cast<RocksDBTrxMethods*>(_rocksMethods.get());
  if (trxMethods) {
    trxMethods->beginQuery(isModificationQuery);
  }
}

void RocksDBTransactionState::endQuery(bool isModificationQuery) noexcept {
  auto* trxMethods = dynamic_cast<RocksDBTrxMethods*>(_rocksMethods.get());
  if (trxMethods) {
    trxMethods->endQuery(isModificationQuery);
  }
}

/// @brief whether or not a RocksDB iterator in this transaction must check its
/// bounds during iteration in addition to setting iterator_lower_bound or
/// iterate_upper_bound. this is currently true for all iterators that are based
/// on in-flight writes of the current transaction. it is never necessary to
/// check bounds for read-only transactions
bool RocksDBTransactionState::iteratorMustCheckBounds(ReadOwnWrites readOwnWrites) const {
  return _rocksMethods->iteratorMustCheckBounds(readOwnWrites);
}

TRI_voc_tick_t RocksDBTransactionState::lastOperationTick() const noexcept {
  return _rocksMethods->lastOperationTick();
}
  
uint64_t RocksDBTransactionState::numCommits() const {
  return _rocksMethods->numCommits();
}

bool RocksDBTransactionState::hasOperations() const noexcept {
  return _rocksMethods->hasOperations();
}

uint64_t RocksDBTransactionState::numOperations() const noexcept {
  return _rocksMethods->numOperations();
}

void RocksDBTransactionState::prepareOperation(DataSourceId cid, RevisionId rid,
                                               TRI_voc_document_operation_e operationType) {
  _rocksMethods->prepareOperation(cid, rid, operationType);
}

/// @brief undo the effects of the previous prepareOperation call
void RocksDBTransactionState::rollbackOperation(TRI_voc_document_operation_e operationType) {
  _rocksMethods->rollbackOperation(operationType);
}

/// @brief add an operation for a transaction collection
Result RocksDBTransactionState::addOperation(DataSourceId cid, RevisionId revisionId,
                                             TRI_voc_document_operation_e operationType,
                                             bool& hasPerformedIntermediateCommit) {
  Result result = _rocksMethods->addOperation(cid, revisionId, operationType);

  if (result.ok()) {
    auto tcoll = static_cast<RocksDBTransactionCollection*>(findCollection(cid));
    if (tcoll == nullptr) {
      std::string message = "collection '" + std::to_string(cid.id()) +
                            "' not found in transaction state";
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_INTERNAL, message);
    }

    // should not fail or fail with exception
    tcoll->addOperation(operationType, revisionId);

    // clear the query cache for this collection
    auto queryCache = arangodb::aql::QueryCache::instance();

    if (queryCache->mayBeActive() && tcoll->collection()) {
      queryCache->invalidate(&_vocbase, tcoll->collection()->guid());
    }
    
    result = _rocksMethods->checkIntermediateCommit(hasPerformedIntermediateCommit);
  }
  return result;
}

bool RocksDBTransactionState::ensureSnapshot() {
  return _rocksMethods->ensureSnapshot();
}

RocksDBTransactionCollection::TrackedOperations& RocksDBTransactionState::trackedOperations(DataSourceId cid) {
  auto col = findCollection(cid);
  TRI_ASSERT(col != nullptr);
  return static_cast<RocksDBTransactionCollection*>(col)->trackedOperations();
}

void RocksDBTransactionState::trackInsert(DataSourceId cid, RevisionId rid) {
  auto col = findCollection(cid);
  if (col != nullptr) {
    static_cast<RocksDBTransactionCollection*>(col)->trackInsert(rid);
  } else {
    TRI_ASSERT(false);
  }
}

void RocksDBTransactionState::trackRemove(DataSourceId cid, RevisionId rid) {
  auto col = findCollection(cid);
  if (col != nullptr) {
    static_cast<RocksDBTransactionCollection*>(col)->trackRemove(rid);
  } else {
    TRI_ASSERT(false);
  }
}

void RocksDBTransactionState::trackIndexInsert(DataSourceId cid, IndexId idxId, uint64_t hash) {
  auto col = findCollection(cid);
  if (col != nullptr) {
    static_cast<RocksDBTransactionCollection*>(col)->trackIndexInsert(idxId, hash);
  } else {
    TRI_ASSERT(false);
  }
}

void RocksDBTransactionState::trackIndexRemove(DataSourceId cid, IndexId idxId, uint64_t hash) {
  auto col = findCollection(cid);
  if (col != nullptr) {
    static_cast<RocksDBTransactionCollection*>(col)->trackIndexRemove(idxId, hash);
  } else {
    TRI_ASSERT(false);
  }
}

bool RocksDBTransactionState::isOnlyExclusiveTransaction() const {
  if (!AccessMode::isWriteOrExclusive(_type)) {
    return false;
  }
  for (TransactionCollection* coll : _collections) {
    if (AccessMode::isWrite(coll->accessType())) {
      return false;
    }
  }
  return true;
}

rocksdb::SequenceNumber RocksDBTransactionState::beginSeq() const {
  return _rocksMethods->GetSequenceNumber();
}

#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
RocksDBTransactionStateGuard::RocksDBTransactionStateGuard(RocksDBTransactionState* state) noexcept
    : _state(state) {
  _state->use();
}

RocksDBTransactionStateGuard::~RocksDBTransactionStateGuard() {
  _state->unuse();
}
#endif

/// @brief constructor, leases a builder
RocksDBKeyLeaser::RocksDBKeyLeaser(transaction::Methods* trx)
    : _ctx(trx->transactionContextPtr()),
      _key(RocksDBTransactionState::toState(trx)->inParallelMode() ? nullptr : _ctx->leaseString()) {
  TRI_ASSERT(_ctx != nullptr);
  TRI_ASSERT(_key.buffer() != nullptr);
}

/// @brief destructor
RocksDBKeyLeaser::~RocksDBKeyLeaser() {
  if (!_key.usesInlineBuffer()) {
    _ctx->returnString(_key.buffer());
  }
}
