#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

/**
 * SAX-style extractor for the Todoist "get all projects" response:
 *
 *   {"results":[{"id":"...","name":"Work","child_order":1,...}],"next_cursor":null}
 *
 * Only id, name and child_order are kept. Fed as the body arrives off the
 * socket, like TodoistTasksParser, so only the project being assembled is ever
 * in RAM.
 */
class TodoistProjectsParser {
 public:
  // Invoked once per project object, as soon as it closes. order is the
  // project's child_order (its position in the user's own project list), 0 when
  // absent.
  using ProjectSink = void (*)(void* ctx, const char* id, const char* name, int32_t order);

  TodoistProjectsParser(ProjectSink sink, void* sinkCtx);

  TodoistProjectsParser(const TodoistProjectsParser&) = delete;
  TodoistProjectsParser& operator=(const TodoistProjectsParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  size_t projectCount() const { return projectsSeen; }
  bool hasError() const { return parser.hasError(); }

 private:
  enum class Position : uint8_t { TOP_LEVEL, IN_RESULTS_ARRAY, IN_PROJECT_OBJECT };
  enum class LastKey : uint8_t { NONE, RESULTS, PROJECT_ID, PROJECT_NAME, PROJECT_ORDER };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitProject();
  void clearCurrent();

  StreamingJsonParser parser;
  ProjectSink sink;
  void* sinkCtx;

  Position position;
  LastKey lastKey;
  uint8_t depth;         // Object/array nesting outside the results array
  uint8_t projectDepth;  // Nesting inside the current project object (1 = the project itself)
  size_t projectsSeen;

  char currentId[32];
  char currentName[64];  // Longer than TodoistProject::NAME_MAX_LEN; the sink cuts it to what fits on screen.
  int32_t currentOrder;
};
