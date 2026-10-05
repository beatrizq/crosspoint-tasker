#pragma once
#include <cstdint>
#include <string>

#include "TodoistDate.h"

// One row of the Today screen. Priorities are dropped at parse time (labels are
// kept, as one display string, and the project survives only as a 4-byte hash);
// the due date survives only as a packed 2-byte day count, so a synced list
// costs ~44 bytes plus the content string per task.
struct TodoistTask {
  std::string id;       // Todoist task id, needed for the close call
  std::string content;  // Task title, truncated to CONTENT_MAX_LEN at parse time
  // Days since 2000-01-01, or todoist::DUE_NONE when the task has no due date.
  // Doubles as the sort key and as the source the sync derives "today" from.
  uint16_t dueDays = todoist::DUE_NONE;
  bool overdue = false;  // Due before today; set by the cache once today is known
  // due.is_recurring from the API. Rescheduling a recurring task to a specific
  // date replaces its recurrence entirely rather than moving just that one
  // occurrence - Todoist's API has no way to do the latter - so this is what
  // the Reschedule action warns against before it lets that happen.
  bool isRecurring = false;
  // todoist::hashProjectId() of the task's project_id, 0 when unknown. The
  // Companion groups tasks by it under its title row.
  uint32_t projectHash = 0;
  // The task's Todoist labels as one display string, "a, b" -- empty when it has
  // none. Shown under the title on the Companion; truncated at parse time.
  std::string labels;

  static constexpr size_t CONTENT_MAX_LEN = 120;
  static constexpr size_t LABELS_MAX_LEN = 48;
};

// A task list longer than this is not readable on a 480px screen anyway; the
// cap bounds the fetch buffer, the SD file, and the completion queue alike. A
// filter can match more than this, so the fetch keeps the soonest.
static constexpr size_t TODOIST_MAX_TASKS = 60;

// The cache holds one fetch, so it is as long as the fetch cap. Bounds the
// cache, its SD file, and the completion queue.
static constexpr size_t TODOIST_MAX_CACHED_TASKS = TODOIST_MAX_TASKS;
