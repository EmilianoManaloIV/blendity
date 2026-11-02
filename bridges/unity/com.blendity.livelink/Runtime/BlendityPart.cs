// SPDX-License-Identifier: GPL-2.0-or-later
// Round 380: on a child the Editor bridge made for one of Live Link's objects - an LOD level, its collider - so the
// bridge finds it again by what it is, not by its name (a rename, an object of Blendity's named like one).
using UnityEngine;

namespace Blendity.LiveLink
{
    [DisallowMultipleComponent]
    public sealed class BlendityPart : MonoBehaviour
    {
        public const string Level = "lod", ColliderRole = "collider", PhysBonesRole = "physbones";
        [Tooltip("What the bridge made it for: lod, collider or physbones.")]
        public string Role = Level;
        [Tooltip("An LOD level's number (1 and up).")]
        public int Number;
    }
}
