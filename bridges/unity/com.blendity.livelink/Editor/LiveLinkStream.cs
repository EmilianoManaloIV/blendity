// SPDX-License-Identifier: GPL-2.0-or-later
// blendity-livelink/1 framing (docs/LIVELINK.md): one JSON object a line, then the "bin" bytes it announces. No
// UnityEngine here (and C# 5 syntax), so the same code reads a socket in the Editor and a recorded stream in a test.
using System;
using System.Collections.Generic;
using System.Text;

namespace Blendity.LiveLink
{
    public sealed class LiveLinkMessage
    {
        public Dictionary<string, object> Json;
        public byte[] Bin;

        public string Type { get { return Json.GetString("t"); } }
    }

    /// <summary>Bytes in (Feed), whole messages out (Next).</summary>
    public sealed class LiveLinkStream
    {
        byte[] buf = new byte[1 << 16];
        int start, end;

        public int PendingBytes { get { return end - start; } }

        public void Feed(byte[] data, int count)
        {
            if (end + count > buf.Length)
            {
                if (end - start + count <= buf.Length)
                    Compact();
                else
                {
                    var bigger = new byte[Math.Max(buf.Length * 2, end - start + count)];
                    Buffer.BlockCopy(buf, start, bigger, 0, end - start);
                    end -= start;
                    start = 0;
                    buf = bigger;
                }
            }
            Buffer.BlockCopy(data, 0, buf, end, count);
            end += count;
        }

        void Compact()
        {
            Buffer.BlockCopy(buf, start, buf, 0, end - start);
            end -= start;
            start = 0;
        }

        /// <summary>The next complete message, or null until more bytes arrive.</summary>
        public LiveLinkMessage Next()
        {
            int nl = Array.IndexOf(buf, (byte)'\n', start, end - start);
            if (nl < 0)
                return null;
            var json = MiniJson.Parse(Encoding.UTF8.GetString(buf, start, nl - start)) as Dictionary<string, object>;
            if (json == null)
                throw new FormatException("a Live Link line that isn't a JSON object");
            int need = (int)json.GetNumber("bin", 0);
            if (end - (nl + 1) < need)
                return null;
            var bin = new byte[need];
            Buffer.BlockCopy(buf, nl + 1, bin, 0, need);
            start = nl + 1 + need;
            return new LiveLinkMessage { Json = json, Bin = bin };
        }
    }
}
