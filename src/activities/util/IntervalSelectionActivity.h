#pragma once

#include <I18n.h>

#include <string>
#include <utility>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class GfxRenderer;

class IntervalSelectionActivity final : public Activity {
 public:
  // `customTitle` and `valueSuffix` are for callers whose title or unit is a
  // runtime string rather than a translated StrId (e.g. a task's name) - left empty, titleId/valueFormatId behave
  // exactly as before.
  explicit IntervalSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* activityName,
                                     StrId titleId, int initialValue, int minValue, int maxValue, int smallStep,
                                     int largeStep, StrId valueFormatId = StrId::STR_NONE_OPT,
                                     bool readerActivity = false, bool ignoreInitialConfirmRelease = false,
                                     StrId maxBoundaryLabelId = StrId::STR_NONE_OPT, std::string customTitle = "",
                                     std::string valueSuffix = "")
      : Activity(activityName, renderer, mappedInput),
        titleId(titleId),
        valueFormatId(valueFormatId),
        maxBoundaryLabelId(maxBoundaryLabelId),
        value(initialValue),
        minValue(minValue),
        maxValue(maxValue),
        smallStep(smallStep),
        largeStep(largeStep),
        readerActivity(readerActivity),
        ignoreConfirmRelease(ignoreInitialConfirmRelease),
        customTitle(std::move(customTitle)),
        valueSuffix(std::move(valueSuffix)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return readerActivity; }

 private:
  StrId titleId;
  StrId valueFormatId;
  StrId maxBoundaryLabelId;
  int value;
  int minValue;
  int maxValue;
  int smallStep;
  int largeStep;
  bool readerActivity;
  bool ignoreConfirmRelease;
  std::string customTitle;
  std::string valueSuffix;
  bool draggingBar = false;
  ButtonNavigator buttonNavigator;

  void adjustValue(int delta);
  int clampedValue(int candidate) const;
  void drawStepHintLine(int y, StrId labelId, int step);
  // The Y the slider bar sits at, centring the whole title/value/bar/hint
  // block within the screen rather than pinning it near the top. Shared by
  // loop() (touch/drag hit-testing) and render() so they can never disagree
  // on where the bar actually is.
  int barY() const;
};
