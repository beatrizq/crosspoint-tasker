#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

/**
 * SAX-style counter for the Todoist "completed tasks by completion date"
 * response:
 *
 *   {"items":[{"id":"...","content":"...","completed_at":"...",...}, ...],
 *    "next_cursor":null}
 *
 * Every field except `id` and `content` is walked past without being stored,
 * the same streaming approach TodoistTasksParser uses -- the task's id and
 * title are captured into small per-item scratch buffers and handed to an
 * optional sink as each item closes, so a response with many items never needs
 * more than one item's worth of RAM at a time. The sink is optional: a caller
 * that only wants the count (as this class originally did) can pass nullptr.
 */
class TodoistCompletedCountParser {
 public:
  // Invoked once per completed item, as soon as it closes, with its task id and
  // title (content field). id is "" when the item carries none. Not invoked for
  // an item with no content field.
  using ItemSink = void (*)(void* ctx, const char* id, const char* content);

  explicit TodoistCompletedCountParser(ItemSink sink = nullptr, void* sinkCtx = nullptr);

  TodoistCompletedCountParser(const TodoistCompletedCountParser&) = delete;
  TodoistCompletedCountParser& operator=(const TodoistCompletedCountParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  size_t count() const { return itemCount; }
  bool hasError() const { return parser.hasError(); }

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_ITEMS_ARRAY,
    IN_ITEM_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    ITEMS,
    CONTENT,
    ID,
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

  StreamingJsonParser parser;
  ItemSink sink;
  void* sinkCtx;

  Position position;
  LastKey lastKey;
  uint8_t depth;      // Object/array nesting outside the items array
  uint8_t itemDepth;  // Nesting inside the current item object (1 = the item itself)
  size_t itemCount;

  // Same size as TodoistTasksParser's currentContent -- Todoist's own title
  // length convention, not something this parser invents independently.
  char currentContent[121];
  char currentId[32];
};
