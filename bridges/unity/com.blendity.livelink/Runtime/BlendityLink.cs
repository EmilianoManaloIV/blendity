// SPDX-License-Identifier: GPL-2.0-or-later
// On every GameObject Blendity's Live Link made: the Editor bridge finds its objects again by Id.
using UnityEngine;

namespace Blendity.LiveLink
{
    [DisallowMultipleComponent]
    public sealed class BlendityLink : MonoBehaviour
    {
        [Tooltip("The object's id in Blendity (stable through undo and Play).")]
        public long Id;
        [Tooltip("mesh, light, camera, armature or empty.")]
        public string Kind = "empty";
        [Tooltip("The armature object this bone belongs to (0: not a bone).")]
        public long BoneOf;
    }
}
