#include "TodoistProjectsParser.h"

#include <cstdlib>
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

}  // namespace

TodoistProjectsParser::TodoistProjectsParser(const ProjectSink sink, void* sinkCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      sink(sink),
      sinkCtx(sinkCtx) {
  reset();
}

void TodoistProjectsParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  projectDepth = 0;
  projectsSeen = 0;
  clearCurrent();
}

void TodoistProjectsParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void TodoistProjectsParser::clearCurrent() {
  currentId[0] = '\0';
  currentName[0] = '\0';
  currentOrder = 0;
}

void TodoistProjectsParser::commitProject() {
  if (currentId[0] != '\0' && currentName[0] != '\0') {
    projectsSeen++;
    if (sink) sink(sinkCtx, currentId, currentName, currentOrder);
  }
  clearCurrent();
}

// -- SAX callbacks (static trampolines) -------------------------------------

void TodoistProjectsParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<TodoistProjectsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->lastKey = (self->depth == 1 && keyIs(key, len, "results", 7)) ? LastKey::RESULTS : LastKey::NONE;
      break;
    case Position::IN_PROJECT_OBJECT:
      if (self->projectDepth == 1) {
        if (keyIs(key, len, "id", 2))
          self->lastKey = LastKey::PROJECT_ID;
        else if (keyIs(key, len, "name", 4))
          self->lastKey = LastKey::PROJECT_NAME;
        else if (keyIs(key, len, "child_order", 11))
          self->lastKey = LastKey::PROJECT_ORDER;
        else
          self->lastKey = LastKey::NONE;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
    default:
      break;
  }
}

void TodoistProjectsParser::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<TodoistProjectsParser*>(ctx);

  if (self->position == Position::IN_PROJECT_OBJECT && self->projectDepth == 1) {
    if (self->lastKey == LastKey::PROJECT_ID) {
      safeCopy(self->currentId, sizeof(self->currentId), value, len);
    } else if (self->lastKey == LastKey::PROJECT_NAME) {
      safeCopy(self->currentName, sizeof(self->currentName), value, len);
    }
  }
  self->lastKey = LastKey::NONE;
}

void TodoistProjectsParser::sOnNumber(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<TodoistProjectsParser*>(ctx);

  if (self->position == Position::IN_PROJECT_OBJECT && self->projectDepth == 1 &&
      self->lastKey == LastKey::PROJECT_ORDER) {
    char digits[16];
    safeCopy(digits, sizeof(digits), value, len);
    self->currentOrder = static_cast<int32_t>(strtol(digits, nullptr, 10));
  }
  self->lastKey = LastKey::NONE;
}

void TodoistProjectsParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<TodoistProjectsParser*>(ctx)->lastKey = LastKey::NONE;
}

void TodoistProjectsParser::sOnNull(void* ctx) { static_cast<TodoistProjectsParser*>(ctx)->lastKey = LastKey::NONE; }

void TodoistProjectsParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<TodoistProjectsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_RESULTS_ARRAY:
      self->position = Position::IN_PROJECT_OBJECT;
      self->projectDepth = 1;
      self->clearCurrent();
      break;
    case Position::IN_PROJECT_OBJECT:
      self->projectDepth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void TodoistProjectsParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<TodoistProjectsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_PROJECT_OBJECT:
      self->projectDepth--;
      if (self->projectDepth == 0) {
        self->commitProject();
        self->position = Position::IN_RESULTS_ARRAY;
      }
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void TodoistProjectsParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<TodoistProjectsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::RESULTS && self->depth == 1) {
        self->position = Position::IN_RESULTS_ARRAY;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_PROJECT_OBJECT:
      self->projectDepth++;
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void TodoistProjectsParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<TodoistProjectsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_RESULTS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_PROJECT_OBJECT:
      self->projectDepth--;
      break;
  }
  self->lastKey = LastKey::NONE;
}
