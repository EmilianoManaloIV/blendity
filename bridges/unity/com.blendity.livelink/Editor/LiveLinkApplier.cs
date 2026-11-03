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
    public sealed partial class LiveLinkApplier
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
        readonly Dictionary<int, Texture2D> texByKey = new Dictionary<int, Texture2D>();  // (round 377: from PNG bytes)
        readonly Dictionary<int, Dictionary<string, object>> materialMsgs = new Dictionary<int, Dictionary<string, object>>();
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
                case "sync": seen = new HashSet<long>(); Playing = j.GetBool("playing"); EnsureSceneDescriptor(); break;
                case "sync_end": EndSync(); break;
                case "play": Playing = j.GetBool("playing"); break;
                case "object": OnObject(j); break;
                case "transform": OnTransform(j); break;
                case "pose": OnPose(j); break;
                case "delete": Remove(j.GetId("id")); break;
                case "mesh": OnMesh(j, m.Bin); break;
                case "deform": OnDeform(j, m.Bin); break;
                case "material": OnMaterial(j); break;
                case "texture": OnTexture(j, m.Bin); break;  // (round 377)
                case "weights": OnWeights(j); break;  // (round 378)
                case "clip": OnClip(j, m.Bin); break;  // (round 379: LiveLinkClips.cs)
                case "lod": OnLod(j); break;  // (round 380: LiveLinkWorld.cs)
                case "lodmesh": OnLodMesh(j, m.Bin); break;
                case "collider": OnCollider(j); break;
                case "lightvolume": OnLightVolume(j); break;  // (round 397)
                case "reflectionprobe": OnReflectionProbe(j); break;  // (round 401: LiveLinkWorld.cs)
                case "lightvolumebake": OnLightVolumeBake(j, m.Bin); break;  // (round 398)
                case "avatar": OnAvatar(j); break;  // (round 381: LiveLinkVRChat.cs)
                case "vrccomponents": OnVrcComponents(j); break;  // (round 402: LiveLinkVRChat.cs)
                case "springs": OnSprings(j); break;
                case "light": OnLight(j); break;
                case "camera": OnCamera(j); break;
                case "world": OnWorld(j); break;  // (round 376: LiveLinkLook.cs)
                case "render": OnRender(j); break;
                case "filters": OnFilters(j); break;
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
            SetStatic(id, go, j.GetBool("static"));  // (round 381: Contribute GI)
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
            SetTrsOnly(t, p, q, s);
            if (FindType(LightVolumeType) != null)  // (round 397: a volume's or light's package data follows; only with the package there)
                RefreshMoved(t.gameObject);
        }

        static void SetTrsOnly(Transform t, float[] p, float[] q, float[] s)
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
                SetTrsOnly(go.transform, new[] { f[1], f[2], f[3] }, new[] { f[4], f[5], f[6], f[7] }, new[] { f[8], f[9], f[10] });  // (a pose: bones hold no volume)
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
            shapeWeights.Remove(id);
            lodScreens.Remove(id);
            lodCull.Remove(id);
            colliderMsgs.Remove(id);
            colliderStale.Remove(id);
            avatarMsgs.Remove(id);
            avatarWritten.Remove(id);
            madeStatic.Remove(id);
        }

        /* A deleted object's child: one the bridge made for it (an LOD level, its collider) goes with it; any other - one
         * of Blendity's, or the user's own - waits at the root (review 380: the user's children had been deleted). */
        void MoveOrDrop(Transform child)
        {
            if (child.GetComponent<BlendityPart>() != null)
            {
                DestroyPart(child.gameObject);
                return;
            }
            child.SetParent(root, true);
        }

        void Destroy(GameObject go)
        {
            var mf = go.GetComponent<MeshFilter>();
            if (mf != null && mf.sharedMesh != null)
                Object.DestroyImmediate(mf.sharedMesh);
            var smr = go.GetComponent<SkinnedMeshRenderer>();
            if (smr && smr.sharedMesh != null)
                Object.DestroyImmediate(smr.sharedMesh);  // (review 378: a skinned mesh's too)
            var kids = new List<Transform>();
            foreach (Transform child in go.transform)
                kids.Add(child);
            foreach (var child in kids)
                MoveOrDrop(child);
            Object.DestroyImmediate(go);
        }

        // ------------------------------------------------------------ meshes

        void OnMesh(Dictionary<string, object> j, byte[] bin)
        {
            long id = j.GetId("id");
            GameObject go;
            if (!Objects.TryGetValue(id, out go) || go == null || (go.GetComponent<MeshFilter>() == null && go.GetComponent<SkinnedMeshRenderer>() == null))
                return;  // (a mesh for an object already gone)
            var gm = GlbReader.ReadMesh(bin);
            var mesh = BuildMesh(gm, go.name);
            meshes[id] = gm;
            if (Skin(go, j, gm, mesh))  // (round 378: a skinned or keyed mesh: a SkinnedMeshRenderer on the bones Live Link sent)
            {
                topology[id] = j.GetNumber("topology").ToString("R") + "/" + j.GetNumber("verts");
                AssignMaterials(id);
                ApplyWeights(id);
                AvatarsAgain(id);  // (round 381: its visemes or blink on an avatar)
                return;
            }
            var mf = Unskin(go);  // (review 378: a rig no more - its MeshFilter and MeshRenderer back)
            if (mf.sharedMesh != null)
                Object.DestroyImmediate(mf.sharedMesh);
            mf.sharedMesh = mesh;
            topology[id] = j.GetNumber("topology").ToString("R") + "/" + j.GetNumber("verts");
            AssignMaterials(id);
            ColliderAgain(id);  // (round 380: a Mesh or Convex collider is the mesh's)
        }

        static Mesh BuildMesh(GlbMesh gm, string name)
        {
            var mesh = new Mesh { name = name };
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
            return mesh;
        }

        /* Round 378: a mesh Live Link sent at rest with its skin ("skin": its joints the bone objects) or shape keys
         * ("shapes"): its MeshFilter and MeshRenderer give way to a SkinnedMeshRenderer, the bones its joints' objects, the
         * bind poses glTF's inverse bind matrices in Unity's axes (X mirrored on both sides), each key a blend shape.
         * False for a mesh with neither. */
        bool Skin(GameObject go, Dictionary<string, object> j, GlbMesh gm, Mesh mesh)
        {
            var skin = j.GetDict("skin");
            var shapes = j.GetList("shapes");
            if ((skin == null || gm.Joints == null || gm.InverseBinds == null) && (shapes == null || shapes.Count == 0))
                return false;
            var joints = skin != null && gm.Joints != null && gm.InverseBinds != null ? skin.GetList("joints") : null;
            int jn = joints != null ? joints.Count : 0;
            var bones = new Transform[jn];
            var binds = new Matrix4x4[jn];
            for (int b = 0; b < jn; b++)
            {
                GameObject bone;
                bones[b] = Objects.TryGetValue(System.Convert.ToInt64(joints[b]), out bone) && bone != null ? bone.transform : go.transform;
                binds[b] = BindPose(gm, b);
            }
            if (jn > 0)
            {
                mesh.boneWeights = BoneWeights(gm);
                mesh.bindposes = binds;
            }
            for (int t = 0; shapes != null && t < shapes.Count && t < gm.Targets.Count; t++)
                mesh.AddBlendShapeFrame(System.Convert.ToString(shapes[t]), 100f, ToVectors(gm.Targets[t]), null, null);
            var mf = go.GetComponent<MeshFilter>();
            if (mf != null)
                Object.DestroyImmediate(mf);
            var mr = go.GetComponent<MeshRenderer>();
            if (mr != null)
                Object.DestroyImmediate(mr);
            var smr = go.GetComponent<SkinnedMeshRenderer>();
            if (!smr)  // (not ??: a destroyed Unity object isn't null to it)
                smr = go.AddComponent<SkinnedMeshRenderer>();
            if (smr.sharedMesh != null)
                Object.DestroyImmediate(smr.sharedMesh);
            smr.sharedMesh = mesh;
            smr.bones = bones;
            smr.rootBone = jn > 0 ? bones[0] : go.transform;
            smr.updateWhenOffscreen = true;
            return true;
        }

        /* Joint b's inverse bind matrix (column-major) in Unity's axes: X mirrored on both sides. */
        static Matrix4x4 BindPose(GlbMesh gm, int b)
        {
            var m = new Matrix4x4();
            for (int i = 0; i < 16; i++)
                m[i % 4, i / 4] = gm.InverseBinds[b * 16 + i] * ((i % 4 == 0) != (i / 4 == 0) ? -1f : 1f);
            return m;
        }

        static BoneWeight[] BoneWeights(GlbMesh gm)
        {
            var weights = new BoneWeight[gm.VertexCount];
            for (int v = 0; v < weights.Length; v++)
            {
                int o = v * 4;
                weights[v] = new BoneWeight
                {
                    boneIndex0 = (int)gm.Joints[o], weight0 = gm.Weights[o], boneIndex1 = (int)gm.Joints[o + 1], weight1 = gm.Weights[o + 1],
                    boneIndex2 = (int)gm.Joints[o + 2], weight2 = gm.Weights[o + 2], boneIndex3 = (int)gm.Joints[o + 3], weight3 = gm.Weights[o + 3]
                };
            }
            return weights;
        }

        /* A mesh that is no longer a rig: its SkinnedMeshRenderer (and the mesh it held) gives way to a MeshFilter and a
         * MeshRenderer again. */
        static MeshFilter Unskin(GameObject go)
        {
            var smr = go.GetComponent<SkinnedMeshRenderer>();
            if (smr)
            {
                if (smr.sharedMesh != null)
                    Object.DestroyImmediate(smr.sharedMesh);
                Object.DestroyImmediate(smr);
            }
            var mf = go.GetComponent<MeshFilter>();
            if (!mf)
                mf = go.AddComponent<MeshFilter>();
            if (!go.GetComponent<MeshRenderer>())
                go.AddComponent<MeshRenderer>();
            return mf;
        }

        /* Round 378: a keyed mesh's shape weights (0-1, Unity's 0-100), in its keys' order; kept, since they can come
         * before the mesh (built off Blendity's main thread) and are set when it does. */
        readonly Dictionary<long, List<object>> shapeWeights = new Dictionary<long, List<object>>();

        void OnWeights(Dictionary<string, object> j)
        {
            long id = j.GetId("id");
            shapeWeights[id] = j.GetList("weights");
            ApplyWeights(id);
        }

        void ApplyWeights(long id)
        {
            GameObject go;
            List<object> w;
            if (!Objects.TryGetValue(id, out go) || go == null || !shapeWeights.TryGetValue(id, out w) || w == null)
                return;
            var smr = go.GetComponent<SkinnedMeshRenderer>();
            for (int i = 0; smr && smr.sharedMesh && i < w.Count && i < smr.sharedMesh.blendShapeCount; i++)
                smr.SetBlendShapeWeight(i, 100f * System.Convert.ToSingle(w[i]));
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
            if (go.GetComponent<SkinnedMeshRenderer>() != null)
                return;  // (round 378: it skins itself on its bones)
            var into = new float[gm.Positions.Length];
            if (!GlbReader.Deform(gm, bin, into))
                return;
            var mesh = go.GetComponent<MeshFilter>().sharedMesh;
            mesh.vertices = ToVectors(into);
            mesh.normals = ToVectors(GlbReader.SmoothNormals(gm, into));  // (not RecalculateNormals: UV seams stay smooth)
            mesh.RecalculateBounds();
            ColliderDeformed(id);  // (review 380: a Mesh or Convex collider cooked again once it stands still)
        }

        // ------------------------------------------------------------ materials

        void AssignMaterials(long id)
        {
            GameObject go;
            int[] keys;
            if (!Objects.TryGetValue(id, out go) || go == null || !slots.TryGetValue(id, out keys))
                return;
            Renderer mr = go.GetComponent<MeshRenderer>();
            if (mr == null)
                mr = go.GetComponent<SkinnedMeshRenderer>();  // (round 378)
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
            SetLods(id, go);  // (round 380: its levels wear the same)
        }

        /* Round 402 (ADR 0100): a material whose lilToon block is on becomes a lilToon material when the project has the
         * shader (its cutout / transparent variants by the surface) - every field of the block by lilToon's property name,
         * the colours as gamma (Material.SetColor linearises them), the vectors as they are; its textures stay the ones the
         * main maps carried (the block's own textures aren't sent). Without lilToon in the project the Standard / URP Lit
         * material stands, with the toon JSON as before. */
        static readonly string[,] LilFloats = {
            {"Lil Light Min Limit", "_LightMinLimit"}, {"Lil Light Max Limit", "_LightMaxLimit"}, {"Lil Monochrome Lighting", "_MonochromeLighting"}, {"Lil As Unlit", "_AsUnlit"},
            {"Lil Shadow Env Strength", "_ShadowEnvStrength"}, {"Lil Shadow Border", "_ShadowBorder"}, {"Lil Shadow Blur", "_ShadowBlur"}, {"Lil Shadow Strength", "_ShadowStrength"},
            {"Lil Shadow Receive", "_ShadowReceive"}, {"Lil Shadow Main Strength", "_ShadowMainStrength"}, {"Lil Shadow Border Range", "_ShadowBorderRange"},
            {"Lil Shadow 2nd Border", "_Shadow2ndBorder"}, {"Lil Shadow 2nd Blur", "_Shadow2ndBlur"}, {"Lil Shadow 2nd Receive", "_Shadow2ndReceive"},
            {"Lil Backface Force Shadow", "_BackfaceForceShadow"}, {"Lil Rim Main Strength", "_RimMainStrength"}, {"Lil Rim Fresnel Power", "_RimFresnelPower"},
            {"Lil Rim Border", "_RimBorder"}, {"Lil Rim Blur", "_RimBlur"}, {"Lil Rim Enable Lighting", "_RimEnableLighting"}, {"Lil Rim Shadow Mask", "_RimShadowMask"},
            {"Lil Rim Backface Mask", "_RimBackfaceMask"}, {"Lil Rim Dir Strength", "_RimDirStrength"}, {"Lil Rim Dir Range", "_RimDirRange"}, {"Lil Rim Indir Range", "_RimIndirRange"},
            {"Lil Rim Indir Border", "_RimIndirBorder"}, {"Lil Rim Indir Blur", "_RimIndirBlur"}, {"Lil Rim Blend Mode", "_RimBlendMode"},
            {"Lil Backlight Main Strength", "_BacklightMainStrength"}, {"Lil Backlight Receive Shadow", "_BacklightReceiveShadow"}, {"Lil Backlight Backface Mask", "_BacklightBackfaceMask"},
            {"Lil Backlight Border", "_BacklightBorder"}, {"Lil Backlight Blur", "_BacklightBlur"}, {"Lil Backlight Directivity", "_BacklightDirectivity"}, {"Lil Backlight View Strength", "_BacklightViewStrength"},
            {"Lil MatCap Blend", "_MatCapBlend"}, {"Lil MatCap Blend Mode", "_MatCapBlendMode"}, {"Lil MatCap Enable Lighting", "_MatCapEnableLighting"}, {"Lil MatCap Shadow Mask", "_MatCapShadowMask"},
            {"Lil MatCap Backface Mask", "_MatCapBackfaceMask"}, {"Lil MatCap Main Strength", "_MatCapMainStrength"}, {"Lil Emission Blend", "_EmissionBlend"}, {"Lil Emission Blend Mode", "_EmissionBlendMode"},
            {"Lil Emission Main Strength", "_EmissionMainStrength"}, {"Lil Smoothness", "_Smoothness"}, {"Lil Metallic", "_Metallic"}, {"Lil Reflectance", "_Reflectance"},
            {"Lil Specular Border", "_SpecularBorder"}, {"Lil Specular Blur", "_SpecularBlur"},
        };
        static readonly string[,] LilToggles = {
            {"Lil Use Shadow", "_UseShadow"}, {"Lil Shadow Post AO", "_ShadowPostAO"}, {"Lil Use Rim", "_UseRim"}, {"Lil Use Backlight", "_UseBacklight"}, {"Lil Use MatCap", "_UseMatCap"},
            {"Lil Use Emission", "_UseEmission"}, {"Lil Use Reflection", "_UseReflection"}, {"Lil Apply Specular", "_ApplySpecular"}, {"Lil Apply Reflection", "_ApplyReflection"},
            {"Lil Specular Toon", "_SpecularToon"},
        };
        static readonly string[,] LilColors = {  // (the colour's key, its alpha's key, lilToon's property)
            {"Lil Shadow Color", "", "_ShadowColor"}, {"Lil Shadow 2nd Color", "Lil Shadow 2nd Weight", "_Shadow2ndColor"}, {"Lil Shadow Border Color", "", "_ShadowBorderColor"},
            {"Lil Rim Color", "Lil Rim Alpha", "_RimColor"}, {"Lil Rim Indir Color", "Lil Rim Indir Alpha", "_RimIndirColor"}, {"Lil Backlight Color", "Lil Backlight Alpha", "_BacklightColor"},
            {"Lil MatCap Color", "Lil MatCap Alpha", "_MatCapColor"}, {"Lil Emission Color", "Lil Emission Alpha", "_EmissionColor"},
        };
        static void LilToonMaterial(Material m, Dictionary<string, object> toon)
        {
            if (toon == null || !toon.GetBool("lilToon"))
                return;
            var shader = Shader.Find("lilToon");
            if (shader == null)
                return;
            string mode = m.GetFloat("_Mode") >= 2f ? "Hidden/lilToonTransparent" : m.GetFloat("_Mode") >= 1f ? "Hidden/lilToonCutout" : "lilToon";  // (lilToon's variants are hidden shaders; Fade is transparent too)
            var variant = Shader.Find(mode) ?? shader;
            if (m.shader != variant)
                m.shader = variant;
            for (int k = 0; k < LilFloats.GetLength(0); k++)
                if (toon.ContainsKey(LilFloats[k, 0]) && m.HasProperty(LilFloats[k, 1]))
                    m.SetFloat(LilFloats[k, 1], (float)toon.GetNumber(LilFloats[k, 0], 0));
            for (int k = 0; k < LilToggles.GetLength(0); k++)
                if (toon.ContainsKey(LilToggles[k, 0]) && m.HasProperty(LilToggles[k, 1]))
                    m.SetFloat(LilToggles[k, 1], toon.GetBool(LilToggles[k, 0]) ? 1f : 0f);
            for (int k = 0; k < LilColors.GetLength(0); k++)
            {
                if (!toon.ContainsKey(LilColors[k, 0]) || !m.HasProperty(LilColors[k, 2]))
                    continue;
                var c = toon.GetFloats(LilColors[k, 0], 1f, 1f, 1f);
                var color = new Color(c[0], c[1], c[2]).gamma;  // (linear in Blendity; a Color property takes gamma)
                color.a = LilColors[k, 1].Length > 0 ? (float)toon.GetNumber(LilColors[k, 1], 1) : 1f;
                m.SetColor(LilColors[k, 2], color);
            }
            var hsv = toon.GetFloats("Lil Main HSV", 0f, 1f, 1f);
            if (m.HasProperty("_MainTexHSVG"))
                m.SetVector("_MainTexHSVG", new Vector4(hsv[0], hsv[1], hsv[2], (float)toon.GetNumber("Lil Main Gamma", 1)));
            if (m.HasProperty("_Cull"))
                m.SetFloat("_Cull", m.GetFloat("_Cull"));  // (as SetSurface left it)
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
            materialMsgs[key] = j;
            var gltf = j.GetDict("gltf") ?? new Dictionary<string, object>();
            var pbr = gltf.GetDict("pbrMetallicRoughness") ?? new Dictionary<string, object>();
            var c = pbr.GetFloats("baseColorFactor", 0.8f, 0.8f, 0.8f, 1f);
            var color = new Color(c[0], c[1], c[2]).gamma;  // (Live Link's colours are linear; a material's are set as gamma)
            color.a = c[3];
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
            var emission = (new Color(e[0], e[1], e[2]) * strength).gamma;
            m.SetColor("_EmissionColor", emission);
            if (emission.maxColorComponent > 0)
                m.EnableKeyword("_EMISSION");
            else
                m.DisableKeyword("_EMISSION");
            SetSurface(m, gltf.GetString("alphaMode", "OPAQUE"), (float)gltf.GetNumber("alphaCutoff", 0.5), gltf.GetBool("doubleSided"));
            var maps = j.GetDict("maps");
            SetTexture(m, maps, "Base Map", "_BaseMap", "_MainTex", false);
            SetTexture(m, maps, "Normal Map", "_BumpMap", "_BumpMap", true);
            bool metal = SetTexture(m, maps, "Metallic Map", "_MetallicGlossMap", "_MetallicGlossMap", false);  // (round 377: every map Unity's Lit has)
            SetKeyword(m, "_METALLICSPECGLOSSMAP", metal);  // (URP's)
            SetKeyword(m, "_METALLICGLOSSMAP", metal);      // (the built-in Standard's - review 377)
            if (SetTexture(m, maps, "Emission Map", "_EmissionMap", "_EmissionMap", false))
                m.EnableKeyword("_EMISSION");
            var toon = ext != null ? ext.GetDict("BLENDITY_toon") : null;
            toonFields[key] = toon != null ? Serialize(toon) : "";
            LilToonMaterial(m, toon);  // (round 402: a lilToon material when the block is on and the project has lilToon)
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

        /* A map's texture: the one Live Link sent its bytes for (round 377: no stale copy cached by path), else the file
         * at its path. True when one was set. */
        bool SetTexture(Material m, Dictionary<string, object> maps, string slot, string urp, string builtin, bool normal)
        {
            var entry = maps != null ? maps.GetDict(slot) : null;
            if (entry == null)
            {
                m.SetTexture(urp, null);  // (a map taken off in Blendity: not kept - review 377)
                m.SetTexture(builtin, null);
                return false;
            }
            Texture2D tex;
            if (!texByKey.TryGetValue((int)entry.GetNumber("texture", 0), out tex) || tex == null)
                tex = LoadTexture(entry.GetString("path"), normal || entry.GetBool("non_color"));
            if (tex == null)
                return false;
            m.SetTexture(urp, tex);
            m.SetTexture(builtin, tex);
            if (normal)
                m.EnableKeyword("_NORMALMAP");
            return true;
        }

        /* Round 377: a texture's pixels as PNG bytes (painted, generated or a file's), loaded into a texture of its own
         * (the same one when it comes again); the materials naming its key built again. */
        void OnTexture(Dictionary<string, object> j, byte[] png)
        {
            int key = (int)j.GetNumber("key");
            bool linear = j.GetBool("non_color");
            Texture2D tex;
            if (!texByKey.TryGetValue(key, out tex) || tex == null || tex.isDataSRGB == linear)
            {
                tex = new Texture2D(2, 2, TextureFormat.RGBA32, true, linear);
                texByKey[key] = tex;
            }
            tex.name = j.GetString("name", "Texture");
            if (png == null || !tex.LoadImage(png))
                return;
            var again = new List<Dictionary<string, object>>();
            foreach (var kv in materialMsgs)
            {
                var maps = kv.Value.GetDict("maps");
                if (maps == null)
                    continue;
                foreach (var entry in maps.Values)
                {
                    var e = entry as Dictionary<string, object>;
                    if (e != null && (int)e.GetNumber("texture", 0) == key)
                    {
                        again.Add(kv.Value);
                        break;
                    }
                }
            }
            foreach (var msg in again)
                OnMaterial(msg);
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
            l.color = new Color(c[0], c[1], c[2]).gamma;  // (Live Link's colours are linear)
            l.intensity = (float)j.GetNumber("intensity", 1);  // (Blendity's intensity is Unity's)
            l.range = (float)j.GetNumber("range", 10);
            l.spotAngle = (float)j.GetNumber("spot_angle", 30);
            l.innerSpotAngle = (float)j.GetNumber("inner_spot_angle", 21.8);
            var a = j.GetFloats("area", 1, 1);
#if UNITY_EDITOR
            l.areaSize = new Vector2(a[0], a[1]);
#endif
            int shadows = (int)j.GetNumber("shadows", 2);  // (round 401: Unity's Shadow Type and Strength, the Indirect Multiplier)
            l.shadows = shadows == 0 ? LightShadows.None : shadows == 1 ? LightShadows.Hard : LightShadows.Soft;
            l.shadowStrength = (float)j.GetNumber("shadow_strength", 1);
            l.bounceIntensity = (float)j.GetNumber("bounce_intensity", 1);
            OnLightVolumeLight(go, j, type);  // (round 397: VRC Light Volumes' light beside it, when Light Volume is on)
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
