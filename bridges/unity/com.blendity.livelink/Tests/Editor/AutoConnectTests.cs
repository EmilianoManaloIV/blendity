// SPDX-License-Identifier: GPL-2.0-or-later
// EditMode test (task 0295): the package connects by itself. tools/livelink_fake_server.py runs first (it writes the
// discovery file under BLENDITY_PREFS_DIR, which the Unity process is given too) and serves the recorded session; the
// editor's own update loop - the [InitializeOnLoad] poll, as in an open Unity - finds the file, connects with no port
// typed, says hello and applies the session. Without a fake server (no discovery file) the test is skipped.
using System.Collections;
using System.IO;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;

namespace Blendity.LiveLink.Tests
{
    public class AutoConnectTests
    {
        [TearDown]
        public void Leave()
        {
            LiveLinkClient.Disconnect();
            var root = GameObject.Find(LiveLinkApplier.RootName);
            if (root != null)
                Object.DestroyImmediate(root);
        }

        [UnityTest]
        public IEnumerator ConnectsByItselfAndAppliesTheSession()
        {
            if (!File.Exists(LiveLinkDiscovery.FilePath()))
            {
                Assert.Ignore("No discovery file at " + LiveLinkDiscovery.FilePath() + " (run tools/livelink_fake_server.py first)");
                yield break;
            }
            LiveLinkClient.AutoConnect = true;
            LiveLinkClient.TryAutoConnect();  // (the poll's first call; the editor's updates finish the connection and read the stream)
            float waited = 0f;
            while (waited < 20f && (LiveLinkClient.Messages < 20 || GameObject.Find("Box") == null))
            {
                yield return null;
                waited += Time.unscaledDeltaTime > 0f ? Time.unscaledDeltaTime : 0.05f;
                System.Threading.Thread.Sleep(20);
            }
            Debug.Log("Blendity Live Link: " + LiveLinkClient.Status + ", " + LiveLinkClient.Messages + " messages after " + waited.ToString("0.0") + " s");
            Assert.Greater(LiveLinkClient.Messages, 20, LiveLinkClient.Status);
            var box = GameObject.Find("Box");
            Assert.NotNull(box, "the session's Box");
            Assert.That(Vector3.Distance(box.transform.localPosition, new Vector3(1, 2, 3)), Is.LessThan(1e-5f));
        }
    }
}
