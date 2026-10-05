#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "lib/Todoist/TodoistProject.h"
#include "lib/Todoist/TodoistProjectsParser.h"

namespace {

// Trimmed but structurally faithful GET /api/v1/projects response.
const char* kProjectsResponse = R"({
  "results": [
    {
      "id": "6Jf8VQXxpwv56VQ7",
      "can_assign_tasks": false,
      "child_order": 2,
      "color": "berry_red",
      "creator_uid": "2671355",
      "created_at": "2026-01-01T09:00:00.000000Z",
      "is_archived": false,
      "is_deleted": false,
      "is_favorite": false,
      "is_frozen": false,
      "name": "Work",
      "updated_at": "2026-02-01T09:00:00.000000Z",
      "view_style": "list",
      "default_order": 0,
      "description": "",
      "public_key": "",
      "access": {"visibility": "restricted", "configuration": {}},
      "role": "CREATOR",
      "parent_id": null,
      "inbox_project": false,
      "is_collapsed": false,
      "is_shared": false
    },
    {"id": "6Jf8VQXxpwv56VQ8", "name": "Inbox", "child_order": 0, "inbox_project": true},
    {"id": "6Jf8VQXxpwv56VQ9", "name": "Home", "child_order": 1}
  ],
  "next_cursor": null
})";

struct ParsedProject {
  std::string id;
  std::string name;
  int32_t order;
};

void collect(void* ctx, const char* id, const char* name, const int32_t order) {
  static_cast<std::vector<ParsedProject>*>(ctx)->push_back({id, name, order});
}

std::vector<ParsedProject> parseInChunks(const char* body, size_t chunkSize) {
  std::vector<ParsedProject> projects;
  TodoistProjectsParser parser(collect, &projects);
  const size_t len = strlen(body);
  for (size_t offset = 0; offset < len; offset += chunkSize) {
    parser.feed(body + offset, std::min(chunkSize, len - offset));
  }
  EXPECT_FALSE(parser.hasError());
  EXPECT_EQ(parser.projectCount(), projects.size());
  return projects;
}

TEST(TodoistProjectsParser, ExtractsIdNameAndOrder) {
  const auto projects = parseInChunks(kProjectsResponse, 4096);

  ASSERT_EQ(projects.size(), 3u);
  EXPECT_EQ(projects[0].id, "6Jf8VQXxpwv56VQ7");
  EXPECT_EQ(projects[0].name, "Work");
  EXPECT_EQ(projects[0].order, 2);
  EXPECT_EQ(projects[1].name, "Inbox");
  EXPECT_EQ(projects[1].order, 0);
  EXPECT_EQ(projects[2].name, "Home");
  EXPECT_EQ(projects[2].order, 1);
}

// Nested objects ("access") and null values must not derail the walk.
TEST(TodoistProjectsParser, SurvivesTinyChunksAndNestedObjects) {
  const auto projects = parseInChunks(kProjectsResponse, 1);

  ASSERT_EQ(projects.size(), 3u);
  EXPECT_EQ(projects[0].name, "Work");
  EXPECT_EQ(projects[2].name, "Home");
}

TEST(TodoistProjectsParser, SkipsProjectsMissingAnIdOrName) {
  const auto projects = parseInChunks(R"({"results":[{"name":"No id"},{"id":"1"},{"id":"2","name":"Ok"}]})", 4096);

  ASSERT_EQ(projects.size(), 1u);
  EXPECT_EQ(projects[0].name, "Ok");
}

TEST(TodoistProjectHash, IsStableAndNeverZeroForARealId) {
  EXPECT_EQ(todoist::hashProjectId("6Jf8VQXxpwv56VQ7"), todoist::hashProjectId("6Jf8VQXxpwv56VQ7"));
  EXPECT_NE(todoist::hashProjectId("6Jf8VQXxpwv56VQ7"), todoist::hashProjectId("6Jf8VQXxpwv56VQ8"));
  EXPECT_NE(todoist::hashProjectId("6Jf8VQXxpwv56VQ7"), 0u);
  EXPECT_EQ(todoist::hashProjectId(""), 0u);
  EXPECT_EQ(todoist::hashProjectId(nullptr), 0u);
}

}  // namespace
