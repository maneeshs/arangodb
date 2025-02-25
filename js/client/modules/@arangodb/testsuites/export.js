/* jshint strict: false, sub: true */
/* global print */
'use strict';

// //////////////////////////////////////////////////////////////////////////////
// / DISCLAIMER
// /
// / Copyright 2016 ArangoDB GmbH, Cologne, Germany
// / Copyright 2014 triagens GmbH, Cologne, Germany
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
// / Copyright holder is ArangoDB GmbH, Cologne, Germany
// /
// / @author Max Neunhoeffer
// //////////////////////////////////////////////////////////////////////////////

const functionsDocumentation = {
  'export': 'export formats tests'
};
const optionsDocumentation = [];

const fs = require('fs');
const pu = require('@arangodb/testutils/process-utils');
const tu = require('@arangodb/testutils/test-utils');
const xmldom = require('xmldom');
const zlib = require('zlib');

const CYAN = require('internal').COLORS.COLOR_CYAN;
const RESET = require('internal').COLORS.COLOR_RESET;

const toArgv = require('internal').toArgv;

const testPaths = {
  'export': [tu.pathForTesting('server/export')] // we have to be fuzzy...
};

// //////////////////////////////////////////////////////////////////////////////
// / @brief TEST: export
// //////////////////////////////////////////////////////////////////////////////

function exportTest (options) {
  const tmpPath = fs.join(options.testOutputDirectory, 'export');
  const DOMParser = new xmldom.DOMParser({
    locator: {},
    errorHandler: {
      warning: function (err) {
        xmlErrors = err;
      },
      error: function (err) {
        xmlErrors = err;
      },
      fatalError: function (err) {
        xmlErrors = err;
      }
    }
  });
  let xmlErrors = null;

  print(CYAN + 'export tests...' + RESET);

  const instanceInfo = pu.startInstance('tcp', options, {}, 'export');

  if (instanceInfo === false) {
    return {
      export: {
        status: false,
        message: 'failed to start server!'
      }
    };
  }

  print(CYAN + Date() + ': Setting up' + RESET);

  let results = {failed: 0};

  function shutdown () {
    print(CYAN + 'Shutting down...' + RESET);
    results['shutdown'] = pu.shutdownInstance(instanceInfo, options);
    print(CYAN + 'done.' + RESET);
    print();
    return results;
  }

  results.setup = tu.runInArangosh(options, instanceInfo, tu.makePathUnix(tu.pathForTesting('server/export/export-setup.js')));
  results.setup.failed = 0;
  if (!pu.arangod.check.instanceAlive(instanceInfo, options) || results.setup.status !== true) {
    results.setup.failed = 1;
    results.failed += 1;
    return shutdown();
  }

  let skipEncrypt = true;
  let keyfile = "";
  if (global.ARANGODB_CLIENT_VERSION) {
    let version = global.ARANGODB_CLIENT_VERSION(true);
    if (version.hasOwnProperty('enterprise-version')) {
      skipEncrypt = false;
      keyfile = fs.join(instanceInfo.rootDir, 'secret-key');
      fs.write(keyfile, 'DER-HUND-der-hund-der-hund-der-h'); // must be exactly 32 chars long
    }
  }
  
  let databases = [
    "UnitTestsExport",
    "اسم قاعدة بيانات يونيكود",
    "имя базы данных юникода !\"23 ää"
  ];

  // loop over different databases
  databases.forEach((name, idx) => {
    try {
      fs.removeDirectory(tmpPath);
    } catch (err) {}
    
    const args = {
      'configuration': fs.join(pu.CONFIG_DIR, 'arangoexport.conf'),
      'server.username': options.username,
      'server.password': options.password,
      'server.endpoint': instanceInfo.endpoint,
      'server.database': name,
      'collection': 'UnitTestsExport',
      'type': 'json',
      'overwrite': true,
      'output-directory': tmpPath
    };

    let testName;

    print(CYAN + Date() + ': Export data (json)' + RESET);
    testName = "exportJson" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
    results[testName].failed = results[testName].status ? 0 : 1;

    testName = "parseJson" + idx;
    try {
      JSON.parse(fs.read(fs.join(tmpPath, 'UnitTestsExport.json')));
      results[testName] = {
        failed: 0,
        status: true
      };
    } catch (e) {
      results.failed += 1;
      results[testName] = {
        failed: 1,
        status: false,
        message: e
      };
    }

    print(CYAN + Date() + ': Export data (json.gz)' + RESET);
    args['compress-output'] = 'true';
    testName = "exportJsonGz" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
    results[testName].failed = results[testName].status ? 0 : 1;

    testName = "parseJsonGz" + idx;
    try {
      const zipBuffer = fs.readGzip(fs.join(tmpPath, 'UnitTestsExport.json.gz'));
      JSON.parse(zipBuffer);
      results[testName] = {
        failed: 0,
        status: true
      };
    } catch (e) {
      results.failed += 1;
      results[testName] = {
        failed: 1,
        status: false,
        message: e
      };
    }
    args['compress-output'] = 'false';

    if (!skipEncrypt) {
      print(CYAN + Date() + ': Export data (json encrypt)' + RESET);
      args['encryption.keyfile'] = keyfile;
      if (fs.exists(fs.join(tmpPath, 'ENCRYPTION'))) {
        fs.remove(fs.join(tmpPath, 'ENCRYPTION'));
      }
    
      testName = "exportJsonEncrypt" + idx;
      results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
      results[testName].failed = results[testName].status ? 0 : 1;

      testName = "parseJsonEncrypt" + idx;
      try {
        const decBuffer = fs.readDecrypt(fs.join(tmpPath, 'UnitTestsExport.json'), keyfile);
        JSON.parse(decBuffer);
        results[testName] = {
          failed: 0,
          status: true
        };
      } catch (e) {
        results.failed += 1;
        results[testName] = {
          failed: 1,
          status: false,
          message: e
        };
      }
      delete args['encryption.keyfile'];
      if (fs.exists(fs.join(tmpPath, 'ENCRYPTION'))) {
        fs.remove(fs.join(tmpPath, 'ENCRYPTION'));
      }
    }

    print(CYAN + Date() + ': Export data (jsonl)' + RESET);
    args['type'] = 'jsonl';
      
    testName = "exportJsonl" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
    results[testName].failed = results[testName].status ? 0 : 1;
    
    testName = "parseJsonl" + idx;
    try {
      fs.read(fs.join(tmpPath, 'UnitTestsExport.jsonl')).split('\n')
      .filter(line => line.trim() !== '')
      .forEach(line => JSON.parse(line));

      results[testName] = {
        failed: 0,
        status: true
      };
    } catch (e) {
      results.failed += 1;
      results[testName] = {
        failed: 1,
        status: false,
        message: e
      };
    }

    print(CYAN + Date() + ': Export data (jsonl.gz)' + RESET);
    args['compress-output'] = 'true';
    testName = "exportJsonlGz" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
    results[testName].failed = results[testName].status ? 0 : 1;
    
    testName = "parseJsonlGz" + idx;
    try {
      fs.readGzip(fs.join(tmpPath, 'UnitTestsExport.jsonl.gz')).split('\n')
      .filter(line => line.trim() !== '')
      .forEach(line => JSON.parse(line));

      results[testName] = {
        failed: 0,
        status: true
      };
    } catch (e) {
      results.failed += 1;
      results[testName] = {
        failed: 1,
        status: false,
        message: e
      };
    }
    args['compress-output'] = 'false';

    print(CYAN + Date() + ': Export data (xgmml)' + RESET);
    args['type'] = 'xgmml';
    args['graph-name'] = 'UnitTestsExport';
    testName = "exportXgmml" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
    results[testName].failed = results[testName].status ? 0 : 1;
    
    testName = "parseXgmml" + idx;
    try {
      const filesContent = fs.read(fs.join(tmpPath, 'UnitTestsExport.xgmml'));
      DOMParser.parseFromString(filesContent);
      results[testName] = {
        failed: 0,
        status: true
      };

      if (xmlErrors !== null) {
        results[testName] = {
          failed: 1,
          status: false,
          message: xmlErrors
        };
      }
    } catch (e) {
      results.failed += 1;
      results.parseXgmml = {
        failed: 1,
        status: false,
        message: e
      };
    }

    print(CYAN + Date() + ': Export data (xgmml.gz)' + RESET);
    args['compress-output'] = 'true';
    testName = "exportXgmmlGz" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
    results[testName].failed = results[testName].status ? 0 : 1;
    
    testName = "parseXgmmlGz" + idx;
    try {
      const filesContent = fs.readGzip(fs.join(tmpPath, 'UnitTestsExport.xgmml.gz'));
      DOMParser.parseFromString(filesContent);
      results[testName] = {
        failed: 0,
        status: true
      };

      if (xmlErrors !== null) {
        results[testName] = {
          failed: 1,
          status: false,
          message: xmlErrors
        };
      }
    } catch (e) {
      results.failed += 1;
      results[testName] = {
        failed: 1,
        status: false,
        message: e
      };
    }
    args['compress-output'] = 'false';

    print(CYAN + Date() + ': Export query (jsonl)' + RESET);
    args['type'] = 'jsonl';
    args['query'] = 'FOR doc IN UnitTestsExport RETURN doc';
    delete args['graph-name'];
    delete args['collection'];
    
    testName = "exportQuery" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
    results[testName].failed = results[testName].status ? 0 : 1;
    
    testName = "parseQuery" + idx;
    try {
      fs.read(fs.join(tmpPath, 'query.jsonl')).split('\n')
      .filter(line => line.trim() !== '')
      .forEach(line => JSON.parse(line));
      results[testName] = {
        failed: 0,
        status: true
      };
    } catch (e) {
      print(e);
      results.failed += 1;
      results[testName] = {
        failed: 1,
        status: false,
        message: e
      };
    }

    print(CYAN + Date() + ': Export query (jsonl.gz)' + RESET);
    args['compress-output'] = 'true';
    testName = "exportQueryGz" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
    results[testName].failed = results[testName].status ? 0 : 1;
    
    testName = "parseQueryGz" + idx;
    try {
      fs.readGzip(fs.join(tmpPath, 'query.jsonl.gz')).split('\n')
      .filter(line => line.trim() !== '')
      .forEach(line => JSON.parse(line));
      results[testName] = {
        failed: 0,
        status: true
      };
    } catch (e) {
      results.failed += 1;
      results[testName] = {
        failed: 1,
        status: false,
        message: e
      };
    }
    args['compress-output'] = 'false';
    
    print(CYAN + Date() + ': Export data (csv)' + RESET);
    args['type'] = 'csv';
    args['query'] = 'FOR doc IN UnitTestsExport RETURN doc';
    args['fields'] = '_key,value1,value2,value3,value4';
    
    testName = "exportCsv" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, false, options.coreCheck);
    results[testName].failed = results[testName].status ? 0 : 1;
    
    testName = "parseCsv" + idx;
    try {
      fs.read(fs.join(tmpPath, 'query.csv'));

      results[testName] = {
        failed: 0,
        status: true
      };
    } catch (e) {
      results.failed += 1;
      results[testName] = {
        failed: 1,
        status: false,
        message: e
      };
    }
    delete args['fields'];
    
    print(CYAN + Date() + ': Export query (maxRuntime, failure)' + RESET);
    args['type'] = 'jsonl';
    args['query'] = 'RETURN SLEEP(4)';
    args['query-max-runtime'] = '2.0';

    testName = "exportQueryMaxRuntimeFail" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
    // we expect a failure here!
    results[testName].status = !results[testName].status;
    results[testName].failed = results[testName].status ? 0 : 1;
    delete args['query-max-runtime'];
    
    print(CYAN + Date() + ': Export query (maxRuntime, ok)' + RESET);
    args['type'] = 'jsonl';
    args['query'] = 'RETURN SLEEP(3)';
    args['query-max-runtime'] = '20.0';
    testName = "exportQueryMaxRuntimeOk" + idx;
    results[testName] = pu.executeAndWait(pu.ARANGOEXPORT_BIN, toArgv(args), options, 'arangosh', tmpPath, options.coreCheck);
    results[testName].failed = results[testName].status ? 0 : 1;
    delete args['query-max-runtime'];
  });

  return shutdown();
}

exports.setup = function (testFns, defaultFns, opts, fnDocs, optionsDoc, allTestPaths) {
  Object.assign(allTestPaths, testPaths);
  testFns['export'] = exportTest;
  defaultFns.push('export');
  for (var attrname in functionsDocumentation) { fnDocs[attrname] = functionsDocumentation[attrname]; }
  for (var i = 0; i < optionsDocumentation.length; i++) { optionsDoc.push(optionsDocumentation[i]); }
};
