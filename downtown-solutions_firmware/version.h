//-------------------------------------------------
//                  NAVA v1.x
//                firmware version
//-------------------------------------------------
//
// The one place the version is written down. The boot splash prints it, the
// release workflow reads it out of this file, and the tag being published must
// equal it - a release whose number disagrees with what the panel shows is
// worse than no release, because the number on the panel is the only version a
// user in front of the machine can see.
//
// `scripts/release.py X.Y` rewrites the string below and pushes the tag; nothing else
// should edit it by hand.

#ifndef NAVA_VERSION_H
#define NAVA_VERSION_H

#define FIRMWARE_VERSION "0.99"

// The splash line is " solutions " + the version, on a 16-column display.
// A version long enough to overflow would silently lose its tail into
// off-screen DDRAM, so it is caught here instead of on the hardware.
// Guarded for C: the sketch is C++, but this header is also the file the release
// tooling parses, and nothing should break if a .c ever includes it.
#ifdef __cplusplus
static_assert(sizeof(" solutions " FIRMWARE_VERSION) - 1 <= 16,
              "FIRMWARE_VERSION is too long for the 16-column splash line");
#endif

#endif  // NAVA_VERSION_H
