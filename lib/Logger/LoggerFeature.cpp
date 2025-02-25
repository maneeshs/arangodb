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
/// @author Dr. Frank Celler
////////////////////////////////////////////////////////////////////////////////

#include "Basics/operating-system.h"

#ifdef ARANGODB_HAVE_GETGRGID
#include <grp.h>
#endif

#ifdef TRI_HAVE_UNISTD_H
#include <unistd.h>
#endif

#if _WIN32
#include <iostream>
#include "Basics/win-utils.h"
#endif

#include "LoggerFeature.h"

#include "ApplicationFeatures/ApplicationServer.h"
#include "ApplicationFeatures/ShellColorsFeature.h"
#include "ApplicationFeatures/VersionFeature.h"
#include "Basics/StringUtils.h"
#include "Basics/Thread.h"
#include "Basics/application-exit.h"
#include "Basics/conversions.h"
#include "Basics/error.h"
#include "Basics/voc-errors.h"
#include "Logger/LogAppender.h"
#include "Logger/LogAppenderFile.h"
#include "Logger/LogMacros.h"
#include "Logger/LogTimeFormat.h"
#include "Logger/Logger.h"
#include "Logger/LoggerStream.h"
#include "ProgramOptions/Option.h"
#include "ProgramOptions/Parameters.h"
#include "ProgramOptions/ProgramOptions.h"

using namespace arangodb::basics;
using namespace arangodb::options;

// Please leave this code in for the next time we have to debug fuerte.
#if 0
void LogHackWriter(char const* p) {
  LOG_DEVEL << p;
}
#endif

namespace arangodb {

LoggerFeature::LoggerFeature(application_features::ApplicationServer& server, 
                             bool threaded)
    : ApplicationFeature(server, "Logger"),
      _timeFormatString(LogTimeFormats::defaultFormatName()),
      _threaded(threaded) {

  // note: we use the _threaded option to determine whether we are arangod
  // (_threaded = true) or one of the client tools (_threaded = false). in
  // the latter case we disable some options for the Logger, which only make
  // sense when we are running in server mode
  setOptional(false);

  startsAfter<ShellColorsFeature>();
  startsAfter<VersionFeature>();

  _levels.push_back("info");

  // if stdout is a tty, then the default for _foregroundTty becomes true
  _foregroundTty = (isatty(STDOUT_FILENO) == 1);
}

LoggerFeature::~LoggerFeature() {
  Logger::shutdown();
}

void LoggerFeature::collectOptions(std::shared_ptr<ProgramOptions> options) {
  options->addOldOption("log.tty", "log.foreground-tty");
  options->addOldOption("log.escape", "log.escape-control-chars");

  options
      ->addOption("--log", "the global or topic-specific log level",
                  new VectorParameter<StringParameter>(&_levels),
                  arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden))
      .setDeprecatedIn(30500);

  options->addSection("log", "logging");

  options->addOption("--log.color", "use colors for TTY logging",
                     new BooleanParameter(&_useColor),
                     arangodb::options::makeDefaultFlags(arangodb::options::Flags::Dynamic));

  options->addOption("--log.escape-control-chars", "escape control characters when logging",
                     new BooleanParameter(&_useControlEscaped))
                     .setIntroducedIn(30900);
  options->addOption("--log.escape-unicode-chars", "escape unicode characters when logging",
                     new BooleanParameter(&_useUnicodeEscaped))
                     .setIntroducedIn(30900);

  options->addOption(
      "--log.output,-o",
      "log destination(s), e.g. "
#ifdef _WIN32
      "file://C:\\path\\to\\file"
#else
      "file:///path/to/file"
#endif
      " (any '$PID' will be replaced with the process id)",
      new VectorParameter<StringParameter>(&_output));

  options->addOption("--log.level,-l", "the global or topic-specific log level",
                     new VectorParameter<StringParameter>(&_levels));
  
  options
      ->addOption("--log.max-entry-length", "maximum length of a log entry (in bytes)",
                  new UInt32Parameter(&_maxEntryLength))
      .setIntroducedIn(30709);

  options
      ->addOption("--log.use-local-time", "use local timezone instead of UTC",
                  new BooleanParameter(&_useLocalTime),
                  arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden))
      .setDeprecatedIn(30500);

  options
      ->addOption("--log.use-microtime", "use microtime instead",
                  new BooleanParameter(&_useMicrotime),
                  arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden))
      .setDeprecatedIn(30500);

  options
      ->addOption("--log.time-format", "time format to use in logs",
                  new DiscreteValuesParameter<StringParameter>(
                      &_timeFormatString, LogTimeFormats::getAvailableFormatNames()))
      .setIntroducedIn(30500);

  options
      ->addOption("--log.ids", "log unique message ids", new BooleanParameter(&_showIds))
      .setIntroducedIn(30500);

  options->addOption("--log.role", "log server role", new BooleanParameter(&_showRole));

  options
      ->addOption("--log.file-mode",
                  "mode to use for new log file, umask will be applied as well",
                  new StringParameter(&_fileMode))
      .setIntroducedIn(30405);

  if (_threaded) {
    // this option only makes sense for arangod, not for arangosh etc.
    options->addOption("--log.api-enabled",
                       "whether the log api is enabled (true) or not (false), or only enabled for superuser JWT (jwt)",
                       new StringParameter(&_apiSwitch))
        .setIntroducedIn(30411)
        .setIntroducedIn(30506)
        .setIntroducedIn(30605);
  }

  options
      ->addOption("--log.use-json-format", "use json output format",
                  new BooleanParameter(&_useJson))
      .setIntroducedIn(30800);

#ifdef ARANGODB_HAVE_SETGID
  options
      ->addOption(
          "--log.file-group",
          "group to use for new log file, user must be a member of this group",
          new StringParameter(&_fileGroup))
      .setIntroducedIn(30405);
#endif

  options->addOption("--log.prefix", "prefix log message with this string",
                     new StringParameter(&_prefix),
                     arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden));

  options->addOption("--log.file",
                     "shortcut for '--log.output file://<filename>'",
                     new StringParameter(&_file),
                     arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden));

  options->addOption("--log.line-number",
                     "include the function name, file name and line number of the source code "
                     "that issues the log message. Format: `[func@FileName.cpp:123]`",
                     new BooleanParameter(&_lineNumber),
                     arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden));

  options->addOption(
      "--log.shorten-filenames",
      "shorten filenames in log output (use with --log.line-number)",
      new BooleanParameter(&_shortenFilenames),
      arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden));
  
  options->addOption("--log.hostname",
                     "hostname to use in log message (empty for none, use 'auto' to automatically figure out hostname)",
                     new StringParameter(&_hostname))
                     .setIntroducedIn(30800);
  
  options->addOption("--log.process", "show process identifier (pid) in log message",
                     new BooleanParameter(&_processId),
                     arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden))
                     .setIntroducedIn(30800);

  options->addOption("--log.thread", "show thread identifier in log message",
                     new BooleanParameter(&_threadId),
                     arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden));

  options->addOption("--log.thread-name", "show thread name in log message",
                     new BooleanParameter(&_threadName),
                     arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden));

  options
      ->addOption("--log.performance",
                  "shortcut for '--log.level performance=trace'",
                  new BooleanParameter(&_performance),
                  arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden))
      .setDeprecatedIn(30500);

  if (_threaded) {
    // this option only makes sense for arangod, not for arangosh etc.
    options->addOption("--log.keep-logrotate",
                       "keep the old log file after receiving a sighup",
                       new BooleanParameter(&_keepLogRotate),
                       arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden));
  }

  options->addOption("--log.foreground-tty", "also log to tty if backgrounded",
                     new BooleanParameter(&_foregroundTty),
                     arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden,
                                                         arangodb::options::Flags::Dynamic));

  options->addOption("--log.force-direct",
                     "do not start a seperate thread for logging",
                     new BooleanParameter(&_forceDirect),
                     arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden));

  options->addOption(
      "--log.request-parameters",
      "include full URLs and HTTP request parameters in trace logs",
      new BooleanParameter(&_logRequestParameters),
      arangodb::options::makeDefaultFlags(arangodb::options::Flags::Hidden));
  
  options->addObsoleteOption("log.content-filter", "", true);
  options->addObsoleteOption("log.source-filter", "", true);
  options->addObsoleteOption("log.application", "", true);
  options->addObsoleteOption("log.facility", "", true);
}

void LoggerFeature::loadOptions(std::shared_ptr<options::ProgramOptions>,
                                char const* binaryPath) {
  // for debugging purpose, we set the log levels NOW
  // this might be overwritten latter
  Logger::setLogLevel(_levels);
}

void LoggerFeature::validateOptions(std::shared_ptr<ProgramOptions> options) {
  if (options->processingResult().touched("log.file")) {
    std::string definition;

    if (_file == "+" || _file == "-") {
      definition = _file;
    } else {
      definition = "file://" + _file;
    }

    _output.push_back(definition);
  }

  if (_performance) {
    _levels.push_back("performance=trace");
  }

  if (options->processingResult().touched("log.time-format") &&
      (options->processingResult().touched("log.use-microtime") ||
       options->processingResult().touched("log.use-local-time"))) {
    LOG_TOPIC("c3f28", FATAL, arangodb::Logger::FIXME)
        << "cannot combine `--log.time-format` with either "
           "`--log.use-microtime` or `--log.use-local-time`";
    FATAL_ERROR_EXIT();
  }

  // convert the deprecated options into the new timeformat
  if (options->processingResult().touched("log.use-local-time")) {
    _timeFormatString = "local-datestring";
    // the following call ensures the string is actually valid.
    // if not valid, the following call will throw an exception and
    // abort the startup
    LogTimeFormats::formatFromName(_timeFormatString);
  } else if (options->processingResult().touched("log.use-microtime")) {
    _timeFormatString = "timestamp-micros";
    // the following call ensures the string is actually valid.
    // if not valid, the following call will throw an exception and
    // abort the startup
    LogTimeFormats::formatFromName(_timeFormatString);
  }

  if (_apiSwitch == "true" || _apiSwitch == "on" ||
      _apiSwitch == "On") {
    _apiEnabled = true;
    _apiSwitch = "true";
  } else if (_apiSwitch == "jwt" || _apiSwitch == "JWT") {
    _apiEnabled = true;
    _apiSwitch = "jwt";
  } else {
    _apiEnabled = false;
    _apiSwitch = "false";
  }

  if (!_fileMode.empty()) {
    try {
      int result = std::stoi(_fileMode, nullptr, 8);
      LogAppenderFile::setFileMode(result);
    } catch (...) {
      LOG_TOPIC("797c2", FATAL, arangodb::Logger::FIXME)
          << "expecting an octal number for log.file-mode, got '" << _fileMode << "'";
      FATAL_ERROR_EXIT();
    }
  }

#ifdef ARANGODB_HAVE_SETGID
  if (!_fileGroup.empty()) {
    int gidNumber = TRI_Int32String(_fileGroup.c_str());

    if (TRI_errno() == TRI_ERROR_NO_ERROR && gidNumber >= 0) {
#ifdef ARANGODB_HAVE_GETGRGID
      group* g = getgrgid(gidNumber);

      if (g == nullptr) {
        LOG_TOPIC("174c2", FATAL, arangodb::Logger::FIXME)
            << "unknown numeric gid '" << _fileGroup << "'";
        FATAL_ERROR_EXIT();
      }
#endif
    } else {
#ifdef ARANGODB_HAVE_GETGRNAM
      std::string name = _fileGroup;
      group* g = getgrnam(name.c_str());

      if (g != nullptr) {
        gidNumber = g->gr_gid;
      } else {
        TRI_set_errno(TRI_ERROR_SYS_ERROR);
        LOG_TOPIC("11a2c", FATAL, arangodb::Logger::FIXME)
            << "cannot convert groupname '" << _fileGroup
            << "' to numeric gid: " << TRI_last_error();
        FATAL_ERROR_EXIT();
      }
#else
      LOG_TOPIC("1c96f", FATAL, arangodb::Logger::FIXME)
          << "cannot convert groupname '" << _fileGroup << "' to numeric gid";
      FATAL_ERROR_EXIT();
#endif
    }

    LogAppenderFile::setFileGroup(gidNumber);
  }
#endif

  // replace $PID with current process id in filenames
  for (auto& output : _output) {
    output = StringUtils::replace(output, "$PID", std::to_string(Thread::currentProcessId()));
  }
}

void LoggerFeature::prepare() {
#if _WIN32
  if (!TRI_InitWindowsEventLog()) {
    std::cerr << "failed to init event log" << std::endl;
    FATAL_ERROR_EXIT();
  }
#endif
  
  // set maximum length for each log entry
  Logger::defaultLogGroup().maxLogEntryLength(std::max<uint32_t>(256, _maxEntryLength));

  Logger::setLogLevel(_levels);
  Logger::setShowIds(_showIds);
  Logger::setShowRole(_showRole);
  Logger::setUseColor(_useColor);
  Logger::setTimeFormat(LogTimeFormats::formatFromName(_timeFormatString));
  Logger::setUseControlEscaped(_useControlEscaped);
  Logger::setUseUnicodeEscaped(_useUnicodeEscaped);
  Logger::setShowLineNumber(_lineNumber);
  Logger::setShortenFilenames(_shortenFilenames);
  Logger::setShowProcessIdentifier(_processId);
  Logger::setShowThreadIdentifier(_threadId);
  Logger::setShowThreadName(_threadName);
  Logger::setOutputPrefix(_prefix);
  Logger::setHostname(_hostname);
  Logger::setKeepLogrotate(_keepLogRotate);
  Logger::setLogRequestParameters(_logRequestParameters);
  Logger::setUseJson(_useJson);

  for (auto const& definition : _output) {
    if (_supervisor && StringUtils::isPrefix(definition, "file://")) {
      LogAppender::addAppender(Logger::defaultLogGroup(),
                               definition + ".supervisor");
    } else {
      LogAppender::addAppender(Logger::defaultLogGroup(), definition);
    }
  }

  if (_foregroundTty) {
    LogAppender::addAppender(Logger::defaultLogGroup(), "-");
  }

  if (_forceDirect || _supervisor) {
    Logger::initialize(server(), false);
  } else {
    Logger::initialize(server(), _threaded);
  }
}

void LoggerFeature::unprepare() { Logger::flush(); }

}  // namespace arangodb
