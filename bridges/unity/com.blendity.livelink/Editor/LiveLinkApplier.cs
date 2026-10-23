// SPDX-License-Identifier: GPL-2.0-or-later
// Applies Live Link messages (docs/LIVELINK.md) to the open scene: the objects live under a root "Blendity Live Link"
// and carry a BlendityLink with their Blendity id; nothing else in the scene is touched. Live Link sends glTF's axes
// (Blendity's left-handed Y up mirrored in X): mirroring back gives Unity's - Blendity's own numbers - exactly.
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEngine.Rendering;

namespace Blendity.LiveLink
{
    public sealed class LiveLinkApplier
    {
        public const string RootName = "Blendity Live Link";
        public readonly Dictionary<long, GameObject> Objects = new Dictionary<long, GameObject>();
        public readonly Dictionary<string, int> Counts = new Dictionary<string, int>();
        public bool Playing { get; private set; }

        readonly Dictionary<long, long> parents = new Dictionary<long, long>();
        readonly Dictionary<long, HashSet<long>> children = new Dictionary<long, HashSet<long>>();
        readonly Dictionary<long, int[]> slots = new Dictionary<long, int[]>();
        readonly Dictionary<long, GlbMesh> meshes = new Dictionary<long, GlbMesh>();
        readonly Dictionary<long, string> topology = new Dictionary<long, string>();
        readonly Dictionary<int, Material> materials = new Dictionary<int, Material>();
        readonly Dictionary<int, string> toonFields = new Dictionary<int, string>();
        readonly Dictionary<string, Texture2D> textures = new Dictionary<string, Texture2D>();
        HashSet<long> seen;
        readonly Transform root;

        public LiveLinkApplier()
        {
            var go = GameObject.Find(RootName) ?? new GameObject(RootName);
            root = go.transform;
            foreach (var link in go.GetComponentsInChildren<BlendityLink>(true))
                Objects[link.Id] = link.gameObject;  // (from an earlier session: the next sync updates them)
        }

        public void Apply(LiveLinkMessage m)
        {
            string t = m.Type;
            int n;
            Counts.TryGetValue(t, out n);
            Counts[t] = n + 1;
            var j = m.Json;
            switch (t)
            {
                case "sync": seen = new HashSet<long>(); Playing = j.GetBool("playing"); break;
                case "sync_end": EndSync(); break;
                case "play": Playing = j.GetBool("playing"); break;
                case "object": OnObject(j); break;
                case "transform": OnTransform(j); break;
                case "pose": OnPose(j); break;
                case "delete": Remove(j.GetId("id")); break;
                case "mesh": OnMesh(j, m.Bin); break;
                case "deform": OnDeform(j, m.Bin); break;
                case "material": OnMaterial(j); break;
                case "light": OnLight(j); break;
                case "camera": OnCamera(j); break;
                default: break;  // (unknown messages are ignored: the protocol grows by adding)
            }
        }

        // ------------------------------------------------------------ objects

        static Vector3 Position(float[] p) { return new Vector3(-p[0], p[1], p[2]); }
        static Quaternion Rotation(float[] q) { return new Quaternion(q[0], -q[1], -q[2], q[3]); }
        static Vector3 Scale(float[] s) { return new Vector3(s[0], s[1], s[2]); }

        void EndSync()
        {
            var gone = new List<long>();
            foreach (var id in Objects.Keys)
                if (!seen.Contains(id))
                    gone.Add(id);
            foreach (var id in gone)
                Remove(id);
            seen = null;
        }

        GameObject Create(long id, Dictionary<string, object> j, string kind)
        {
            var go = new GameObject(j.GetString("name", "Object"));
            var link = go.AddComponent<BlendityLink>();
            link.Id = id;
            link.Kind = kind;
            link.BoneOf = j.GetId("bone_of");
            if (kind == "mesh")
            {
                go.AddComponent<MeshFilter>();
                go.AddComponent<MeshRenderer>();
            }
            else if (kind == "light")
                go.AddComponent<Light>();
            else if (kind == "camera")
                go.AddComponent<Camera>().enabled = false;  // (it doesn't take over the Game view)
            go.transform.SetParent(root, false);
            return go;
        }

        void OnObject(Dictionary<string, object> j)
        {
            long id = j.GetId("id");
            if (seen != null)
                seen.Add(id);
            string kind = j.GetString("kind", "empty");
            GameObject go;
            if (Objects.TryGetValue(id, out go) && go != null && go.GetComponent<BlendityLink>().Kind != kind)
            {
                Remove(id);
                go = null;
            }
            if (go == null)
            {
                go = Create(id, j, kind);
                Objects[id] = go;
            }
            go.name = j.GetString("name", go.name);
            go.SetActive(j.GetBool("active", true));
            SetParentId(id, j.GetId("parent"));
            SetTrs(go.transform, j.GetFloats("position", 0, 0, 0), j.GetFloats("rotation", 0, 0, 0, 1), j.GetFloats("scale", 1, 1, 1));
            Reparent(id);
            HashSet<long> kids;
            if (children.TryGetValue(id, out kids))
                foreach (var child in kids)
                    Reparent(child);  // (children that came before their parent)
            if (j.Get("materials") == null)
                return;
            var keys = j.GetList("materials");
            var s = new int[keys.Count];
            for (int i = 0; i < s.Length; i++)
                s[i] = (int)(double)keys[i];
            slots[id] = s;
            AssignMaterials(id);
        }

        void SetParentId(long id, long parent)
        {
            long old;
            HashSet<long> set;
            if (parents.TryGetValue(id, out old) && children.TryGetValue(old, out set))
                set.Remove(id);
            parents[id] = parent;
            if (!children.TryGetValue(parent, out set))
                children[parent] = set = new HashSet<long>();
            set.Add(id);
        }

        static void SetTrs(Transform t, float[] p, float[] q, float[] s)
        {
            t.localPosition = Position(p);
            t.localRotation = Rotation(q);
            t.localScale = Scale(s);
        }

        void Reparent(long id)
        {
            GameObject go, parent = null;
            long pid;
            if (!Objects.TryGetValue(id, out go) || go == null)
                return;
            parents.TryGetValue(id, out pid);
            if (pid != 0 && !Objects.TryGetValue(pid, out parent))
                return;  // (its parent comes later)
            var to = parent != null ? parent.transform : root;
            if (go.transform.parent != to)
                go.transform.SetParent(to, false);
        }

        void OnTransform(Dictionary<string, object> j)
        {
            GameObject go;
            if (Objects.TryGetValue(j.GetId("id"), out go) && go != null)
                SetTrs(go.transform, j.GetFloats("position", 0, 0, 0), j.GetFloats("rotation", 0, 0, 0, 1), j.GetFloats("scale", 1, 1, 1));
        }

        void OnPose(Dictionary<string, object> j)
        {
            foreach (List<object> b in j.GetList("bones"))
            {
                GameObject go;
                if (b.Count < 11 || !Objects.TryGetValue((long)(double)b[0], out go) || go == null)
                    continue;
                var f = new float[11];
                for (int i = 0; i < 11; i++)
                    f[i] = (float)(double)b[i];
                SetTrs(go.transform, new[] { f[1], f[2], f[3] }, new[] { f[4], f[5], f[6], f[7] }, new[] { f[8], f[9], f[10] });
            }
        }

        void Remove(long id)
        {
            GameObject go;
            if (Objects.TryGetValue(id, out go) && go != null)
                Destroy(go);
            Objects.Remove(id);
            SetParentId(id, 0);
            children[0].Remove(id);
            parents.Remove(id);
            slots.Remove(id);
            meshes.Remove(id);
            topology.Remove(id);
        }

        void Destroy(GameObject go)
        {
            var mf = go.GetComponent<MeshFilter>();
            if (mf != null && mf.sharedMesh != null)
                Object.DestroyImmediate(mf.sharedMesh);
            var kids = new List<Transform>();
            foreach (Transform child in go.transform)
                kids.Add(child);
            foreach (var child in kids)
                child.SetParent(root, true);  // (its children have their own deletes or parents)
            Object.DestroyImmediate(go);
        }

        // ------------------------------------------------------------ meshes

        void OnMesh(Dictionary<string, object> j, byte[] bin)
        {
            long id = j.GetId("id");
            GameObject go;
            if (!Objects.TryGetValue(id, out go) || go == null || go.GetComponent<MeshFilter>() == null)
                return;  // (a mesh for an object already gone)
            var gm = GlbReader.ReadMesh(bin);
            var mesh = new Mesh { name = go.name };
            if (gm.VertexCount > 65535)
                mesh.indexFormat = IndexFormat.UInt32;
            mesh.vertices = ToVectors(gm.Positions);
            mesh.normals = ToVectors(gm.Normals);
            var uv = new Vector2[gm.VertexCount];
            for (int i = 0; i < uv.Length; i++)
                uv[i] = new Vector2(gm.Uvs[i * 2], gm.Uvs[i * 2 + 1]);
            mesh.uv = uv;
            mesh.subMeshCount = gm.Submeshes.Count;
            for (int i = 0; i < gm.Submeshes.Count; i++)
                mesh.SetTriangles(gm.Submeshes[i], i);
            mesh.RecalculateBounds();
            var mf = go.GetComponent<MeshFilter>();
            if (mf.sharedMesh != null)
                Object.DestroyImmediate(mf.sharedMesh);
            mf.sharedMesh = mesh;
            meshes[id] = gm;
            topology[id] = j.GetNumber("topology").ToString("R") + "/" + j.GetNumber("verts");
            AssignMaterials(id);
        }

        static Vector3[] ToVectors(float[] f)
        {
            var v = new Vector3[f.Length / 3];
            for (int i = 0; i < v.Length; i++)
                v[i] = new Vector3(f[i * 3], f[i * 3 + 1], f[i * 3 + 2]);
            return v;
        }

        void OnDeform(Dictionary<string, object> j, byte[] bin)
        {
            long id = j.GetId("id");
            GameObject go;
            GlbMesh gm;
            string topo;
            if (!Objects.TryGetValue(id, out go) || go == null || !meshes.TryGetValue(id, out gm) || !topology.TryGetValue(id, out topo))
                return;
            if (topo != j.GetNumber("topology").ToString("R") + "/" + j.GetNumber("verts"))
                return;  // (a newer mesh is on its way; a deform follows it)
            var into = new float[gm.Positions.Length];
            if (!GlbReader.Deform(gm, bin, into))
                return;
            var mesh = go.GetComponent<MeshFilter>().sharedMesh;
            mesh.vertices = ToVectors(into);
            mesh.normals = ToVectors(GlbReader.SmoothNormals(gm, into));  // (not RecalculateNormals: UV seams stay smooth)
            mesh.RecalculateBounds();
        }

        // ------------------------------------------------------------ materials

        void AssignMaterials(long id)
        {
            GameObject go;
            int[] keys;
            if (!Objects.TryGetValue(id, out go) || go == null || !slots.TryGetValue(id, out keys))
                return;
            var mr = go.GetComponent<MeshRenderer>();
            if (mr == null)
                return;
            GlbMesh gm;
            meshes.TryGetValue(id, out gm);
            int count = gm != null ? gm.Submeshes.Count : keys.Length;
            var mats = new Material[count];
            var toon = new string[count];
            bool anyToon = false;
            for (int i = 0; i < count; i++)
            {
                int slot = gm != null ? gm.Slots[i] : i;
                int key = keys.Length > 0 ? keys[Mathf.Clamp(slot, 0, keys.Length - 1)] : 0;
                Material m;
                materials.TryGetValue(key, out m);
                mats[i] = m;
                string f;
                toon[i] = toonFields.TryGetValue(key, out f) ? f : "";
                anyToon |= toon[i].Length > 0;
            }
            mr.sharedMaterials = mats;
            var bt = go.GetComponent<BlendityToon>();
            if (anyToon && bt == null)
                bt = go.AddComponent<BlendityToon>();
            if (bt != null)
                bt.SlotFields = toon;
        }

        static Shader LitShader()
        {
            bool srp = GraphicsSettings.currentRenderPipeline != null;
            return Shader.Find(srp ? "Universal Render Pipeline/Lit" : "Standard") ?? Shader.Find("Standard");
        }

        void OnMaterial(Dictionary<string, object> j)
        {
            int key = (int)j.GetNumber("key");
            Material m;
            if (!materials.TryGetValue(key, out m) || m == null)
            {
                m = new Material(LitShader());
                materials[key] = m;
            }
            m.name = j.GetString("name", "Material");
            var gltf = j.GetDict("gltf") ?? new Dictionary<string, object>();
            var pbr = gltf.GetDict("pbrMetallicRoughness") ?? new Dictionary<string, object>();
            var c = pbr.GetFloats("baseColorFactor", 0.8f, 0.8f, 0.8f, 1f);
            var color = new Color(c[0], c[1], c[2], c[3]);
            float metallic = (float)pbr.GetNumber("metallicFactor", 0), smooth = 1f - (float)pbr.GetNumber("roughnessFactor", 0.5);
            m.SetColor("_BaseColor", color);
            m.SetColor("_Color", color);
            m.SetFloat("_Metallic", metallic);
            m.SetFloat("_Smoothness", smooth);
            m.SetFloat("_Glossiness", smooth);
            var e = gltf.GetFloats("emissiveFactor", 0, 0, 0);
            var ext = gltf.GetDict("extensions");
            float strength = ext != null && ext.GetDict("KHR_materials_emissive_strength") != null
                                 ? (float)ext.GetDict("KHR_materials_emissive_strength").GetNumber("emissiveStrength", 1) : 1f;
            var emission = new Color(e[0], e[1], e[2]) * strength;
            m.SetColor("_EmissionColor", emission);
            if (emission.maxColorComponent > 0)
                m.EnableKeyword("_EMISSION");
            else
                m.DisableKeyword("_EMISSION");
            SetSurface(m, gltf.GetString("alphaMode", "OPAQUE"), (float)gltf.GetNumber("alphaCutoff", 0.5), gltf.GetBool("doubleSided"));
            var maps = j.GetDict("maps");
            SetTexture(m, maps, "Base Map", "_BaseMap", "_MainTex", false);
            SetTexture(m, maps, "Normal Map", "_BumpMap", "_BumpMap", true);
            var toon = ext != null ? ext.GetDict("BLENDITY_toon") : null;
            toonFields[key] = toon != null ? Serialize(toon) : "";
            foreach (var kv in slots)
                if (System.Array.IndexOf(kv.Value, key) >= 0)
                    AssignMaterials(kv.Key);
        }

        /// <summary>Opaque, cutout (MASK) or transparent (BLEND), and culling, for URP Lit and Standard alike.</summary>
        static void SetSurface(Material m, string mode, float cutoff, bool doubleSided)
        {
            bool blend = mode == "BLEND", clip = mode == "MASK";
            m.SetFloat("_Cull", doubleSided ? 0f : 2f);
            m.SetFloat("_Cutoff", cutoff);
            m.SetFloat("_AlphaClip", clip ? 1f : 0f);
            m.SetFloat("_Surface", blend ? 1f : 0f);  // (URP)
            m.SetFloat("_Mode", blend ? 3f : clip ? 1f : 0f);  // (Standard: Opaque, Cutout, Fade, Transparent)
            m.SetFloat("_SrcBlend", blend ? (float)BlendMode.SrcAlpha : (float)BlendMode.One);
            m.SetFloat("_DstBlend", blend ? (float)BlendMode.OneMinusSrcAlpha : (float)BlendMode.Zero);
            m.SetFloat("_ZWrite", blend ? 0f : 1f);
            SetKeyword(m, "_ALPHATEST_ON", clip);
            SetKeyword(m, "_SURFACE_TYPE_TRANSPARENT", blend);
            SetKeyword(m, "_ALPHABLEND_ON", blend);
            m.renderQueue = blend ? (int)RenderQueue.Transparent : clip ? (int)RenderQueue.AlphaTest : -1;
        }

        static void SetKeyword(Material m, string k, bool on)
        {
            if (on)
                m.EnableKeyword(k);
            else
                m.DisableKeyword(k);
        }

        void SetTexture(Material m, Dictionary<string, object> maps, string slot, string urp, string builtin, bool normal)
        {
            var entry = maps != null ? maps.GetDict(slot) : null;
            if (entry == null)
                return;
            var tex = LoadTexture(entry.GetString("path"), normal || entry.GetBool("non_color"));
            if (tex == null)
                return;
            m.SetTexture(urp, tex);
            m.SetTexture(builtin, tex);
            if (normal)
                m.EnableKeyword("_NORMALMAP");
        }

        Texture2D LoadTexture(string path, bool linear)
        {
            Texture2D tex;
            if (textures.TryGetValue(path, out tex) && tex != null)
                return tex;
            if (!File.Exists(path))
                return null;  // (a path this computer doesn't have)
            tex = new Texture2D(2, 2, TextureFormat.RGBA32, true, linear) { name = Path.GetFileName(path) };
            if (!tex.LoadImage(File.ReadAllBytes(path)))
                return null;
            textures[path] = tex;
            return tex;
        }

        static string Serialize(Dictionary<string, object> d)
        {
            var parts = new List<string>();
            foreach (var kv in d)
                parts.Add("\"" + kv.Key.Replace("\"", "\\\"") + "\":" + Value(kv.Value));
            return "{" + string.Join(",", parts.ToArray()) + "}";
        }

        static string Value(object v)
        {
            if (v == null) return "null";
            if (v is bool) return (bool)v ? "true" : "false";
            if (v is double) return ((double)v).ToString("R", System.Globalization.CultureInfo.InvariantCulture);
            var s = v as string;
            if (s != null) return "\"" + s.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";
            var list = v as List<object>;
            if (list != null) return "[" + string.Join(",", list.ConvertAll(Value).ToArray()) + "]";
            var dict = v as Dictionary<string, object>;
            return dict != null ? Serialize(dict) : "null";
        }

        // ------------------------------------------------------------ lights and cameras

#if UNITY_2023_1_OR_NEWER
        const LightType RectangleLight = LightType.Rectangle;
#else
        const LightType RectangleLight = LightType.Area;  // (named Rectangle from Unity 2023.1)
#endif

        void OnLight(Dictionary<string, object> j)
        {
            GameObject go;
            if (!Objects.TryGetValue(j.GetId("id"), out go) || go == null)
                return;
            var l = go.GetComponent<Light>();
            if (l == null)
                return;
            string type = j.GetString("type", "point");
            l.type = type == "directional" ? LightType.Directional : type == "spot" ? LightType.Spot
                   : type == "area" ? (j.GetBool("disc") ? LightType.Disc : RectangleLight) : LightType.Point;
            var c = j.GetFloats("color", 1, 1, 1);
            l.color = new Color(c[0], c[1], c[2]);
            l.intensity = (float)j.GetNumber("intensity", 1);  // (Blendity's intensity is Unity's)
            l.range = (float)j.GetNumber("range", 10);
            l.spotAngle = (float)j.GetNumber("spot_angle", 30);
            l.innerSpotAngle = (float)j.GetNumber("inner_spot_angle", 21.8);
            var a = j.GetFloats("area", 1, 1);
#if UNITY_EDITOR
            l.areaSize = new Vector2(a[0], a[1]);
#endif
        }

        void OnCamera(Dictionary<string, object> j)
        {
            GameObject go;
            if (!Objects.TryGetValue(j.GetId("id"), out go) || go == null)
                return;
            var cam = go.GetComponent<Camera>();
            if (cam == null)
                return;
            cam.nearClipPlane = (float)j.GetNumber("near", 0.3);
            cam.farClipPlane = (float)j.GetNumber("far", 1000);
            cam.orthographic = j.GetBool("orthographic");
            cam.orthographicSize = (float)j.GetNumber("ortho_size", 5);
            cam.usePhysicalProperties = j.GetBool("physical");
            if (cam.usePhysicalProperties)
            {
                var s = j.GetFloats("sensor", 36, 24);
                cam.sensorSize = new Vector2(s[0], s[1]);
                cam.focalLength = (float)j.GetNumber("focal_length", 50);
            }
            else
                cam.fieldOfView = (float)j.GetNumber("fov", 60);
        }
    }
}
