// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// A tablet's axes by their names (task 0219): X11's XInput 2 labels a device's valuators ("Abs Pressure", "Abs Tilt X",
// ...) and sends only the ones that changed, as a bit mask over the values. These read them without any X11 call, so
// the rules are tested on every platform.

#include <string>

namespace bl::pen {

enum class Axis { Other, Pressure, TiltX, TiltY };

/* The axis a valuator's label names (the kernel's and libinput's names, as Xorg's evdev and libinput drivers give); a
 * touch screen's "Abs MT ..." axes are none of them. */
Axis axis_of(const std::string &label);
/* A device that is a pen's eraser end (Wacom and libinput name it so). */
bool eraser_device(const std::string &name);
/* A value within [mn, mx] as 0..1; 0 when the range is empty. */
float unit(double v, double mn, double mx);
/* A tilt within [mn, mx] as degrees from upright, -90..90: a range within +-90 is degrees already (Xorg's drivers),
 * any other is stretched over it. */
float tilt_degrees(double v, double mn, double mx);
/* Valuator `index`'s value from an event's mask and its values (one value per set bit, in order): false when the
 * event didn't carry it. */
bool valuator(const unsigned char *mask, int mask_len, const double *values, int index, double &out);

}  // namespace bl::pen
