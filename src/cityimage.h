#pragma once
#include <Arduino.h>

// 1-bit full-screen city photo (bit set = black), cached in NVS per location.
constexpr int IMG_W = 250;
constexpr int IMG_H = 122;
constexpr int IMG_STRIDE = (IMG_W + 7) / 8;
constexpr int IMG_BYTES = IMG_STRIDE * IMG_H;

// Fills `bits` if a photo is cached for `key`. `known` says whether we
// already tried this key (photo or "none available"), so we don't retry.
bool cityImageLoad(const String& key, uint8_t* bits, bool& known);

enum class ImgResult { Ok, NoImage, NetworkError };

// Looks up `title` on Wikipedia, downloads its lead photo, crops, dithers.
ImgResult cityImageFetch(const String& title, uint8_t* bits);

// Remember the result for `key` (bits == nullptr means "no photo exists").
void cityImageStore(const String& key, const uint8_t* bits);
