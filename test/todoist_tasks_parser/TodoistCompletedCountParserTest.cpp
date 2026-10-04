#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "lib/Todoist/TodoistCompletedCountParser.h"

namespace {

// Trimmed but structurally faithful GET
// /api/v1/tasks/completed/by_completion_date response: items carry nested
// objects/arrays (due, labels) the parser must walk past without miscounting.
const char* kRealisticResponse = R"({
  "items": [
    {
      "id": "6X4Vw2Hfmg73Q2XR",
      "content": "terminar fixes cup pong para release",
      "project_id": "220474322",
      "labels": ["urgent", "work"],
      "due": {"date": "2026-08-17", "is_recurring": false},
      "completed_at": "2026-08-17T14:30:00Z"
    },
    {
      "id": "6X4Vw2Hfmg73Q2XS",
      "content": "confirmar q cambios hacer a goat para crazygames",
      "due": null,
      "labels": [],
      "completed_at": "2026-08-17T09:12:00Z"
    },
    {
      "id": "6X4Vw2Hfmg73Q2XT",
      "content": "hacer musicas pong",
      "completed_at": "2026-08-17T23:59:00Z"
    }
  ],
  "next_cursor": null
})";

size_t countInChunks(const char* body, const size_t chunkSize) {
  TodoistCompletedCountParser parser;
  const size_t len = strlen(body);
  for (size_t offset = 0; offset < len; offset += chunkSize) {
    parser.feed(body + offset, std::min(chunkSize, len - offset));
  }
  EXPECT_FALSE(parser.hasError());
  return parser.count();
}

}  // namespace

TEST(TodoistCompletedCountParserTest, CountsEveryItemAtOnce) {
  EXPECT_EQ(countInChunks(kRealisticResponse, 1u << 20), 3u);
}

TEST(TodoistCompletedCountParserTest, CountsAcrossByteChunks) {
  // A single byte at a time forces the parser to resume mid-token, mid-object
  // and mid-array on nearly every feed() call.
  EXPECT_EQ(countInChunks(kRealisticResponse, 1), 3u);
}

TEST(TodoistCompletedCountParserTest, EmptyItemsArray) {
  EXPECT_EQ(countInChunks(R"({"items":[],"next_cursor":null})", 1u << 20), 0u);
}

TEST(TodoistCompletedCountParserTest, NestingOverflowReportsError) {
  // The underlying StreamingJsonParser flags hasError() on exceeding
  // MAX_NESTING, not on merely truncated input (more bytes could always
  // still arrive) - matches StreamingJsonParserTest's own NestingOverflow case.
  std::string body;
  for (size_t i = 0; i < StreamingJsonParser::MAX_NESTING + 5; ++i) body += "[";

  TodoistCompletedCountParser parser;
  parser.feed(body.c_str(), body.size());
  EXPECT_TRUE(parser.hasError());
}

TEST(TodoistCompletedCountParserTest, ResetClearsCountAndError) {
  TodoistCompletedCountParser parser;
  parser.feed(kRealisticResponse, strlen(kRealisticResponse));
  EXPECT_EQ(parser.count(), 3u);

  parser.reset();
  EXPECT_EQ(parser.count(), 0u);
  EXPECT_FALSE(parser.hasError());

  const char* body = R"({"items":[{"id":"x"}]})";
  parser.feed(body, strlen(body));
  EXPECT_EQ(parser.count(), 1u);
}

namespace {

struct SeenItem {
  std::string id;
  std::string content;
};

void collectItem(void* ctx, const char* id, const char* content) {
  static_cast<std::vector<SeenItem>*>(ctx)->push_back({id, content});
}

}  // namespace

// The sink gets each item's task id along with its title, so a caller merging
// two filters' completions can tell which are the same task.
TEST(TodoistCompletedCountParserTest, SinkReceivesIdAndContent) {
  std::vector<SeenItem> items;
  TodoistCompletedCountParser parser(collectItem, &items);
  const size_t len = strlen(kRealisticResponse);
  for (size_t offset = 0; offset < len; offset += 7) {
    parser.feed(kRealisticResponse + offset, std::min<size_t>(7, len - offset));
  }

  ASSERT_EQ(items.size(), 3u);
  EXPECT_EQ(items[0].id, "6X4Vw2Hfmg73Q2XR");
  EXPECT_EQ(items[0].content, "terminar fixes cup pong para release");
  EXPECT_EQ(items[2].id, "6X4Vw2Hfmg73Q2XT");
}
