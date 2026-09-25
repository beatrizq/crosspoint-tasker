#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "lib/Todoist/TodoistTasksParser.h"

namespace {

// Trimmed but structurally faithful GET /api/v1/tasks/filter?query=today|overdue
// response: sibling objects that also carry a "date" key (deadline), a
// null due, a datetime due, and fields the parser must walk past.
const char* kRealisticResponse = R"({
  "results": [
    {
      "user_id": "2671355",
      "id": "6X4Vw2Hfmg73Q2XR",
      "project_id": "220474322",
      "section_id": null,
      "parent_id": null,
      "labels": ["urgent", "work"],
      "deadline": {"date": "2026-08-30", "lang": "en"},
      "duration": {"amount": 30, "unit": "minute"},
      "checked": false,
      "added_at": "2026-08-10T09:00:00.000000Z",
      "due": {"date": "2026-08-17", "timezone": null, "string": "today", "lang": "en", "is_recurring": false},
      "priority": 4,
      "content": "terminar fixes cup pong para release",
      "description": ""
    },
    {
      "id": "6X4Vw2Hfmg73Q2XS",
      "content": "confirmar q cambios hacer a goat para crazygames",
      "due": {"date": "2026-08-11", "is_recurring": true},
      "labels": []
    },
    {
      "id": "6X4Vw2Hfmg73Q2XT",
      "content": "hacer musicas pong",
      "due": null,
      "labels": []
    },
    {
      "id": "6X4Vw2Hfmg73Q2XU",
      "content": "revisar q no hayan bordes negros en sprites cup pong",
      "due": {"date": "2026-08-17T14:30:00", "timezone": "Europe/Madrid"}
    }
  ],
  "next_cursor": null
})";

struct ParsedTask {
  std::string id;
  std::string content;
  std::string due;
  bool isRecurring;
  std::string labels;
};

void collect(void* ctx, const char* id, const char* content, const char* due, const bool isRecurring,
             const char* labels) {
  static_cast<std::vector<ParsedTask>*>(ctx)->push_back({id, content, due, isRecurring, labels});
}

std::vector<ParsedTask> parseInChunks(const char* body, size_t chunkSize) {
  std::vector<ParsedTask> tasks;
  TodoistTasksParser parser(collect, &tasks);
  const size_t len = strlen(body);
  for (size_t offset = 0; offset < len; offset += chunkSize) {
    parser.feed(body + offset, std::min(chunkSize, len - offset));
  }
  EXPECT_FALSE(parser.hasError());
  EXPECT_EQ(parser.taskCount(), tasks.size());
  return tasks;
}

TEST(TodoistTasksParser, ExtractsIdContentAndDueDate) {
  const auto tasks = parseInChunks(kRealisticResponse, 4096);

  ASSERT_EQ(tasks.size(), 4u);
  EXPECT_EQ(tasks[0].id, "6X4Vw2Hfmg73Q2XR");
  EXPECT_EQ(tasks[0].content, "terminar fixes cup pong para release");
  EXPECT_EQ(tasks[0].due, "2026-08-17");
  EXPECT_EQ(tasks[1].due, "2026-08-11");
}

// The task object's own "deadline" carries a "date" key too; only due.date may
// drive the overdue flag.
TEST(TodoistTasksParser, IgnoresDatesFromSiblingObjects) {
  const auto tasks = parseInChunks(kRealisticResponse, 4096);

  ASSERT_EQ(tasks.size(), 4u);
  EXPECT_NE(tasks[0].due, "2026-08-30");
}

TEST(TodoistTasksParser, HandlesNullDueAndDatetimeDue) {
  const auto tasks = parseInChunks(kRealisticResponse, 4096);

  ASSERT_EQ(tasks.size(), 4u);
  EXPECT_EQ(tasks[2].due, "");            // "due": null
  EXPECT_EQ(tasks[3].due, "2026-08-17");  // datetime truncated to its date half
}

TEST(TodoistTasksParser, ExtractsIsRecurring) {
  const auto tasks = parseInChunks(kRealisticResponse, 4096);

  ASSERT_EQ(tasks.size(), 4u);
  EXPECT_FALSE(tasks[0].isRecurring);  // due.is_recurring: false
  EXPECT_TRUE(tasks[1].isRecurring);   // due.is_recurring: true
  EXPECT_FALSE(tasks[2].isRecurring);  // due: null - nothing to be recurring
  EXPECT_FALSE(tasks[3].isRecurring);  // due present, is_recurring key absent
}

// Labels are kept as one ", "-joined string; an empty array, or no labels key at
// all, gives "" -- and a label never bleeds from one task into the next.
TEST(TodoistTasksParser, ExtractsLabels) {
  const auto tasks = parseInChunks(kRealisticResponse, 4096);

  ASSERT_EQ(tasks.size(), 4u);
  EXPECT_EQ(tasks[0].labels, "urgent, work");
  EXPECT_EQ(tasks[1].labels, "");  // "labels": []
  EXPECT_EQ(tasks[2].labels, "");
  EXPECT_EQ(tasks[3].labels, "");  // no labels key
}

// More labels than the buffer holds: whole labels only, never a truncated one.
TEST(TodoistTasksParser, DropsLabelsThatDoNotFit) {
  const char* body = R"({"results":[{"id":"a","content":"c","labels":
    ["aaaaaaaaaaaaaaaaaaaa","bbbbbbbbbbbbbbbbbbbb","cccccccccccccccccccc","dddd"]}]})";
  const auto tasks = parseInChunks(body, 4096);

  ASSERT_EQ(tasks.size(), 1u);
  // 20 + 2 + 20 = 42; the third label would pass 48 so it is dropped, and "dddd"
  // (which alone would fit) is dropped after it rather than leaving a gap.
  EXPECT_EQ(tasks[0].labels, "aaaaaaaaaaaaaaaaaaaa, bbbbbbbbbbbbbbbbbbbb");
}

// The body arrives in TLS-sized chunks, so no field may depend on landing
// inside a single feed() call.
TEST(TodoistTasksParser, IsIndependentOfChunkBoundaries) {
  for (const size_t chunkSize : {1u, 3u, 7u, 64u, 512u}) {
    const auto tasks = parseInChunks(kRealisticResponse, chunkSize);
    ASSERT_EQ(tasks.size(), 4u) << "chunk size " << chunkSize;
    EXPECT_EQ(tasks[0].id, "6X4Vw2Hfmg73Q2XR") << "chunk size " << chunkSize;
    EXPECT_EQ(tasks[3].due, "2026-08-17") << "chunk size " << chunkSize;
  }
}

TEST(TodoistTasksParser, SkipsTasksMissingIdOrContent) {
  const char* body = R"({"results":[{"id":"1"},{"content":"no id"},{"id":"2","content":"kept"}]})";
  const auto tasks = parseInChunks(body, 4096);

  ASSERT_EQ(tasks.size(), 1u);
  EXPECT_EQ(tasks[0].id, "2");
  EXPECT_EQ(tasks[0].content, "kept");
}

TEST(TodoistTasksParser, EmptyResultsYieldNoTasks) {
  const auto tasks = parseInChunks(R"({"results":[],"next_cursor":null})", 4096);
  EXPECT_TRUE(tasks.empty());
}

// Content is capped at the parser's buffer, which matches TodoistTask's cap.
TEST(TodoistTasksParser, TruncatesOverlongContent) {
  const std::string longContent(400, 'x');
  const std::string body = R"({"results":[{"id":"9","content":")" + longContent + R"("}]})";
  const auto tasks = parseInChunks(body.c_str(), 4096);

  ASSERT_EQ(tasks.size(), 1u);
  EXPECT_EQ(tasks[0].content.size(), 120u);
}

// Overdue classification in TodoistClient is a plain ISO string compare.
TEST(TodoistTasksParser, IsoDatesOrderLexicographically) {
  EXPECT_LT(strcmp("2026-08-11", "2026-08-17"), 0);
  EXPECT_EQ(strcmp("2026-08-17", "2026-08-17"), 0);
  EXPECT_GT(strcmp("2026-09-01", "2026-08-31"), 0);
}

}  // namespace
