// SPDX-License-Identifier: GPL-2.0-or-later
// EditMode test (Window > General > Test Runner): replays the recorded Live Link session in the Blendity repository
// (tests/fixtures/livelink_stream.bin; the package added from the repository's bridges/unity folder) through the
// applier and checks the scene it leaves. The session: Blendity's sample scene, a toon cube "Box", a rig of two bones;
// then the Box moved to (1, 2, 3), its material recoloured, one vertex moved, a bone turned 30 degrees, the Sphere
// deleted, Play and Stop.
using System.IO;
using NUnit.Framework;
using UnityEngine;

namespace Blendity.LiveLink.Tests
{
    public class ReplayTests
    {
        static string FixturePath()
        {
            var pkg = UnityEditor.PackageManager.PackageInfo.FindForAssembly(typeof(LiveLinkApplier).Assembly);
            if (pkg == null)
                return "";
            return Path.GetFullPath(Path.Combine(pkg.resolvedPath, "..", "..", "..", "tests", "fixtures", "livelink_stream.bin"));
        }

        [TearDown]
        public void RemoveTheLinkedObjects()
        {
            var root = GameObject.Find(LiveLinkApplier.RootName);
            if (root != null)
                Object.DestroyImmediate(root);
        }

        [Test]
        public void RecordedSessionLeavesTheScene()
        {
            string path = FixturePath();
            if (!File.Exists(path))
                Assert.Ignore("No recorded session at " + path + " (add the package from the Blendity repository)");
            var data = File.ReadAllBytes(path);
            var s = new LiveLinkStream();
            s.Feed(data, data.Length);
            var a = new LiveLinkApplier();
            int n = 0;
            long armId = 0;
            byte[] armMesh = null, armPosed = null;  // (round 378: the Arm at rest, and posed as Blendity deformed it for the recorder)
            for (var m = s.Next(); m != null; m = s.Next(), n++)
            {
                if (m.Type == "object" && m.Json.GetString("name") == "Arm")
                    armId = m.Json.GetId("id");
                if (m.Type == "mesh" && armId != 0 && m.Json.GetId("id") == armId)
                    armMesh = m.Bin;
                if (m.Type == "deform" && armId != 0 && m.Json.GetId("id") == armId)
                    armPosed = m.Bin;
                a.Apply(m);
            }
            Assert.Greater(n, 20);
            Assert.AreEqual(0, s.PendingBytes);
            var box = GameObject.Find("Box");
            Assert.NotNull(box);
            Assert.That(Vector3.Distance(box.transform.localPosition, new Vector3(1, 2, 3)), Is.LessThan(1e-5f));
            var mesh = box.GetComponent<MeshFilter>().sharedMesh;
            Assert.AreEqual(36, mesh.triangles.Length);  // (12 triangles)
            var mat = box.GetComponent<MeshRenderer>().sharedMaterial;
            Assert.AreEqual("Cel", mat.name);
            // Round 377: the Base Map from its PNG bytes (a 4x4 checker), the colour made gamma.
            var baseMap = mat.GetTexture("_MainTex") as Texture2D;
            Assert.NotNull(baseMap);
            Assert.AreEqual(4, baseMap.width);
            Assert.That(mat.GetColor("_Color").r, Is.EqualTo(Mathf.LinearToGammaSpace(0.9f)).Within(1e-3f));
            Assert.NotNull(box.GetComponent<BlendityToon>());
            Assert.IsNull(GameObject.Find("Sphere"));
            // Round 378: the Arm skinned on the rig's bones, its Smile a blend shape at 25.
            var arm = GameObject.Find("Arm").GetComponent<SkinnedMeshRenderer>();
            Assert.NotNull(arm);
            Assert.AreEqual(3, arm.bones.Length);
            Assert.AreEqual("Upper", arm.bones[1].name);
            Assert.AreEqual(1, arm.sharedMesh.blendShapeCount);
            Assert.That(arm.GetBlendShapeWeight(0), Is.EqualTo(25f).Within(1e-3f));
            Assert.AreEqual(arm.sharedMesh.vertexCount, arm.sharedMesh.boneWeights.Length);
            // ...and Unity's skinning puts every vertex where Blendity's did (the turned bone, the smile at 25).
            var rest = GlbReader.ReadMesh(armMesh);
            var baked = new Mesh();
            arm.BakeMesh(baked, true);
            var bv = baked.vertices;
            float worst = 0f;
            for (int i = 0; i < bv.Length; i++)
            {
                int v = rest.VertexOf[i] * 12;
                var want = new Vector3(-System.BitConverter.ToSingle(armPosed, v), System.BitConverter.ToSingle(armPosed, v + 4), System.BitConverter.ToSingle(armPosed, v + 8));
                worst = Mathf.Max(worst, Vector3.Distance(bv[i], want));
            }
            Debug.Log("Blendity Live Link: the skinned Arm against Blendity's posed vertices, at most " + worst + " m apart");
            Assert.That(worst, Is.LessThan(1e-3f));
            var lower = GameObject.Find("Lower");
            Assert.That(Quaternion.Angle(lower.transform.localRotation, Quaternion.identity), Is.EqualTo(30f).Within(1e-3f));
            // Round 376: the look - a gradient with fog 5-40 m, half a stop up, Bloom 0.8 and a Vignette.
            Assert.IsTrue(RenderSettings.fog);
            Assert.AreEqual(UnityEngine.Rendering.AmbientMode.Trilight, RenderSettings.ambientMode);
            Assert.That(RenderSettings.fogStartDistance, Is.EqualTo(5f).Within(1e-4f));
            Assert.That(RenderSettings.fogEndDistance, Is.EqualTo(40f).Within(1e-4f));
            Assert.That(RenderSettings.ambientSkyColor.b, Is.EqualTo(Mathf.LinearToGammaSpace(0.8f)).Within(1e-4f));  // (linear sent, gamma kept)
            var look = GameObject.Find(LiveLinkApplier.LookName).GetComponent<BlendityLook>();
            Assert.That(look.Exposure, Is.EqualTo(0.5f).Within(1e-5f));
            StringAssert.Contains("Bloom", look.Filters);
            StringAssert.Contains("Vignette", look.Filters);
            Assert.IsTrue(look.HasSceneLighting);
            look.RestoreSceneLighting();  // (the scene's own lighting back: no fog before the session)
            Assert.IsFalse(RenderSettings.fog);
#if BLENDITY_URP
            var profile = look.GetComponent<UnityEngine.Rendering.Volume>().sharedProfile;
            UnityEngine.Rendering.Universal.Bloom bloom;
            Assert.IsTrue(profile.TryGet(out bloom) && bloom.active);
            Assert.That(bloom.intensity.value, Is.EqualTo(0.8f).Within(1e-5f));
            UnityEngine.Rendering.Universal.ColorAdjustments color;
            Assert.IsTrue(profile.TryGet(out color));
            Assert.That(color.postExposure.value, Is.EqualTo(0.5f).Within(1e-5f));
            Debug.Log("Blendity Live Link: the URP Volume checked (Bloom, exposure)");
#endif
            Object.DestroyImmediate(GameObject.Find(LiveLinkApplier.RootName));
        }
    }
}
