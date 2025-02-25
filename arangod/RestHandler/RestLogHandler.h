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
/// @author Lars Maier
////////////////////////////////////////////////////////////////////////////////
#ifndef ARANGODB3_RESTLOGHANDLER_H
#define ARANGODB3_RESTLOGHANDLER_H

#include "Basics/Common.h"
#include "RestHandler/RestVocbaseBaseHandler.h"

namespace arangodb {

struct ReplicatedLogMethods;

class RestLogHandler : public RestVocbaseBaseHandler {
 public:
  RestLogHandler(application_features::ApplicationServer&, GeneralRequest*,
                      GeneralResponse*);
  ~RestLogHandler() override;

 public:
  RestStatus execute() final;
  char const* name() const final { return "RestLogHandler"; }
  RequestLane lane() const final {
    return RequestLane::CLIENT_SLOW;
  }

 private:
  RestStatus executeByMethod(ReplicatedLogMethods const& methods);
  RestStatus handleGetRequest(ReplicatedLogMethods const& methods);
  RestStatus handlePostRequest(ReplicatedLogMethods const& methods);
  RestStatus handleDeleteRequest(ReplicatedLogMethods const& methods);

  RestStatus handleGet(ReplicatedLogMethods const& methods);
  RestStatus handleGetTail(ReplicatedLogMethods const& methods, replication2::LogId);
  RestStatus handleGetLog(ReplicatedLogMethods const& methods, replication2::LogId);
  RestStatus handleGetReadEntry(ReplicatedLogMethods const& methods, replication2::LogId);

};
}  // namespace arangodb
#endif  // ARANGODB3_RESTLOGHANDLER_H
