#pragma once

#include <CivilTime.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

/**
 * Date picker for rescheduling a Todoist task: three editable fields (day,
 * month, year), modelled directly on ClockOffsetActivity's sign/hours/minutes
 * picker. Confirm cycles the active field; -/+ adjust it. Like
 * ClockOffsetActivity, Back commits the currently shown date and reports it
 * back via ActivityResult (DateResult) - there is no separate cancel gesture,
 * since a picker seeded from the task's own due date and left untouched
 * simply reschedules it to the same day it already had.
 */
class RescheduleTaskActivity final : public Activity {
 public:
  // `initialDate` seeds the three fields - packed days since 2000-01-01, the
  // same form TodoistTask::dueDays uses (see CivilTime.h). Pass
  // TODOIST_TASKS.getSyncDate() converted to a packed date for "default to
  // today".
  RescheduleTaskActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint16_t initialDate)
      : Activity("RescheduleTask", renderer, mappedInput), initialDate(initialDate) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  enum Field { FIELD_DAY = 0, FIELD_MONTH = 1, FIELD_YEAR = 2, FIELD_COUNT };
  Field activeField = FIELD_DAY;

  const uint16_t initialDate;
  // Working copy of the date, edited in place. Bounds match civil::packDate's
  // representable range (years 2000..2179).
  int32_t year = 2000;
  uint32_t month = 1;
  uint32_t day = 1;

  // Screen was entered with Confirm already down (still held from picking
  // "Reschedule" off the Options popup, which answers on the press) - its
  // eventual release must not be read as a fresh press cycling the field.
  bool swallowConfirmRelease = false;

  void loadInitialDate();
  uint16_t packedDate() const;
  uint32_t daysInActiveMonth() const;
  void adjustActiveField(int delta);
  bool fieldFromPoint(int x, int y, Field& field) const;
  // "Front buttons: 1" / "Side buttons: 5" -- same legend and wording
  // IntervalSelectionActivity uses for its own front/side step split.
  void drawStepHintLine(int y, StrId labelId, int step) const;
};
