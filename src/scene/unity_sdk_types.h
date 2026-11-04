// SPDX-License-Identifier: GPL-2.0-or-later
// Round 403 (ADR 0101): the names of VRChat's SDK classes by the fileID Unity gives a class in a DLL - the first four
// bytes of MD4("s\0\0\0" + the namespace + the class, no dot between) as a little-endian int32 - so a scene's "MonoBehaviour (guid:fileID)"
// reads "VRCMirrorReflection"; and the fileID of any full name (a test, the bridge's checks).
#pragma once
#include <cstdint>
#include <string>

namespace bl {

/* Unity's fileID of a class in a DLL from its full name ("VRC.SDK3.Components.VRCPickup"). */
int32_t unity_script_file_id(const std::string &full_name);
/* The class's short name for a fileID of the VRChat SDK's (and Light Volumes') known classes; "" when unknown. */
std::string unity_sdk_class_name(int32_t file_id);
/* Every known class, "Namespace.Class" (for the type table's check). */
const char *const *unity_sdk_class_names(size_t *count);

}  // namespace bl
