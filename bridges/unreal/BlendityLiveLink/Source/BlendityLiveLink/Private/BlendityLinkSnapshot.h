// SPDX-License-Identifier: GPL-2.0-or-later
// Send to Blendity (round 374, task 0351): the open level - or the selected actors - written as a Blendity snapshot
// (docs/LIVELINK.md): Unreal's own FBX exporter's model and snapshot.json (blendity_link::snapshot_json) with each
// actor's transform, lights, cameras and Blueprint class. It goes into the inbox Blendity's discovery file names, so
// Blendity imports it within a second; without Blendity running, into Saved/Blendity for a drop.
#pragma once

#include "CoreMinimal.h"

/* Writes the snapshot; true and where it went (or why not) in `Message`. */
bool BlendityWriteSnapshot(bool bSelectedOnly, FString& Message);
