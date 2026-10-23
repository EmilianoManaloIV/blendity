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
            for (var m = s.Next(); m != null; m = s.Next(), n++)
                a.Apply(m);
            Assert.Greater(n, 20);
            Assert.AreEqual(0, s.PendingBytes);
            var box = GameObject.Find("Box");
            Assert.NotNull(box);
            Assert.That(Vector3.Distance(box.transform.localPosition, new Vector3(1, 2, 3)), Is.LessThan(1e-5f));
            var mesh = box.GetComponent<MeshFilter>().sharedMesh;
            Assert.AreEqual(36, mesh.triangles.Length);  // (12 triangles)
            var mat = box.GetComponent<MeshRenderer>().sharedMaterial;
            Assert.AreEqual("Cel", mat.name);
            Assert.NotNull(box.GetComponent<BlendityToon>());
            Assert.IsNull(GameObject.Find("Sphere"));
            var lower = GameObject.Find("Lower");
            Assert.That(Quaternion.Angle(lower.transform.localRotation, Quaternion.identity), Is.EqualTo(30f).Within(1e-3f));
            Object.DestroyImmediate(GameObject.Find(LiveLinkApplier.RootName));
        }
    }
}
