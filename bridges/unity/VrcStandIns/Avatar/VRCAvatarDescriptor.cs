// SPDX-License-Identifier: GPL-2.0-or-later
// A stand-in of VRChat's SDK type by its documented full name and fields (round 381), for the bridge's replay test
// in a scratch project (run_replay_tests.py copies it there). Not part of the package: never in a user's project.
using UnityEngine;
namespace VRC.SDK3.Avatars.Components {
public class VRCAvatarDescriptor : VRC.SDKBase.VRC_AvatarDescriptor {
    public enum EyelidType { None, Bones, Blendshapes }
    [System.Serializable]
    public struct CustomEyeLookSettings {
        public Transform leftEye, rightEye;
        public EyelidType eyelidType;
        public SkinnedMeshRenderer eyelidsSkinnedMesh;
        public int[] eyelidsBlendshapes;
    }
    public bool enableEyeLook;
    public CustomEyeLookSettings customEyeLookSettings;
}
}
