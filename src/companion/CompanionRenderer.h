#pragma once
#include <CompanionMood.h>
#include <GfxRenderer.h>

#include <cstdint>
#include <string>
#include <vector>

#include "CompanionSprites.generated.h"

namespace companion {

// Logical footprint of a pose at the given integer scale, for layout maths.
constexpr int poseWidth(const int scale) { return SPRITE_WIDTH * scale; }
constexpr int poseHeight(const int scale) { return SPRITE_HEIGHT * scale; }

/**
 * Draws one companion pose with its top-left at (x, y), magnified by an integer
 * scale (1 = one framebuffer pixel per sprite pixel).
 *
 * Every pixel goes through GfxRenderer::drawPixel, so the renderer's own
 * coordinate transform handles all four screen orientations and no sprite data
 * has to be pre-rotated. Pixels outside the screen are dropped here rather than
 * by drawPixel, which logs an error per out-of-bounds write.
 *
 * Nothing is scaled by fractions: e-ink at this size needs hard edges, and
 * integer scaling keeps the baked dither pattern intact.
 */
// `mirrored` flips the sprite horizontally so a walking character can face the
// way it is travelling. The sprites are symmetric enough to read either way, so
// no second set of art is needed.
void drawPose(const GfxRenderer& renderer, CompanionId id, Mood mood, int x, int y, int scale, bool mirrored = false);

// Which edge the tail hangs off, so the bubble can point at a companion beside
// it or below it.
enum class TailSide : uint8_t { Left, Bottom };

/**
 * Draws a rounded speech bubble with a tail pointing at the speaker.
 *
 * The interior is cleared to paper (or filled black, see `filled`) before the
 * outline is stroked, so callers can draw text straight afterwards without
 * worrying about what was underneath. `tailLength` is how far the tail
 * reaches beyond the bubble body. `lineWidth` defaults to the same thickness
 * a selected tile's outline uses elsewhere (see LyraTheme's
 * selectionLineWidth) so the two selection-adjacent strokes read as the same
 * weight.
 *
 * `filled` draws the body and tail interior in ink instead of paper --
 * a "hovered" bubble, black background and (caller's job) white text, that
 * follows this exact rounded/tailed outline rather than a plain
 * GfxRenderer::invertRect() over the bounding rect, which would also invert
 * the paper-colored corners just outside the curve and the gaps to either
 * side of the tail.
 */
void drawSpeechBubble(const GfxRenderer& renderer, int x, int y, int w, int h, int tailLength,
                      TailSide side = TailSide::Left, int lineWidth = 2, bool filled = false);

// Wrapped lines plus the text column width the bubble should actually use --
// see fitBubbleText().
struct BubbleFit {
  std::vector<std::string> lines;
  int textWidth;
};

/**
 * Wraps `text` at `fontId` against `maxTextWidth`, and reports how wide the
 * bubble's text column actually needs to be: the text's own single-line
 * width when it fits without wrapping, so a short line's bubble does not
 * stretch to fill whatever room happened to be available, or `maxTextWidth`
 * itself once wrapping kicks in, since that is what the wrap points were
 * chosen against. Never narrower than `minTextWidth`, so a one-word line
 * still gets a bubble with room for the tail and rounded corners.
 */
BubbleFit fitBubbleText(const GfxRenderer& renderer, int fontId, const std::string& text, int maxTextWidth,
                        int minTextWidth, int maxLines);

// How many distinct idle remarks each mood has (see idleBubbleText()).
constexpr int IDLE_BUBBLE_VARIANT_COUNT = 5;

// The bubble's default line when there is nothing else to show (no alert
// waiting) -- a plain idle remark, five distinct variants per mood so the
// same line doesn't repeat every time the bubble falls back to idle.
// `variant` selects which one (wrapped into range, so any value is safe);
// the caller owns picking and remembering it -- see QuickPickActivity's own
// idleVariant member -- so this stays a pure, deterministic function of its
// arguments rather than reaching for its own randomness. Never called for
// Sleeping (that mood has its own fixed bubble line).
const char* idleBubbleText(Mood mood, uint8_t variant);

}  // namespace companion
