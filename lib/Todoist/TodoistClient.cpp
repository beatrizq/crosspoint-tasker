#include "TodoistClient.h"

#include <CivilTime.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "TodoistCompletedCountParser.h"
#include "TodoistProjectsParser.h"
#include "TodoistStore.h"
#include "TodoistTasksParser.h"

int TodoistClient::lastHttpCode = 0;

namespace {
constexpr char API_BASE[] = "https://api.todoist.com/api/v1";

// The filter endpoint. The query itself is whatever the user put in the Filter
// setting, in Todoist's own filter syntax, percent-encoded onto the end.
//
// It used to be hardcoded to "overdue | due before: +30 days". That decided in
// firmware what the screen was for; the setting moves the decision to the user,
// and the Tasks tabs now split whatever comes back rather than defining it.
constexpr char FILTER_URL_BASE[] = "https://api.todoist.com/api/v1/tasks/filter?limit=200&query=";

// The projects endpoint: one page is plenty (see fetchProjects()).
constexpr char PROJECTS_URL[] = "https://api.todoist.com/api/v1/projects?limit=50";

// The completed-tasks-by-completion-date endpoint. since/until bound the
// query to one UTC day; filter_query scopes it to the same Filter setting
// fetchTasks() uses, so a completion outside the user's own filter never
// counts. limit is the endpoint's own page cap - see fetchCompletedCountForDay.
constexpr char COMPLETED_URL_BASE[] = "https://api.todoist.com/api/v1/tasks/completed/by_completion_date";
constexpr int COMPLETED_PAGE_LIMIT = 50;

// Percent-encodes everything outside the unreserved set. The filter is typed by
// hand and Todoist's syntax is built from characters that mean something else in
// a URL: "&" separates parameters, spaces and "|" and "!" are not legal in a
// query as-is, and "#" would truncate the URL at a fragment and silently send no
// query at all.
std::string urlEncode(const std::string& value) {
  // Not named HEX: Arduino's Print.h defines that as a macro (`#define HEX 16`).
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() * 3);
  for (const unsigned char c : value) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                            c == '_' || c == '.' || c == '~';
    if (unreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(HEX_DIGITS[c >> 4]);
      out.push_back(HEX_DIGITS[c & 0x0F]);
    }
  }
  return out;
}

// Same TLS heap gate as KOReaderSyncClient: the wolfSSL handshake needs working
// heap, and a doomed attempt costs ~15s before it gives up. Free heap and the
// largest contiguous block are checked separately because SP ECC caps the
// single largest allocation at the ~17KB record buffer while the total
// transient footprint is higher.
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;

bool insufficientHeap() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_FOR_TLS || maxAllocHeap < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("TDA", "Insufficient heap for TLS handshake: %u free (need %u), %u max alloc (need %u)", freeHeap,
            MIN_FREE_FOR_TLS, maxAllocHeap, MIN_BLOCK_FOR_TLS);
    return true;
  }
  return false;
}

void applyAuthHeaders(freeink::SecureHttpClient& http) {
  http.addHeader("Authorization", "Bearer " + TODOIST_STORE.getToken());
  http.addHeader("Accept", "application/json");
}

TodoistClient::Error errorForStatus(const int httpCode) {
  if (httpCode <= 0) return TodoistClient::NETWORK_ERROR;
  if (httpCode >= 200 && httpCode < 300) return TodoistClient::OK;
  if (httpCode == 401 || httpCode == 403) return TodoistClient::AUTH_FAILED;
  // A filter Todoist cannot parse comes back as a 400. Reported separately from a
  // server error because it is the one failure here the user can actually fix,
  // and "invalid filter" points straight at the setting that caused it.
  if (httpCode == 400) return TodoistClient::INVALID_FILTER;
  if (httpCode == 404) return TodoistClient::NOT_FOUND;
  return TodoistClient::SERVER_ERROR;
}

// Sink context for TodoistTasksParser: collects parsed tasks, packing each due
// date. Overdue is decided by the cache, which needs today - only settled once
// the whole response has been seen.
struct TaskCollector {
  std::vector<TodoistTask>* out;
};

void collectTask(void* ctx, const char* id, const char* content, const char* dueDate, const bool isRecurring,
                 const char* labels, const char* projectId) {
  auto* collector = static_cast<TaskCollector*>(ctx);
  auto& out = *collector->out;

  TodoistTask task;
  task.id = id;
  task.content = content;
  task.dueDays = todoist::dueDaysFromIso(dueDate);
  task.isRecurring = isRecurring;
  task.labels = labels;
  task.projectHash = todoist::hashProjectId(projectId);

  if (out.size() < TODOIST_MAX_TASKS) {
    out.push_back(std::move(task));
    return;
  }

  // At the cap. A filter can match far more tasks than the cap holds and the API
  // does not promise an order, so dropping whatever arrives last could discard
  // today's tasks to keep next year's. The least useful task held so far is
  // evicted instead. Linear per task past the cap, over at most 60 entries -
  // nothing next to the TLS read that delivered them.
  //
  // "Least useful" ranks a dated task by how far out it is, and puts every
  // undated task behind all of them. That is deliberately not what DUE_NONE's
  // sentinel value would give: as the numeric maximum it would make undated tasks
  // the *first* thing evicted, emptying the No date tab on exactly the accounts
  // big enough to need it. Ordering them last instead keeps some of them while
  // still preferring a dated task when the cap forces a choice.
  const auto worst = std::max_element(out.begin(), out.end(), [](const TodoistTask& a, const TodoistTask& b) {
    const bool aDated = a.dueDays != todoist::DUE_NONE;
    const bool bDated = b.dueDays != todoist::DUE_NONE;
    if (aDated != bDated) return aDated;  // undated sorts after every dated task
    if (!aDated) return false;            // two undated tasks are equally droppable
    return a.dueDays < b.dueDays;
  });
  if (worst == out.end()) return;

  // Swap in only if the arrival is worth more than what it displaces.
  const bool newDated = task.dueDays != todoist::DUE_NONE;
  const bool worstDated = worst->dueDays != todoist::DUE_NONE;
  const bool preferNew = newDated ? (!worstDated || task.dueDays < worst->dueDays) : false;
  if (preferNew) *worst = std::move(task);
}

// Sink context for TodoistProjectsParser: collects each project with its
// child_order so the list can be put in the app's own order afterwards.
struct ProjectCollector {
  std::vector<std::pair<int32_t, TodoistProject>>* out;
};

void collectProject(void* ctx, const char* id, const char* name, const int32_t order) {
  auto* collector = static_cast<ProjectCollector*>(ctx);
  if (collector->out->size() >= TODOIST_MAX_PROJECTS) return;

  // Cut at NAME_MAX_LEN characters (not bytes, so a multi-byte character is
  // never split) -- the title row is one line wide.
  const auto* begin = reinterpret_cast<const unsigned char*>(name);
  const auto* p = begin;
  size_t chars = 0;
  while (*p != '\0' && chars < TodoistProject::NAME_MAX_LEN) {
    utf8NextCodepoint(&p);
    chars++;
  }
  TodoistProject project;
  project.hash = todoist::hashProjectId(id);
  project.name.assign(name, static_cast<size_t>(p - begin));
  collector->out->emplace_back(order, std::move(project));
}
}  // namespace

TodoistClient::Error TodoistClient::fetchTasks(freeink::SecureHttpClient& http, const std::string& filter,
                                               std::vector<TodoistTask>& outTasks, std::string& outServerDate) {
  lastHttpCode = 0;
  outServerDate.clear();
  if (!TODOIST_STORE.hasToken()) {
    LOG_DBG("TDA", "No API token configured");
    return NO_TOKEN;
  }
  if (insufficientHeap()) return LOW_MEMORY;

  outTasks.clear();
  outTasks.reserve(TODOIST_MAX_TASKS);
  TaskCollector collector{&outTasks};
  TodoistTasksParser parser(collectTask, &collector);

  // std::string rather than a stack buffer: a filter is allowed 1,024 characters
  // and encoding can triple that, far past what belongs on this stack.
  const std::string url = std::string(FILTER_URL_BASE) + urlEncode(filter);
  LOG_DBG("TDA", "Filter query: %s", filter.c_str());

  if (!http.begin(url)) {
    LOG_ERR("TDA", "Bad filter URL");
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);

  // Streamed into the parser as it arrives: a 200-task response is ~100KB of
  // JSON, which would not fit alongside a live TLS session.
  const int httpCode = http.GET([&parser](const uint8_t* data, const size_t len) {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });

  // Read before the caller's next begin(): a reused connection's headers get
  // overwritten by the next request, and this function does not call end().
  const std::string dateHeader = http.getHeader("date");
  lastHttpCode = httpCode;
  LOG_DBG("TDA", "Filter response: %d (%zu tasks parsed)", httpCode, parser.taskCount());

  const Error status = errorForStatus(httpCode);
  if (status != OK) {
    outTasks.clear();
    return status;
  }
  if (parser.hasError()) {
    LOG_ERR("TDA", "Malformed task JSON");
    outTasks.clear();
    return PARSE_ERROR;
  }

  // Today comes off the Date header. Unlike the newest due date this filter can
  // no longer stand in for it - the window reaches a month ahead - and a header
  // present on every successful response beats a body that has to contain
  // something due today to say anything at all.
  const uint16_t serverDate = dateHeader.empty() ? civil::NO_DATE : civil::dateFromHttpHeader(dateHeader.c_str());
  if (serverDate != civil::NO_DATE) {
    char iso[11];
    civil::isoFromDate(serverDate, iso, sizeof(iso));
    outServerDate = iso;
    LOG_DBG("TDA", "Server date: %s", iso);
  } else {
    LOG_DBG("TDA", "No usable Date header; today not derivable from the response");
  }
  return OK;
}

TodoistClient::Error TodoistClient::fetchProjects(freeink::SecureHttpClient& http,
                                                  std::vector<TodoistProject>& outProjects) {
  lastHttpCode = 0;
  outProjects.clear();
  if (!TODOIST_STORE.hasToken()) {
    LOG_DBG("TDA", "No API token configured");
    return NO_TOKEN;
  }
  if (insufficientHeap()) return LOW_MEMORY;

  std::vector<std::pair<int32_t, TodoistProject>> collected;
  collected.reserve(TODOIST_MAX_PROJECTS);
  ProjectCollector collector{&collected};
  TodoistProjectsParser parser(collectProject, &collector);

  if (!http.begin(PROJECTS_URL)) {
    LOG_ERR("TDA", "Bad projects URL");
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);

  const int httpCode = http.GET([&parser](const uint8_t* data, const size_t len) {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  lastHttpCode = httpCode;
  LOG_DBG("TDA", "Projects response: %d (%zu projects parsed)", httpCode, parser.projectCount());

  const Error status = errorForStatus(httpCode);
  if (status != OK) return status;
  if (parser.hasError()) {
    LOG_ERR("TDA", "Malformed projects JSON");
    return PARSE_ERROR;
  }

  // The app's own order, not the response's: child_order is each project's
  // position among its siblings. Stable, so equal orders keep the response order.
  std::stable_sort(collected.begin(), collected.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  outProjects.reserve(collected.size());
  for (auto& entry : collected) outProjects.push_back(std::move(entry.second));
  return OK;
}

TodoistClient::Error TodoistClient::closeTask(freeink::SecureHttpClient& http, const std::string& taskId) {
  lastHttpCode = 0;
  if (!TODOIST_STORE.hasToken()) return NO_TOKEN;
  if (taskId.empty()) return SERVER_ERROR;
  if (insufficientHeap()) return LOW_MEMORY;

  const std::string url = std::string(API_BASE) + "/tasks/" + taskId + "/close";
  if (!http.begin(url)) {
    LOG_ERR("TDA", "Bad close URL for task %s", taskId.c_str());
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  // The close call takes no body; SecureHttpClient only emits Content-Length
  // for a non-empty payload, and a bodyless POST without it risks a 411.
  http.addHeader("Content-Length", "0");

  const int httpCode = http.sendRequest("POST", nullptr, 0);
  lastHttpCode = httpCode;
  LOG_DBG("TDA", "Close %s: %d", taskId.c_str(), httpCode);

  // Already completed (or deleted) elsewhere is the state we wanted anyway.
  if (httpCode == 404) return OK;
  return errorForStatus(httpCode);
}

TodoistClient::Error TodoistClient::rescheduleTask(freeink::SecureHttpClient& http, const std::string& taskId,
                                                   const std::string& isoDueDate) {
  lastHttpCode = 0;
  if (!TODOIST_STORE.hasToken()) return NO_TOKEN;
  if (taskId.empty()) return SERVER_ERROR;
  if (insufficientHeap()) return LOW_MEMORY;

  const std::string url = std::string(API_BASE) + "/tasks/" + taskId;

  // Built by hand rather than through ArduinoJson: one field is not worth a
  // document. An empty isoDueDate means "clear the due date" - see this
  // method's own header comment for why due_string is what does that.
  char body[48];
  if (isoDueDate.empty()) {
    snprintf(body, sizeof(body), "{\"due_string\":\"no date\"}");
  } else {
    snprintf(body, sizeof(body), "{\"due_date\":\"%s\"}", isoDueDate.c_str());
  }

  if (!http.begin(url)) {
    LOG_ERR("TDA", "Bad reschedule URL for task %s", taskId.c_str());
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  http.addHeader("Content-Type", "application/json");

  const int httpCode = http.POST(body);
  lastHttpCode = httpCode;
  LOG_DBG("TDA", "Reschedule %s -> %s: %d", taskId.c_str(), isoDueDate.empty() ? "no date" : isoDueDate.c_str(),
          httpCode);

  // Unlike closeTask(), a 404 here means the task cannot be rescheduled at
  // all - there is no equally-good "already done" reading of it - so it
  // falls straight through errorForStatus() as NOT_FOUND.
  return errorForStatus(httpCode);
}

TodoistClient::Error TodoistClient::fetchCompletedCountForDay(freeink::SecureHttpClient& http,
                                                              const std::string& filter, const std::string& isoDate,
                                                              uint16_t& outCount,
                                                              const TodoistCompletedCountParser::ItemSink itemSink,
                                                              void* itemSinkCtx) {
  lastHttpCode = 0;
  outCount = 0;
  if (!TODOIST_STORE.hasToken()) {
    LOG_DBG("TDA", "No API token configured");
    return NO_TOKEN;
  }
  if (insufficientHeap()) return LOW_MEMORY;

  const std::string url = std::string(COMPLETED_URL_BASE) + "?since=" + isoDate + "T00:00:00Z&until=" + isoDate +
                          "T23:59:59Z&limit=" + std::to_string(COMPLETED_PAGE_LIMIT) +
                          "&filter_query=" + urlEncode(filter);

  TodoistCompletedCountParser parser(itemSink, itemSinkCtx);

  if (!http.begin(url)) {
    LOG_ERR("TDA", "Bad completed-count URL");
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);

  const int httpCode = http.GET([&parser](const uint8_t* data, const size_t len) {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  lastHttpCode = httpCode;
  LOG_DBG("TDA", "Completed-count response: %d (%zu items)", httpCode, parser.count());

  const Error status = errorForStatus(httpCode);
  if (status != OK) return status;
  if (parser.hasError()) {
    LOG_ERR("TDA", "Malformed completed-tasks JSON");
    return PARSE_ERROR;
  }

  outCount = static_cast<uint16_t>(std::min<size_t>(parser.count(), UINT16_MAX));
  return OK;
}

const char* TodoistClient::errorString(const Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_TOKEN:
      return "No API token configured";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Invalid API token";
    case SERVER_ERROR:
      return "Todoist server error";
    case INVALID_FILTER:
      return "Todoist rejected the filter";
    case PARSE_ERROR:
      return "Unexpected response";
    case LOW_MEMORY:
      return "Not enough memory to sync";
    case NOT_FOUND:
      return "No such task";
    default:
      return "Unknown error";
  }
}
