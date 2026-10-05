#include "TodoistTasksParser.h"

#include <cstring>

namespace {

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool keyIs(const char* key, size_t len, const char* expected, size_t expectedLen) {
  return len == expectedLen && memcmp(key, expected, expectedLen) == 0;
}

// Appends `label` to the ", "-joined list in `dst`. A label that does not fit
// is dropped whole (never truncated) and sets `full`, so no later label is added
// after the gap.
void appendLabel(char* dst, size_t dstSize, size_t& len, bool& full, const char* label, size_t labelLen) {
  const size_t separator = len == 0 ? 0 : 2;
  if (full || labelLen == 0) return;
  if (len + separator + labelLen > dstSize - 1) {
    full = true;
    return;
  }
  if (separator) {
    dst[len++] = ',';
    dst[len++] = ' ';
  }
  memcpy(dst + len, label, labelLen);
  len += labelLen;
  dst[len] = '\0';
}

}  // namespace

TodoistTasksParser::TodoistTasksParser(const TaskSink sink, void* sinkCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      sink(sink),
      sinkCtx(sinkCtx) {
  reset();
}

void TodoistTasksParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  taskDepth = 0;
  dueDepth = 0;
  labelsDepth = 0;
  tasksSeen = 0;
  currentId[0] = '\0';
  currentContent[0] = '\0';
  currentProjectId[0] = '\0';
  currentDue[0] = '\0';
  currentIsRecurring = false;
  currentLabels[0] = '\0';
  currentLabelsLen = 0;
  labelsFull = false;
}

void TodoistTasksParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void TodoistTasksParser::commitTask() {
  if (currentId[0] != '\0' && currentContent[0] != '\0') {
    tasksSeen++;
    if (sink) sink(sinkCtx, currentId, currentContent, currentDue, currentIsRecurring, currentLabels, currentProjectId);
  }
  currentId[0] = '\0';
  currentContent[0] = '\0';
  currentProjectId[0] = '\0';
  currentDue[0] = '\0';
  currentIsRecurring = false;
  currentLabels[0] = '\0';
  currentLabelsLen = 0;
  labelsFull = false;
}

// -- SAX callbacks (static trampolines) -------------------------------------

void TodoistTasksParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<TodoistTasksParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == 1 && keyIs(key, len, "results", 7)) {
        self->lastKey = LastKey::RESULTS;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_TASK_OBJECT:
      if (self->taskDepth == 1) {
        if (keyIs(key, len, "id", 2))
          self->lastKey = LastKey::TASK_ID;
        else if (keyIs(key, len, "content", 7))
          self->lastKey = LastKey::TASK_CONTENT;
        else if (keyIs(key, len, "due", 3))
          self->lastKey = LastKey::TASK_DUE;
        else if (keyIs(key, len, "labels", 6))
          self->lastKey = LastKey::TASK_LABELS;
        else if (keyIs(key, len, "project_id", 10))
          self->lastKey = LastKey::TASK_PROJECT_ID;
        else
          self->lastKey = LastKey::NONE;
      } else if (self->dueDepth != 0 && self->taskDepth == self->dueDepth && keyIs(key, len, "date", 4)) {
        // due.date is either "YYYY-MM-DD" or a full datetime; both start with
        // the date, and the copy below keeps only that.
        self->lastKey = LastKey::DUE_DATE;
      } else if (self->dueDepth != 0 && self->taskDepth == self->dueDepth && keyIs(key, len, "is_recurring", 12)) {
        self->lastKey = LastKey::DUE_IS_RECURRING;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
    default:
      break;
  }
}

void TodoistTasksParser::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<TodoistTasksParser*>(ctx);

  if (self->position == Position::IN_TASK_OBJECT) {
    // A label: a bare string inside the task's labels array (array elements
    // carry no key, so lastKey says nothing here).
    if (self->labelsDepth != 0 && self->taskDepth == self->labelsDepth) {
      appendLabel(self->currentLabels, sizeof(self->currentLabels), self->currentLabelsLen, self->labelsFull, value,
                  len);
      self->lastKey = LastKey::NONE;
      return;
    }
    switch (self->lastKey) {
      case LastKey::TASK_ID:
        if (self->taskDepth == 1) safeCopy(self->currentId, sizeof(self->currentId), value, len);
        break;
      case LastKey::TASK_CONTENT:
        if (self->taskDepth == 1) safeCopy(self->currentContent, sizeof(self->currentContent), value, len);
        break;
      case LastKey::TASK_PROJECT_ID:
        if (self->taskDepth == 1) safeCopy(self->currentProjectId, sizeof(self->currentProjectId), value, len);
        break;
      case LastKey::DUE_DATE:
        safeCopy(self->currentDue, sizeof(self->currentDue), value, len);
        break;
      default:
        break;
    }
  }
  self->lastKey = LastKey::NONE;
}

void TodoistTasksParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<TodoistTasksParser*>(ctx)->lastKey = LastKey::NONE;
}

void TodoistTasksParser::sOnBool(void* ctx, const bool value) {
  auto* self = static_cast<TodoistTasksParser*>(ctx);
  if (self->position == Position::IN_TASK_OBJECT && self->lastKey == LastKey::DUE_IS_RECURRING) {
    self->currentIsRecurring = value;
  }
  self->lastKey = LastKey::NONE;
}

void TodoistTasksParser::sOnNull(void* ctx) { static_cast<TodoistTasksParser*>(ctx)->lastKey = LastKey::NONE; }

void TodoistTasksParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<TodoistTasksParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_RESULTS_ARRAY:
      self->position = Position::IN_TASK_OBJECT;
      self->taskDepth = 1;
      self->dueDepth = 0;
      self->labelsDepth = 0;
      self->currentId[0] = '\0';
      self->currentContent[0] = '\0';
      self->currentProjectId[0] = '\0';
      self->currentDue[0] = '\0';
      self->currentLabels[0] = '\0';
      self->currentLabelsLen = 0;
      self->labelsFull = false;
      break;
    case Position::IN_TASK_OBJECT:
      self->taskDepth++;
      // Only the object opened right after the "due" key is the due object;
      // sibling objects (deadline, duration, meta) carry their own "date".
      if (self->lastKey == LastKey::TASK_DUE && self->dueDepth == 0) self->dueDepth = self->taskDepth;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void TodoistTasksParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<TodoistTasksParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_TASK_OBJECT:
      if (self->dueDepth == self->taskDepth) self->dueDepth = 0;
      self->taskDepth--;
      if (self->taskDepth == 0) {
        self->commitTask();
        self->position = Position::IN_RESULTS_ARRAY;
      }
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void TodoistTasksParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<TodoistTasksParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::RESULTS && self->depth == 1) {
        self->position = Position::IN_RESULTS_ARRAY;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_TASK_OBJECT:
      self->taskDepth++;
      // Only the array opened right after the "labels" key is the labels array.
      if (self->lastKey == LastKey::TASK_LABELS && self->labelsDepth == 0) self->labelsDepth = self->taskDepth;
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void TodoistTasksParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<TodoistTasksParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_RESULTS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_TASK_OBJECT:
      if (self->dueDepth == self->taskDepth) self->dueDepth = 0;
      if (self->labelsDepth == self->taskDepth) self->labelsDepth = 0;
      self->taskDepth--;
      break;
  }
  self->lastKey = LastKey::NONE;
}
