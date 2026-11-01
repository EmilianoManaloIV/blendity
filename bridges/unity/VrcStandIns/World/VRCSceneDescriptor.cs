// SPDX-License-Identifier: GPL-2.0-or-later
// A stand-in of VRChat's SDK type by its documented full name and fields (round 381), for the bridge's replay test
// in a scratch project (run_replay_tests.py copies it there). Not part of the package: never in a user's project.
using UnityEngine;
namespace VRC.SDK3.Components {
public class VRCSceneDescriptor : MonoBehaviour {
    public Transform[] spawns;
}
}
