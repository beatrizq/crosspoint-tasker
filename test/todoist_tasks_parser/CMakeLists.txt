add_executable(TodoistTasksParserTest
  TodoistTasksParserTest.cpp
  TodoistProjectsParserTest.cpp
  TodoistCompletedCountParserTest.cpp
  TodoistDateTest.cpp
  ${REPO_ROOT}/lib/Todoist/TodoistTasksParser.cpp
  ${REPO_ROOT}/lib/Todoist/TodoistProjectsParser.cpp
  ${REPO_ROOT}/lib/Todoist/TodoistCompletedCountParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(TodoistTasksParserTest PRIVATE
  ${REPO_ROOT}/lib/Todoist
  ${REPO_ROOT}/lib/CivilTime
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(TodoistTasksParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TodoistTasksParserTest)
