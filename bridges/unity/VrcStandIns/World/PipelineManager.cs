// SPDX-License-Identifier: GPL-2.0-or-later
// A stand-in of VRChat's pipeline manager (round 396: the real SDK wants one beside each descriptor), for the bridge's
// replay test in a scratch project (run_replay_tests.py copies it there). Not part of the package: never in a user's project.
using UnityEngine;
namespace VRC.Core {
public class PipelineManager : MonoBehaviour {
    public string blueprintId;
}
}
