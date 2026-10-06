#include "GCalEventsParser.h"

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

GCalEventsParser::GCalEventsParser(const EventSink sink, void* sinkCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      sink(sink),
      sinkCtx(sinkCtx) {
  reset();
}

void GCalEventsParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  eventDepth = 0;
  startDepth = 0;
  endDepth = 0;
  eventsSeen = 0;
  currentSummary[0] = '\0';
  currentStart[0] = '\0';
  currentEnd[0] = '\0';
  currentStatus[0] = '\0';
}

void GCalEventsParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void GCalEventsParser::commitEvent() {
  // An event with no start cannot be placed on the list, and Google omits
  // "summary" entirely for events created without a title.
  if (currentStart[0] != '\0') {
    eventsSeen++;
    if (sink) sink(sinkCtx, currentSummary, currentStart, currentEnd, currentStatus);
  }
  currentSummary[0] = '\0';
  currentStart[0] = '\0';
  currentEnd[0] = '\0';
  currentStatus[0] = '\0';
}

// -- SAX callbacks (static trampolines) -------------------------------------

void GCalEventsParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<GCalEventsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == 1 && keyIs(key, len, "items", 5)) {
        self->lastKey = LastKey::ITEMS;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_ITEMS_ARRAY:
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_EVENT_OBJECT:
      if (self->eventDepth == 1) {
        if (keyIs(key, len, "summary", 7))
          self->lastKey = LastKey::EVENT_SUMMARY;
        else if (keyIs(key, len, "status", 6))
          self->lastKey = LastKey::EVENT_STATUS;
        else if (keyIs(key, len, "start", 5))
          self->lastKey = LastKey::EVENT_START;
        else if (keyIs(key, len, "end", 3))
          self->lastKey = LastKey::EVENT_END;
        else
          self->lastKey = LastKey::NONE;
      } else if ((self->startDepth != 0 && self->eventDepth == self->startDepth) ||
                 (self->endDepth != 0 && self->eventDepth == self->endDepth)) {
        // Inside start{} or end{}: an all-day event carries "date", a timed one
        // "dateTime". Both land in the same buffer.
        if (keyIs(key, len, "dateTime", 8))
          self->lastKey = LastKey::STAMP_DATETIME;
        else if (keyIs(key, len, "date", 4))
          self->lastKey = LastKey::STAMP_DATE;
        else
          self->lastKey = LastKey::NONE;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
  }
}

void GCalEventsParser::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<GCalEventsParser*>(ctx);

  if (self->position == Position::IN_EVENT_OBJECT) {
    switch (self->lastKey) {
      case LastKey::EVENT_SUMMARY:
        if (self->eventDepth == 1) safeCopy(self->currentSummary, sizeof(self->currentSummary), value, len);
        break;
      case LastKey::EVENT_STATUS:
        if (self->eventDepth == 1) safeCopy(self->currentStatus, sizeof(self->currentStatus), value, len);
        break;
      case LastKey::STAMP_DATETIME:
      case LastKey::STAMP_DATE:
        if (self->startDepth != 0 && self->eventDepth == self->startDepth) {
          safeCopy(self->currentStart, sizeof(self->currentStart), value, len);
        } else if (self->endDepth != 0 && self->eventDepth == self->endDepth) {
          safeCopy(self->currentEnd, sizeof(self->currentEnd), value, len);
        }
        break;
      default:
        break;
    }
  }
  self->lastKey = LastKey::NONE;
}

void GCalEventsParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<GCalEventsParser*>(ctx)->lastKey = LastKey::NONE;
}

void GCalEventsParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<GCalEventsParser*>(ctx)->lastKey = LastKey::NONE;
}

void GCalEventsParser::sOnNull(void* ctx) { static_cast<GCalEventsParser*>(ctx)->lastKey = LastKey::NONE; }

void GCalEventsParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<GCalEventsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_ITEMS_ARRAY:
      self->position = Position::IN_EVENT_OBJECT;
      self->eventDepth = 1;
      self->startDepth = 0;
      self->endDepth = 0;
      self->currentSummary[0] = '\0';
      self->currentStart[0] = '\0';
      self->currentEnd[0] = '\0';
      self->currentStatus[0] = '\0';
      break;
    case Position::IN_EVENT_OBJECT:
      self->eventDepth++;
      // Only the object opened right after "start"/"end" is that stamp; sibling
      // objects (originalStartTime, creator, planner) carry their own "date".
      if (self->lastKey == LastKey::EVENT_START && self->startDepth == 0) {
        self->startDepth = self->eventDepth;
      } else if (self->lastKey == LastKey::EVENT_END && self->endDepth == 0) {
        self->endDepth = self->eventDepth;
      }
      break;
  }
  self->lastKey = LastKey::NONE;
}

void GCalEventsParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<GCalEventsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_EVENT_OBJECT:
      if (self->startDepth == self->eventDepth) self->startDepth = 0;
      if (self->endDepth == self->eventDepth) self->endDepth = 0;
      self->eventDepth--;
      if (self->eventDepth == 0) {
        self->commitEvent();
        self->position = Position::IN_ITEMS_ARRAY;
      }
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void GCalEventsParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<GCalEventsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::ITEMS && self->depth == 1) {
        self->position = Position::IN_ITEMS_ARRAY;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_EVENT_OBJECT:
      self->eventDepth++;
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void GCalEventsParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<GCalEventsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ITEMS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_EVENT_OBJECT:
      if (self->startDepth == self->eventDepth) self->startDepth = 0;
      if (self->endDepth == self->eventDepth) self->endDepth = 0;
      self->eventDepth--;
      break;
  }
  self->lastKey = LastKey::NONE;
}
