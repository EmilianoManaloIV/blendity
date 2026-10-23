// SPDX-License-Identifier: GPL-2.0-or-later
// The Unity bridge's parts that need no Unity (the framing, the JSON reader, the glTF reader), run on the recorded
// Live Link session without Unity: any C# compiler, even the .NET Framework's own (C# 5):
//
//   csc /out:ReplayCheck.exe ReplayCheck.cs ..\com.blendity.livelink\Editor\LiveLinkStream.cs
//       ..\com.blendity.livelink\Editor\MiniJson.cs ..\com.blendity.livelink\Editor\GlbReader.cs
//   ReplayCheck.exe ..\..\..\tests\fixtures\livelink_stream.bin
using System;
using System.Collections.Generic;
using System.IO;
using Blendity.LiveLink;

static class ReplayCheck
{
    static int Main(string[] args)
    {
        var data = File.ReadAllBytes(args.Length > 0 ? args[0] : Path.Combine("..", "..", "..", "tests", "fixtures", "livelink_stream.bin"));
        var s = new LiveLinkStream();
        s.Feed(data, data.Length);
        var counts = new SortedDictionary<string, int>();
        var meshes = new Dictionary<long, GlbMesh>();
        int n = 0, failures = 0;
        for (var m = s.Next(); m != null; m = s.Next(), n++)
        {
            int c;
            counts.TryGetValue(m.Type, out c);
            counts[m.Type] = c + 1;
            if (m.Type == "mesh")
            {
                var gm = GlbReader.ReadMesh(m.Bin);
                int tris = 0;
                foreach (var sm in gm.Submeshes)
                    tris += sm.Length / 3;
                int verts = (int)m.Json.GetNumber("verts");
                foreach (int v in gm.VertexOf)
                    if (v < 0 || v >= verts)
                        failures++;
                meshes[m.Json.GetId("id")] = gm;
                /* The normals worked out again from the positions agree with the file's (the sign of the winding). */
                var again = GlbReader.SmoothNormals(gm, gm.Positions);
                int agree = 0;
                for (int i = 0; i < gm.VertexCount; i++)
                    if (again[i * 3] * gm.Normals[i * 3] + again[i * 3 + 1] * gm.Normals[i * 3 + 1] + again[i * 3 + 2] * gm.Normals[i * 3 + 2] > 0.5f)
                        agree++;
                if (agree < gm.VertexCount * 9 / 10)
                    failures++;
                Console.WriteLine("  normals worked out again agree with the file's at {0} of {1} vertices", agree, gm.VertexCount);
                Console.WriteLine("mesh {0}: {1} vertices ({2} in the mesh), {3} triangles, {4} submeshes", m.Json.GetId("id"), gm.VertexCount, verts, tris, gm.Submeshes.Count);
            }
            if (m.Type == "deform")
            {
                GlbMesh gm;
                if (!meshes.TryGetValue(m.Json.GetId("id"), out gm) || !GlbReader.Deform(gm, m.Bin, new float[gm.Positions.Length]))
                    failures++;
            }
        }
        foreach (var kv in counts)
            Console.Write(kv.Key + " " + kv.Value + "  ");
        Console.WriteLine();
        if (s.PendingBytes != 0)
            failures++;
        Console.WriteLine(failures == 0 ? "REPLAY OK: " + n + " messages" : "REPLAY FAILED: " + failures + " problem(s)");
        return failures == 0 && n > 20 ? 0 : 1;
    }
}
