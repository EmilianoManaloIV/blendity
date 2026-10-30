// SPDX-License-Identifier: GPL-2.0-or-later
// Where Blendity's Live Link listens (task 0274, ADR 0086): Blendity writes ~/.blendity/livelink.json while it listens
// (BLENDITY_PREFS_DIR moves it) and removes it when it stops. No UnityEngine here: ReplayCheck compiles it with csc.
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Blendity.LiveLink
{
    public sealed class LiveLinkEndpoint
    {
        public int Port;
        public string Scene = "";
        public string App = "";
    }

    public static class LiveLinkDiscovery
    {
        public const string Protocol = "blendity-livelink/1";
        public const string BridgeVersion = "1.1";

        public static string FilePath()
        {
            string dir = Environment.GetEnvironmentVariable("BLENDITY_PREFS_DIR");
            if (string.IsNullOrEmpty(dir))
                dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".blendity");
            return Path.Combine(dir, "livelink.json");
        }

        /// <summary>The file's port and scene; null when there is none, it can't be read, or it is another protocol.</summary>
        public static LiveLinkEndpoint Read(string path = null)
        {
            string text;
            try
            {
                text = File.ReadAllText(path ?? FilePath(), Encoding.UTF8);
            }
            catch (Exception)
            {
                return null;
            }
            var d = MiniJson.Parse(text) as Dictionary<string, object>;
            if (d == null || d.GetString("protocol") != Protocol)
                return null;
            int port = (int)d.GetNumber("port");
            if (port < 1 || port > 65535)
                return null;
            return new LiveLinkEndpoint { Port = port, Scene = d.GetString("scene"), App = d.GetString("app") };
        }

        /// <summary>The line a bridge sends first: who it is (Blendity's Live Link window lists it; nothing reads more).</summary>
        public static byte[] Hello(string app)
        {
            string esc = app.Replace("\\", "\\\\").Replace("\"", "\\\"");
            return Encoding.UTF8.GetBytes("{\"t\":\"client\",\"app\":\"" + esc + "\",\"bridge\":\"" + BridgeVersion + "\"}\n");
        }
    }
}
