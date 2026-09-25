#include "TodoistCompletedCountParser.h"

#include <cstring>

namespace {

void safeCopy(char* dst, const size_t dstSize, const char* src, const size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool keyIs(const char* key, const size_t len, const char* expected, const size_t expectedLen) {
  return len == expectedLen && memcmp(key, expected, expectedLen) == 0;
}
}  // namespace

TodoistCompletedCountParser::TodoistCompletedCountParser(const ItemSink sink, void* sinkCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      sink(sink),
      sinkCtx(sinkCtx) {
  reset();
}

void TodoistCompletedCountParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  itemDepth = 0;
  itemCount = 0;
  currentContent[0] = '\0';
  currentId[0] = '\0';
}

void TodoistCompletedCountParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void TodoistCompletedCountParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);

  if (self->position == Position::TOP_LEVEL && self->depth == 1 && keyIs(key, len, "items", 5)) {
    self->lastKey = LastKey::ITEMS;
  } else if (self->position == Position::IN_ITEM_OBJECT && self->itemDepth == 1 && keyIs(key, len, "content", 7)) {
    self->lastKey = LastKey::CONTENT;
  } else if (self->position == Position::IN_ITEM_OBJECT && self->itemDepth == 1 && keyIs(key, len, "id", 2)) {
    self->lastKey = LastKey::ID;
  } else {
    self->lastKey = LastKey::NONE;
  }
}

void TodoistCompletedCountParser::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);
  if (self->position == Position::IN_ITEM_OBJECT && self->lastKey == LastKey::CONTENT) {
    safeCopy(self->currentContent, sizeof(self->currentContent), value, len);
  } else if (self->position == Position::IN_ITEM_OBJECT && self->lastKey == LastKey::ID) {
    safeCopy(self->currentId, sizeof(self->currentId), value, len);
  }
  self->lastKey = LastKey::NONE;
}

void TodoistCompletedCountParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<TodoistCompletedCountParser*>(ctx)->lastKey = LastKey::NONE;
}

void TodoistCompletedCountParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<TodoistCompletedCountParser*>(ctx)->lastKey = LastKey::NONE;
}

void TodoistCompletedCountParser::sOnNull(void* ctx) {
  static_cast<TodoistCompletedCountParser*>(ctx)->lastKey = LastKey::NONE;
}

void TodoistCompletedCountParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_ITEMS_ARRAY:
      self->position = Position::IN_ITEM_OBJECT;
      self->itemDepth = 1;
      self->currentContent[0] = '\0';
      self->currentId[0] = '\0';
      break;
    case Position::IN_ITEM_OBJECT:
      self->itemDepth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void TodoistCompletedCountParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ITEM_OBJECT:
      self->itemDepth--;
      if (self->itemDepth == 0) {
        self->itemCount++;
        if (self->sink && self->currentContent[0] != '\0') {
          self->sink(self->sinkCtx, self->currentId, self->currentContent);
        }
        self->currentContent[0] = '\0';
        self->currentId[0] = '\0';
        self->position = Position::IN_ITEMS_ARRAY;
      }
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void TodoistCompletedCountParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::ITEMS && self->depth == 1) {
        self->position = Position::IN_ITEMS_ARRAY;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_ITEM_OBJECT:
      self->itemDepth++;
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void TodoistCompletedCountParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ITEMS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_ITEM_OBJECT:
      self->itemDepth--;
      break;
  }
  self->lastKey = LastKey::NONE;
}
