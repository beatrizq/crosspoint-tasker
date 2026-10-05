#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace todoist {

// A task's project, kept as a 32-bit hash of Todoist's project id rather than
// the id string: tasks only ever need to be grouped by project, and four bytes
// per task is far cheaper than a 16-character std::string each. 0 means "no
// project known" (a cache written before projects were tracked, or a task whose
// project_id was missing), so a real id never hashes to it.
constexpr uint32_t hashProjectId(const char* id) {
  if (id == nullptr || id[0] == '\0') return 0;
  uint32_t hash = 2166136261u;  // FNV-1a
  for (const char* p = id; *p != '\0'; p++) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 16777619u;
  }
  return hash == 0 ? 1 : hash;
}

}  // namespace todoist

// One Todoist project: what the Companion's title row shows. `hash` is
// todoist::hashProjectId() of its id, matching TodoistTask::projectHash.
struct TodoistProject {
  uint32_t hash = 0;
  std::string name;

  // The title row is one line wide, so a long name is cut at parse time.
  static constexpr size_t NAME_MAX_LEN = 24;
};

// A cap on how many projects are kept; the title row cannot show more than a
// handful anyway, and it bounds the cache and the fetch alike.
static constexpr size_t TODOIST_MAX_PROJECTS = 16;
