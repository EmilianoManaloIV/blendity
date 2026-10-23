// SPDX-License-Identifier: GPL-2.0-or-later
// The glTF 2.0 binaries Live Link sends (docs/LIVELINK.md, "mesh"), read into Unity's layout without UnityEngine:
// glTF's axes mirrored back in X (which is Unity's own left-handed Y up: Blendity's numbers come back exactly), the
// winding reversed back, V flipped back, one submesh per material slot, and for each vertex its mesh vertex (_VERT),
// which a "deform" message moves.
using System;
using System.Collections.Generic;
using System.Text;

namespace Blendity.LiveLink
{
    public sealed class GlbMesh
    {
        public float[] Positions;   // x, y, z per vertex (Unity's axes)
        public float[] Normals;     // x, y, z per vertex
        public float[] Uvs;         // u, v per vertex
        public int[] VertexOf;      // each vertex's mesh vertex (what a deform's positions are indexed by)
        public int[] NormalGroup;   // vertices that share a mesh vertex and had the same normal: one smooth normal
        public readonly List<int[]> Submeshes = new List<int[]>();  // triangles, clockwise (Unity's front)
        public readonly List<int> Slots = new List<int>();          // each submesh's material slot
        public int VertexCount { get { return Positions.Length / 3; } }
    }

    public static class GlbReader
    {
        static readonly Dictionary<string, int> Count = new Dictionary<string, int> { { "SCALAR", 1 }, { "VEC2", 2 }, { "VEC3", 3 }, { "VEC4", 4 } };

        public static void Parse(byte[] glb, out Dictionary<string, object> doc, out byte[] bin)
        {
            if (glb.Length < 20 || BitConverter.ToUInt32(glb, 0) != 0x46546C67u || BitConverter.ToUInt32(glb, 4) != 2u)
                throw new FormatException("not a glTF 2.0 binary");
            doc = null;
            bin = new byte[0];
            int off = 12, length = (int)Math.Min(BitConverter.ToUInt32(glb, 8), (uint)glb.Length);
            while (off + 8 <= length)
            {
                int size = (int)BitConverter.ToUInt32(glb, off);
                uint kind = BitConverter.ToUInt32(glb, off + 4);
                off += 8;
                if (kind == 0x4E4F534Au)
                    doc = MiniJson.Parse(Encoding.UTF8.GetString(glb, off, size)) as Dictionary<string, object>;
                else if (kind == 0x004E4942u)
                {
                    bin = new byte[size];
                    Buffer.BlockCopy(glb, off, bin, 0, size);
                }
                off += size;
            }
            if (doc == null)
                throw new FormatException("a glTF binary without its JSON chunk");
        }

        /// <summary>Accessor `index` as floats (float or unsigned-int components), `n` per element.</summary>
        public static float[] Floats(Dictionary<string, object> doc, byte[] bin, int index, out int n)
        {
            var a = (Dictionary<string, object>)doc.GetList("accessors")[index];
            var view = (Dictionary<string, object>)doc.GetList("bufferViews")[(int)a.GetNumber("bufferView")];
            n = Count[a.GetString("type")];
            int count = (int)a.GetNumber("count"), type = (int)a.GetNumber("componentType");
            int start = (int)view.GetNumber("byteOffset") + (int)a.GetNumber("byteOffset");
            var r = new float[count * n];
            for (int i = 0; i < r.Length; i++)
                r[i] = type == 5126 ? BitConverter.ToSingle(bin, start + i * 4) : BitConverter.ToUInt32(bin, start + i * 4);
            return r;
        }

        public static GlbMesh ReadMesh(byte[] glb)
        {
            Dictionary<string, object> doc;
            byte[] bin;
            Parse(glb, out doc, out bin);
            var mesh = new GlbMesh();
            var pos = new List<float>();
            var nrm = new List<float>();
            var uv = new List<float>();
            var vof = new List<int>();
            var prims = ((Dictionary<string, object>)doc.GetList("meshes")[0]).GetList("primitives");
            foreach (Dictionary<string, object> prim in prims)
                ReadPrimitive(doc, bin, prim, mesh, pos, nrm, uv, vof);
            mesh.Positions = pos.ToArray();
            mesh.Normals = nrm.ToArray();
            mesh.Uvs = uv.ToArray();
            mesh.VertexOf = vof.ToArray();
            mesh.NormalGroup = NormalGroups(mesh);
            return mesh;
        }

        static void ReadPrimitive(Dictionary<string, object> doc, byte[] bin, Dictionary<string, object> prim, GlbMesh mesh,
                                  List<float> pos, List<float> nrm, List<float> uv, List<int> vof)
        {
            int n, first = pos.Count / 3;
            var at = prim.GetDict("attributes");
            float[] p = Floats(doc, bin, (int)at.GetNumber("POSITION"), out n);
            int verts = p.Length / 3;
            float[] nm = at.Get("NORMAL") != null ? Floats(doc, bin, (int)at.GetNumber("NORMAL"), out n) : new float[verts * 3];
            float[] t = at.Get("TEXCOORD_0") != null ? Floats(doc, bin, (int)at.GetNumber("TEXCOORD_0"), out n) : new float[verts * 2];
            float[] v = at.Get("_VERT") != null ? Floats(doc, bin, (int)at.GetNumber("_VERT"), out n) : null;
            for (int i = 0; i < verts; i++)
            {
                pos.Add(-p[i * 3]); pos.Add(p[i * 3 + 1]); pos.Add(p[i * 3 + 2]);
                nrm.Add(-nm[i * 3]); nrm.Add(nm[i * 3 + 1]); nrm.Add(nm[i * 3 + 2]);
                uv.Add(t[i * 2]); uv.Add(1.0f - t[i * 2 + 1]);
                vof.Add(v != null ? (int)v[i] : first + i);
            }
            float[] idx = Floats(doc, bin, (int)prim.GetNumber("indices"), out n);
            var tris = new int[idx.Length];
            for (int i = 0; i + 2 < idx.Length; i += 3)
            {  // (the mirror back reverses the winding back)
                tris[i] = first + (int)idx[i];
                tris[i + 1] = first + (int)idx[i + 2];
                tris[i + 2] = first + (int)idx[i + 1];
            }
            mesh.Submeshes.Add(tris);
            var extras = prim.GetDict("extras");
            mesh.Slots.Add(extras != null ? (int)extras.GetNumber("slot") : mesh.Slots.Count);
        }

        /// <summary>Each vertex's group: the first vertex with its mesh vertex and its normal (a split by UV alone stays
        /// smooth across; a split by normal, a hard edge, stays hard).</summary>
        static int[] NormalGroups(GlbMesh mesh)
        {
            var groups = new int[mesh.VertexOf.Length];
            var first = new Dictionary<string, int>();
            for (int i = 0; i < groups.Length; i++)
            {
                string key = mesh.VertexOf[i] + ":" + mesh.Normals[i * 3] + "," + mesh.Normals[i * 3 + 1] + "," + mesh.Normals[i * 3 + 2];
                int g;
                if (!first.TryGetValue(key, out g))
                {
                    g = i;
                    first[key] = i;
                }
                groups[i] = g;
            }
            return groups;
        }

        /// <summary>Normals for moved vertices: the area-weighted face normals summed over each normal group.</summary>
        public static float[] SmoothNormals(GlbMesh mesh, float[] positions)
        {
            var sum = new float[positions.Length];
            foreach (var tris in mesh.Submeshes)
                for (int t = 0; t + 2 < tris.Length; t += 3)
                    AddFaceNormal(mesh, positions, sum, tris[t], tris[t + 1], tris[t + 2]);
            var n = new float[positions.Length];
            for (int i = 0; i < mesh.NormalGroup.Length; i++)
            {
                int g = mesh.NormalGroup[i];
                float x = sum[g * 3], y = sum[g * 3 + 1], z = sum[g * 3 + 2];
                float len = (float)Math.Sqrt(x * x + y * y + z * z);
                if (len < 1e-20f)
                    continue;
                n[i * 3] = x / len;
                n[i * 3 + 1] = y / len;
                n[i * 3 + 2] = z / len;
            }
            return n;
        }

        static void AddFaceNormal(GlbMesh mesh, float[] p, float[] sum, int a, int b, int c)
        {
            float ux = p[b * 3] - p[a * 3], uy = p[b * 3 + 1] - p[a * 3 + 1], uz = p[b * 3 + 2] - p[a * 3 + 2];
            float vx = p[c * 3] - p[a * 3], vy = p[c * 3 + 1] - p[a * 3 + 1], vz = p[c * 3 + 2] - p[a * 3 + 2];
            // (Unity's convention, as its RecalculateNormals: (b - a) x (c - a) for its clockwise front faces)
            float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            foreach (int v in new[] { a, b, c })
            {
                int g = mesh.NormalGroup[v];
                sum[g * 3] += nx;
                sum[g * 3 + 1] += ny;
                sum[g * 3 + 2] += nz;
            }
        }

        /// <summary>A deform's positions (glTF axes, per mesh vertex) onto the mesh's vertices, in Unity's axes.</summary>
        public static bool Deform(GlbMesh mesh, byte[] positions, float[] into)
        {
            int count = positions.Length / 12;
            for (int i = 0; i < mesh.VertexOf.Length; i++)
            {
                int v = mesh.VertexOf[i];
                if (v < 0 || v >= count)
                    return false;
                into[i * 3] = -BitConverter.ToSingle(positions, v * 12);
                into[i * 3 + 1] = BitConverter.ToSingle(positions, v * 12 + 4);
                into[i * 3 + 2] = BitConverter.ToSingle(positions, v * 12 + 8);
            }
            return true;
        }
    }
}
