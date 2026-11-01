// SPDX-License-Identifier: GPL-2.0-or-later
// A stand-in of VRChat's SDK type by its documented full name and fields (round 381), for the bridge's replay test
// in a scratch project (run_replay_tests.py copies it there). Not part of the package: never in a user's project.
using UnityEngine;
namespace VRC.SDKBase {
public class VRC_AvatarDescriptor : MonoBehaviour {
    public enum LipSyncStyle { Default, JawFlapBone, JawFlapBlendShape, VisemeBlendShape, VisemeParameterOnly }
    public Vector3 ViewPosition;
    public LipSyncStyle lipSync;
    public SkinnedMeshRenderer VisemeSkinnedMesh;
    public string[] VisemeBlendShapes;
}
}
