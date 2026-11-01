// SPDX-License-Identifier: GPL-2.0-or-later
// EditMode test (round 318): the package follows a running Blendity, not a recording. Blendity's own test runs it
// (tests_round318 in tests/test_livelink.cpp, BLENDITY_TEST_BRIDGE naming a Unity -runTests command for this class):
// the package connects by itself through the discovery file and, once it has the Box, writes BLENDITY_BRIDGE_DUMP.ready.
// Blendity then moves, recolours and deforms the Box, turns a bone, deletes the Sphere, makes "Late" and deforms the
// dense sphere "Dense" ten times, the last to radius 1.2, and stops Live Link. When the discovery file goes, the scene
// is checked and the report written to BLENDITY_BRIDGE_DUMP ("LIVE OK" or "LIVE FAILED"). Skipped without it.
using System.Collections;
using System.Collections.Generic;
using System.IO;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using UnityEngine.TestTools;

namespace Blendity.LiveLink.Tests
{
    public class LiveSessionTests
    {
        readonly List<string> lines = new List<string>();
        int failures;

        void Check(bool ok, string what)
        {
            lines.Add((ok ? "ok: " : "FAILED: ") + what);
            if (!ok)
                failures++;
        }

        static bool HasMesh(string name)
        {
            var g = GameObject.Find(name);
            var f = g != null ? g.GetComponent<MeshFilter>() : null;
            return f != null && f.sharedMesh != null && f.sharedMesh.vertexCount > 0;
        }

        [TearDown]
        public void Leave()
        {
            LiveLinkClient.Disconnect();
            var root = GameObject.Find(LiveLinkApplier.RootName);
            if (root != null)
                Object.DestroyImmediate(root);
        }

        [UnityTest, Timeout(900000)]
        public IEnumerator FollowsALiveBlendity()
        {
            var dump = System.Environment.GetEnvironmentVariable("BLENDITY_BRIDGE_DUMP");
            if (string.IsNullOrEmpty(dump))
            {
                Assert.Ignore("No BLENDITY_BRIDGE_DUMP: Blendity's live test runs this (tests_round318)");
                yield break;
            }
            LiveLinkClient.AutoConnect = true;
            LiveLinkClient.TryAutoConnect();
            bool ready = false, ended = false;
            double longest = 0, last = Now();
            var start = Now();
            while (Now() - start < 600)
            {
                bool dense = HasMesh("Dense");
                yield return null;  // (the editor's update reads the stream and applies it)
                var now = Now();
                if (dense)
                    longest = System.Math.Max(longest, (now - last) * 1000.0);
                last = now;
                if (!ready && LiveLinkClient.Connected && GameObject.Find("Box") != null)
                {
                    ready = true;
                    File.WriteAllText(dump + ".ready", "Unity " + Application.unityVersion + "\n");
                }
                if (ready && !File.Exists(LiveLinkDiscovery.FilePath()))
                {
                    ended = true;
                    break;
                }
                System.Threading.Thread.Sleep(5);
            }
            Check(ended, "followed until Blendity stopped Live Link (" + LiveLinkClient.Messages + " messages)");
            CheckScene();
            lines.Add("app: Unity " + Application.unityVersion + "; the longest editor frame once the dense mesh was in: " + longest.ToString("0.0") + " ms");
            lines.Add(failures == 0 ? "LIVE OK" : "LIVE FAILED: " + failures + " problem(s)");
            File.WriteAllText(dump, string.Join("\n", lines) + "\n");
            Debug.Log(string.Join("\n", lines));
            Assert.AreEqual(0, failures, string.Join("\n", lines));
        }

        static double Now()
        {
            return System.Diagnostics.Stopwatch.GetTimestamp() / (double)System.Diagnostics.Stopwatch.Frequency;
        }

        void CheckScene()
        {
            Check(GameObject.Find("Sphere") == null, "the Sphere deleted");
            var box = GameObject.Find("Box");
            Check(box != null, "the Box");
            if (box != null)
            {
                var p = box.transform.localPosition;  // (Unity's axes are Blendity's)
                Check(Vector3.Distance(p, new Vector3(1, 2, 3)) < 1e-4f, "the Box moved to " + p.ToString("F4"));
                var r = box.GetComponent<Renderer>();
                var c = r != null && r.sharedMaterial != null ? r.sharedMaterial.color : Color.black;  // (the shader's main colour: _Color or URP's _BaseColor)
                var want = new Color(0.9f, 0.4f, 0.2f).gamma;  // (Live Link's colours are linear; the material's gamma - round 377)
                Check(Mathf.Abs(c.r - want.r) < 1e-3f && Mathf.Abs(c.g - want.g) < 1e-3f && Mathf.Abs(c.b - want.b) < 1e-3f, "the Box recolored: " + c);
                var moved = new HashSet<float>();
                foreach (var v in box.GetComponent<MeshFilter>().sharedMesh.vertices)
                    if (Mathf.Abs(Mathf.Abs(v.x) - 0.5f) > 1e-4f)
                        moved.Add(Mathf.Round(v.x * 10000f) / 10000f);
                Check(moved.Count == 1, "the Box's vertex moved: " + string.Join(", ", moved));
            }
            var lower = GameObject.Find("Lower");
            var angle = lower != null ? Quaternion.Angle(lower.transform.localRotation, Quaternion.identity) : 0f;
            Check(Mathf.Abs(angle - 30f) < 1e-2f, "the bone turned: " + angle.ToString("0.000") + " degrees");
            var late = GameObject.Find("Late");
            Check(late != null && Vector3.Distance(late.transform.localPosition, new Vector3(0, 5, 0)) < 1e-4f,
                  "the object made live at " + (late != null ? late.transform.localPosition.ToString("F4") : "nowhere"));
            var dense = GameObject.Find("Dense");
            float radius = 0f;
            var mf = dense != null ? dense.GetComponent<MeshFilter>() : null;
            if (mf != null && mf.sharedMesh != null)
                foreach (var v in mf.sharedMesh.vertices)
                    radius = Mathf.Max(radius, v.magnitude);
            Check(Mathf.Abs(radius - 1.2f) < 1e-3f, "the dense mesh's last deform: radius " + radius.ToString("0.0000"));
            CheckFlows(box);
        }

        /* Round 382: what flows since round 376, live - the fog, the Box's checker by its pixels, the skinned Arm's Smile
         * at 0.5, the rig's Wave, the Crate's levels and collider. */
        void CheckFlows(GameObject box)
        {
            Check(RenderSettings.fog && Mathf.Abs(RenderSettings.fogStartDistance - 5f) < 1e-4f, "the fog from 5 m");
            var boxRenderer = box != null ? box.GetComponent<Renderer>() : null;
            var tex = boxRenderer != null && boxRenderer.sharedMaterial != null ? boxRenderer.sharedMaterial.mainTexture : null;
            Check(tex != null && tex.width == 4, "the Box's 4 x 4 checker");
            var arm = GameObject.Find("Arm");
            var smr = arm != null ? arm.GetComponent<SkinnedMeshRenderer>() : null;
            Check(smr != null && smr.bones.Length > 0, "the Arm skinned on the rig");
            Check(smr != null && smr.sharedMesh != null && smr.sharedMesh.blendShapeCount == 1 && Mathf.Abs(smr.GetBlendShapeWeight(0) - 50f) < 1e-3f, "the Arm's Smile at 0.5");
            const string wave = LiveLinkApplier.ClipFolder + "/live382_wave.anim";
            Check(AssetDatabase.LoadAssetAtPath<AnimationClip>(wave) != null, "the rig's Wave as an AnimationClip");
            AssetDatabase.DeleteAsset(wave);  // (the test's own: never the folder)
            var crate = GameObject.Find("Crate");
            var group = crate != null ? crate.GetComponent<LODGroup>() : null;
            var col = crate != null ? crate.GetComponentInChildren<BoxCollider>() : null;
            Check(group != null && group.lodCount == 3 && col != null && Vector3.Distance(col.size, new Vector3(1, 2, 1)) < 1e-4f, "the Crate's levels and collider");
        }
    }
}
