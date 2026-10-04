#include "CompanionRenderer.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>

namespace companion {
namespace {

// Radius is at most 12, so a plain search beats pulling in <cmath> for sqrt.
int isqrt(const int value) {
  if (value <= 0) return 0;
  int root = 0;
  while ((root + 1) * (root + 1) <= value) root++;
  return root;
}

// Half-plane test fill. The tail is only ~12x20 px, so testing its bounding box
// is cheaper than sorting edges, and it is correct for either winding order.
void fillTriangle(const GfxRenderer& renderer, const int x0, const int y0, const int x1, const int y1, const int x2,
                  const int y2, const bool state) {
  const int minX = std::min({x0, x1, x2});
  const int maxX = std::max({x0, x1, x2});
  const int minY = std::min({y0, y1, y2});
  const int maxY = std::max({y0, y1, y2});

  const auto edge = [](int ax, int ay, int bx, int by, int px, int py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
  };

  for (int py = minY; py <= maxY; py++) {
    for (int px = minX; px <= maxX; px++) {
      const int e0 = edge(x0, y0, x1, y1, px, py);
      const int e1 = edge(x1, y1, x2, y2, px, py);
      const int e2 = edge(x2, y2, x0, y0, px, py);
      if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0)) {
        renderer.drawPixel(px, py, state);
      }
    }
  }
}

}  // namespace

void drawPose(const GfxRenderer& renderer, const CompanionId id, const Mood mood, const int x, const int y,
              const int scale, const bool mirrored) {
  if (scale < 1) return;

  const auto companionIndex = static_cast<uint8_t>(id);
  const auto moodIndex = static_cast<uint8_t>(mood);
  if (companionIndex >= COMPANION_COUNT || moodIndex >= MOOD_COUNT) {
    LOG_ERR("COMP", "Sprite index out of range: companion %u mood %u", companionIndex, moodIndex);
    return;
  }

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const uint8_t* bits = COMPANION_SPRITES[companionIndex][moodIndex];

  for (int row = 0; row < SPRITE_HEIGHT; row++) {
    const uint8_t* rowBits = bits + row * SPRITE_ROW_BYTES;
    const int baseY = y + row * scale;
    // Whole sprite row lands off-screen: skip its columns entirely.
    if (baseY + scale <= 0 || baseY >= screenHeight) continue;

    for (int col = 0; col < SPRITE_WIDTH; col++) {
      // Mirroring reads the opposite column while writing to the same place, so
      // the flip costs nothing beyond one subtraction.
      const int srcCol = mirrored ? SPRITE_WIDTH - 1 - col : col;
      if (((rowBits[srcCol >> 3] >> (7 - (srcCol & 7))) & 1) == 0) continue;

      const int baseX = x + col * scale;
      if (baseX + scale <= 0 || baseX >= screenWidth) continue;

      for (int dy = 0; dy < scale; dy++) {
        const int py = baseY + dy;
        if (py < 0 || py >= screenHeight) continue;
        for (int dx = 0; dx < scale; dx++) {
          const int px = baseX + dx;
          if (px < 0 || px >= screenWidth) continue;
          renderer.drawPixel(px, py, true);
        }
      }
    }
  }
}

void drawSpeechBubble(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                      const int tailLength, const TailSide side, const int lineWidth, const bool filled) {
  if (w <= 4 || h <= 4) return;

  const int radius = std::min({10, w / 3, h / 3});
  // Never thicker than the corner radius itself -- same clamp
  // GfxRenderer::drawRoundedRect applies to its own lineWidth, so a bubble
  // asked for an implausibly thick border degrades the same way a tile's
  // selection outline would rather than drawing outside the rounded corner.
  const int stroke = std::max(1, std::min(lineWidth, radius));
  const int left = x;
  const int top = y;
  const int right = x + w - 1;
  const int bottom = y + h - 1;

  // Clear to paper (or fill black, when `filled`) first, following the
  // rounded edge, so the text that follows is never sitting on top of
  // whatever was behind the bubble.
  for (int row = 0; row < h; row++) {
    int inset = 0;
    if (row < radius) {
      const int dy = radius - row;
      inset = radius - isqrt(radius * radius - dy * dy);
    } else if (row >= h - radius) {
      const int dy = row - (h - 1 - radius);
      inset = radius - isqrt(radius * radius - dy * dy);
    }
    renderer.fillRect(left + inset, top + row, w - 2 * inset, 1, filled);
  }

  // Straight runs between the corner arcs, `stroke` pixels thick -- same
  // technique GfxRenderer::drawRoundedRect uses for its own border.
  renderer.fillRect(left + radius, top, w - 2 * radius, stroke, true);
  renderer.fillRect(left + radius, bottom - stroke + 1, w - 2 * radius, stroke, true);
  renderer.fillRect(left, top + radius, stroke, h - 2 * radius, true);
  renderer.fillRect(right - stroke + 1, top + radius, stroke, h - 2 * radius, true);
  renderer.drawArc(radius, left + radius, top + radius, -1, -1, stroke, true);
  renderer.drawArc(radius, right - radius, top + radius, 1, -1, stroke, true);
  renderer.drawArc(radius, right - radius, bottom - radius, 1, 1, stroke, true);
  renderer.drawArc(radius, left + radius, bottom - radius, -1, 1, stroke, true);

  if (tailLength <= 0) return;

  // Tail angled towards whoever is talking, the way a comic bubble points. The
  // base is kept near the tail's own length: a base much wider than the reach
  // reads as a shallow flap rather than a pointer. Paper (or black, when
  // `filled`) fill goes down first, which also erases the body edge between
  // the base points, so the tail opens into the bubble instead of being a
  // stuck-on shape.
  if (side == TailSide::Bottom) {
    const int baseHalf = std::max(3, std::min(tailLength / 2, w / 8));
    const int midX = left + (2 * w) / 3;  // off-centre, towards the character's head
    const int baseLeftX = midX - baseHalf;
    const int baseRightX = midX + baseHalf;
    const int tipY = bottom + tailLength;
    const int tipX = baseLeftX - tailLength / 3;
    fillTriangle(renderer, baseLeftX, bottom, baseRightX, bottom, tipX, tipY, filled);
    renderer.drawLine(baseLeftX, bottom, tipX, tipY, stroke, true);
    renderer.drawLine(tipX, tipY, baseRightX, bottom, stroke, true);
    return;
  }

  const int baseHalf = std::max(3, std::min(tailLength / 2, h / 8));
  const int midY = top + h / 2;
  const int baseTopY = midY - baseHalf;
  const int baseBottomY = midY + baseHalf;
  const int tipX = left - tailLength;
  const int tipY = baseBottomY + tailLength / 3;
  fillTriangle(renderer, left, baseTopY, left, baseBottomY, tipX, tipY, filled);
  renderer.drawLine(left, baseTopY, tipX, tipY, stroke, true);
  renderer.drawLine(tipX, tipY, left, baseBottomY, stroke, true);
}

BubbleFit fitBubbleText(const GfxRenderer& renderer, const int fontId, const std::string& text, const int maxTextWidth,
                        const int minTextWidth, const int maxLines) {
  BubbleFit fit;
  if (text.empty()) {
    fit.textWidth = minTextWidth;
    return fit;
  }

  const int naturalWidth = renderer.getTextWidth(fontId, text.c_str());
  if (naturalWidth <= maxTextWidth) {
    fit.lines.push_back(text);
    fit.textWidth = std::max(minTextWidth, naturalWidth);
  } else {
    fit.lines = renderer.wrappedText(fontId, text.c_str(), maxTextWidth, maxLines);
    fit.textWidth = maxTextWidth;
  }
  return fit;
}

const char* idleBubbleText(const Mood mood, const uint8_t variant) {
  const uint8_t v = variant % IDLE_BUBBLE_VARIANT_COUNT;
  switch (mood) {
    case Mood::Break:  // Nothing left to do reads like a good day -- same lines as Happy.
    case Mood::Happy:
      switch (v) {
        case 0:
          return tr(STR_COMPANION_IDLE_HAPPY_1);
        case 1:
          return tr(STR_COMPANION_IDLE_HAPPY_2);
        case 2:
          return tr(STR_COMPANION_IDLE_HAPPY_3);
        case 3:
          return tr(STR_COMPANION_IDLE_HAPPY_4);
        default:
          return tr(STR_COMPANION_IDLE_HAPPY_5);
      }
    case Mood::Satisfied:
      switch (v) {
        case 0:
          return tr(STR_COMPANION_IDLE_SATISFIED_1);
        case 1:
          return tr(STR_COMPANION_IDLE_SATISFIED_2);
        case 2:
          return tr(STR_COMPANION_IDLE_SATISFIED_3);
        case 3:
          return tr(STR_COMPANION_IDLE_SATISFIED_4);
        default:
          return tr(STR_COMPANION_IDLE_SATISFIED_5);
      }
    case Mood::Cranky:
      switch (v) {
        case 0:
          return tr(STR_COMPANION_IDLE_CRANKY_1);
        case 1:
          return tr(STR_COMPANION_IDLE_CRANKY_2);
        case 2:
          return tr(STR_COMPANION_IDLE_CRANKY_3);
        case 3:
          return tr(STR_COMPANION_IDLE_CRANKY_4);
        default:
          return tr(STR_COMPANION_IDLE_CRANKY_5);
      }
    case Mood::Neglected:
      switch (v) {
        case 0:
          return tr(STR_COMPANION_IDLE_NEGLECTED_1);
        case 1:
          return tr(STR_COMPANION_IDLE_NEGLECTED_2);
        case 2:
          return tr(STR_COMPANION_IDLE_NEGLECTED_3);
        case 3:
          return tr(STR_COMPANION_IDLE_NEGLECTED_4);
        default:
          return tr(STR_COMPANION_IDLE_NEGLECTED_5);
      }
    case Mood::Amazed:
      switch (v) {
        case 0:
          return tr(STR_COMPANION_IDLE_AMAZED_1);
        case 1:
          return tr(STR_COMPANION_IDLE_AMAZED_2);
        case 2:
          return tr(STR_COMPANION_IDLE_AMAZED_3);
        case 3:
          return tr(STR_COMPANION_IDLE_AMAZED_4);
        default:
          return tr(STR_COMPANION_IDLE_AMAZED_5);
      }
    case Mood::Sleeping:
      return tr(STR_COMPANION_IDLE_SATISFIED_1);
    case Mood::Focus:
      // Unreached in practice: Focus is only ever shown by the focus-session
      // screens, which draw their own bubble text directly rather than going
      // through this function. Same placeholder Sleeping uses rather than
      // leaving it to the fallback below.
      return tr(STR_COMPANION_IDLE_SATISFIED_1);
  }
  return tr(STR_COMPANION_IDLE_SATISFIED_1);
}

}  // namespace companion
