#pragma once
#include "settings.h"

// The glow's dimensions on screen for a picture of a given size, px.
struct GlowMetrics {
  float marginPx;      // how far the glow reaches beyond the frame before the blur
  float edgePx;        // depth of the picture's edge band averaged into the glow
  float sigmaPx;       // gaussian blur sigma
  float transitionPx;  // width of the sharp-to-blurred transition next to the picture
};

constexpr float kProjectorMin = 160.f;  // projector short side, texels

// Glow geometry in screen px from the (unclipped) picture size. Percentages are of half the picture's
// long side; the reach is continuous, not a whole number of enlarged copies.
GlowMetrics ComputeGlowMetrics(const Settings& s, float pictureW, float pictureH);

// How far the glow is visible beyond the frame, px: the reach plus three blur sigmas.
float GlowReach(const Settings& s, float pictureW, float pictureH);

// Slider 1..100 -> exponent of the fade curve. The slider moves the point where half the light is gone
// geometrically from 95% to 10% of the reach, so every step looks like the same amount of change.
float FadeExponent(int curve);

// Vibrance slider (0..200) -> shader strength (-1..1), eased.
float VibranceStrength(int vibrance);
