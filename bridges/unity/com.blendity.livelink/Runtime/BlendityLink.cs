// SPDX-License-Identifier: GPL-2.0-or-later
// On every GameObject Blendity's Live Link made: the Editor bridge finds its objects again by Id. With VRChat's SDK in the
// project it is one of VRChat's editor-only components (round 396: the SDK's checks had refused an avatar carrying it -
// "will be removed by the client"); its builds drop it without a word.
using UnityEngine;

namespace Blendity.LiveLink
{
    [DisallowMultipleComponent]
    public sealed class BlendityLink : MonoBehaviour
#if BLENDITY_VRCSDK
        , VRC.SDKBase.IEditorOnly
#endif
    {
        [Tooltip("The object's id in Blendity (stable through undo and Play).")]
        public long Id;
        [Tooltip("mesh, light, camera, armature or empty.")]
        public string Kind = "empty";
        [Tooltip("The armature object this bone belongs to (0: not a bone).")]
        public long BoneOf;
    }
}
