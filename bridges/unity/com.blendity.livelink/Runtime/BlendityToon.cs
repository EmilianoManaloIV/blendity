// SPDX-License-Identifier: GPL-2.0-or-later
// For a renderer with Toon materials from Blendity's Live Link: their fields by slot (docs/LIVELINK.md, BLENDITY_toon:
// every field by its Inspector name) for a toon shader of your own to read. (Its own file: Unity finds a component's
// script by the file's name.)
using UnityEngine;

namespace Blendity.LiveLink
{
    [DisallowMultipleComponent]
    public sealed class BlendityToon : MonoBehaviour
    {
        [Tooltip("Per material slot: the Toon material's fields as JSON (empty for a PBR material).")]
        public string[] SlotFields = new string[0];
    }
}
