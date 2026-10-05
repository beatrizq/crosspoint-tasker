#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

/**
 * SAX-style extractor for the Todoist "get tasks by filter" response:
 *
 *   {"results":[{"id":"...","content":"...",
 *                "due":{"date":"2026-08-17","is_recurring":false,...},...}],"next_cursor":null}
 *
 * Only id, content, labels, project_id, due.date and due.is_recurring are kept;
 * every other field (priority, description, duration) is walked past
 * without being stored. The body is fed in as it arrives off the socket, so a
 * 200-task response never exists in RAM as a whole — only the ~200 bytes of
 * the task being assembled.
 */
class TodoistTasksParser {
 public:
  // Invoked once per task object, as soon as it closes. dueDate is "" when the
  // task has no due object (possible for tasks pulled in by a filter's
  // secondary clauses); isRecurring is meaningless in that case too. labels is
  // the task's labels joined as "a, b" (truncated), "" when it has none.
  // projectId is the task's project_id, "" when absent.
  using TaskSink = void (*)(void* ctx, const char* id, const char* content, const char* dueDate, bool isRecurring,
                            const char* labels, const char* projectId);

  TodoistTasksParser(TaskSink sink, void* sinkCtx);

  TodoistTasksParser(const TodoistTasksParser&) = delete;
  TodoistTasksParser& operator=(const TodoistTasksParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  // Tasks seen in the response, including any the sink chose to drop.
  size_t taskCount() const { return tasksSeen; }
  bool hasError() const { return parser.hasError(); }

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_RESULTS_ARRAY,
    IN_TASK_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    RESULTS,
    TASK_ID,
    TASK_CONTENT,
    TASK_DUE,
    TASK_LABELS,
    TASK_PROJECT_ID,
    DUE_DATE,
    DUE_IS_RECURRING,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitTask();

  StreamingJsonParser parser;
  TaskSink sink;
  void* sinkCtx;

  Position position;
  LastKey lastKey;
  uint8_t depth;        // Object/array nesting outside the results array
  uint8_t taskDepth;    // Nesting inside the current task object (1 = task itself)
  uint8_t dueDepth;     // taskDepth of the task's due object; 0 when not inside it
  uint8_t labelsDepth;  // taskDepth of the task's labels array; 0 when not inside it
  size_t tasksSeen;

  // Ids are 19-digit numerics or 16-char alphanumerics today; dates are
  // "YYYY-MM-DD" (a datetime is truncated to its date half on copy).
  char currentId[32];
  char currentContent[121];
  char currentProjectId[32];
  char currentDue[11];
  bool currentIsRecurring;
  // Labels joined with ", " -- 48 characters (TodoistTask::LABELS_MAX_LEN) is
  // what the Companion has room to show, so anything past it is dropped here
  // rather than carried through the cache.
  char currentLabels[49];
  size_t currentLabelsLen;
  bool labelsFull;  // a label did not fit: the rest are dropped too, so the order stays meaningful
};
