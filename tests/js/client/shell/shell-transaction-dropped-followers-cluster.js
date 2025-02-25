/* jshint globalstrict:false, strict:false, maxlen: 200 */
/* global fail, assertEqual, assertNotEqual */

// //////////////////////////////////////////////////////////////////////////////
// / DISCLAIMER
// /
// / Copyright 2018 ArangoDB GmbH, Cologne, Germany
// /
// / Licensed under the Apache License, Version 2.0 (the "License")
// / you may not use this file except in compliance with the License.
// / You may obtain a copy of the License at
// /
// /     http://www.apache.org/licenses/LICENSE-2.0
// /
// / Unless required by applicable law or agreed to in writing, software
// / distributed under the License is distributed on an "AS IS" BASIS,
// / WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// / See the License for the specific language governing permissions and
// / limitations under the License.
// /
// / Copyright holder is triAGENS GmbH, Cologne, Germany
// /
// / @author Jan Steemann
// //////////////////////////////////////////////////////////////////////////////

const jsunity = require('jsunity');
const internal = require('internal');
const arangodb = require('@arangodb');
const _ = require('lodash');
const db = arangodb.db;
const { getEndpointById, getEndpointsByType, getMetric } = require('@arangodb/test-helper');
const request = require('@arangodb/request');

function transactionDroppedFollowersSuite() {
  'use strict';
  const cn = 'UnitTestsTransaction';

  return {

    setUp: function () {
      db._drop(cn);
    },

    tearDown: function () {
      db._drop(cn);
    },

    testTransactionWritesSameFollower: function () {
      let c = db._create(cn, { numberOfShards: 40, replicationFactor: 3 });
      let docs = [];
      for (let i = 0; i < 1000; ++i) { 
        docs.push({});
      }
      const opts = {
        collections: {
          write: [ cn  ]
        }
      };

      let shards = db._collection(cn ).shards(true);
      let servers = shards[Object.keys(shards)[0]];

      let droppedFollowers = {};
      servers.forEach((serverId) => {
        let endpoint = getEndpointById(serverId);
        droppedFollowers[serverId] = getMetric(endpoint, "arangodb_dropped_followers_total");
      });

      for (let i = 0; i < 50; ++i) { 
        const trx = db._createTransaction(opts);
        const tc = trx.collection(cn);
        tc.insert(docs);
        let result = trx.commit();
        assertEqual("committed", result.status);
      }

      // follower must not have been dropped
      servers.forEach((serverId) => {
        let endpoint = getEndpointById(serverId);
        assertEqual(droppedFollowers[serverId], getMetric(endpoint, "arangodb_dropped_followers_total"));
      });

      assertEqual(1000 * 50, c.count());
    },
    
    testTransactionExclusiveSameFollower: function () {
      let c = db._create(cn, { numberOfShards: 40, replicationFactor: 3 });
      let docs = [];
      for (let i = 0; i < 1000; ++i) { 
        docs.push({});
      }
      const opts = {
        collections: {
          exclusive: [ cn  ]
        }
      };

      let shards = db._collection(cn ).shards(true);
      let servers = shards[Object.keys(shards)[0]];

      let droppedFollowers = {};
      servers.forEach((serverId) => {
        let endpoint = getEndpointById(serverId);
        droppedFollowers[serverId] = getMetric(endpoint, "arangodb_dropped_followers_total");
      });

      for (let i = 0; i < 50; ++i) { 
        const trx = db._createTransaction(opts);
        const tc = trx.collection(cn);
        tc.insert(docs);
        let result = trx.commit();
        assertEqual("committed", result.status);
      }

      // follower must not have been dropped
      servers.forEach((serverId) => {
        let endpoint = getEndpointById(serverId);
        assertEqual(droppedFollowers[serverId], getMetric(endpoint, "arangodb_dropped_followers_total"));
      });
      
      assertEqual(1000 * 50, c.count());
    },

  };
}

let ep = getEndpointsByType('dbserver');
if (ep.length >= 3) {
  jsunity.run(transactionDroppedFollowersSuite);
}
return jsunity.done();
