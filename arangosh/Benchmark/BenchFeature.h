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

#pragma once

#include <atomic>

#include "ApplicationFeatures/ApplicationFeature.h"

#include <velocypack/velocypack-aliases.h>

namespace arangodb {
namespace arangobench {
struct BenchmarkStats;
}

class ClientFeature;

struct BenchRunResult {
  double _time;
  uint64_t _failures;
  uint64_t _incomplete;
  double _requestTime;

  void update(double time, uint64_t failures, uint64_t incomplete, double requestTime) {
    _time = time;
    _failures = failures;
    _incomplete = incomplete;
    _requestTime = requestTime;
  }
};

class BenchFeature final : public application_features::ApplicationFeature {
 public:
  BenchFeature(application_features::ApplicationServer& server, int* result);

  void collectOptions(std::shared_ptr<options::ProgramOptions>) override;
  void start() override final;

  bool async() const { return _async; }
  uint64_t concurrency() const { return _concurrency; }
  uint64_t operations() const { return _operations; }
  uint64_t batchSize() const { return _batchSize; }
  bool keepAlive() const { return _keepAlive; }
  std::string const& collection() const { return _collection; }
  std::string const& testCase() const { return _testCase; }
  uint64_t complexity() const { return _complexity; }
  bool delay() const { return _delay; }
  bool progress() const { return _progress; }
  bool verbose() const { return _verbose; }
  bool quiet() const { return _quiet; }
  uint64_t runs() const { return _runs; }
  std::string const& junitReportFile() const { return _junitReportFile; }
  uint64_t replicationFactor() const { return _replicationFactor; }
  uint64_t numberOfShards() const { return _numberOfShards; }
  bool waitForSync() const { return _waitForSync; }
  
  std::string const& customQuery() const { return _customQuery; }
  std::string const& customQueryFile() const { return _customQueryFile; }

 private:
  void status(std::string const& value);
  bool report(ClientFeature&, std::vector<BenchRunResult>, arangobench::BenchmarkStats const& stats, std::string const& histogram, VPackBuilder& builder);
  void printResult(BenchRunResult const& result, VPackBuilder& builder);
  bool writeJunitReport(BenchRunResult const& result);

  uint64_t _concurrency;
  uint64_t _operations;
  uint64_t _realOperations;
  uint64_t _batchSize;
  uint64_t _duration;
  std::string _collection;
  std::string _testCase;
  uint64_t _complexity;
  bool _async;
  bool _keepAlive;
  bool _createDatabase;
  bool _delay;
  bool _progress;
  bool _verbose;
  bool _quiet;
  bool _waitForSync;
  uint64_t _runs;
  std::string _junitReportFile;
  std::string _jsonReportFile;
  uint64_t _replicationFactor;
  uint64_t _numberOfShards;
  
  std::string _customQuery;
  std::string _customQueryFile;

  int* _result;

  uint64_t _histogramNumIntervals;
  double _histogramIntervalSize;
  std::vector<double> _percentiles;
  
  static void updateStartCounter();
  static int getStartCounter();

  static std::atomic<int> _started;
};

}  // namespace arangodb

