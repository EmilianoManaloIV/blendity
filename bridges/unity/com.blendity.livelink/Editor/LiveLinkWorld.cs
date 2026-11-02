// SPDX-License-Identifier: GPL-2.0-or-later
// Round 380: Live Link's LODs and colliders (docs/LIVELINK.md, `lod`, `lodmesh`, `collider`) - an LOD Group with the
// object's renderer as level 0 and child renderers <name>_LOD<n> for the rest; a child "Blendity Collider" with a Box,
// Sphere, Capsule or Mesh Collider. The bridge's children carry a BlendityPart, so a rename or the user's own children
// and colliders never confuse them (review 380).
using System.Collections.Generic;
using UnityEngine;

namespace Blendity.LiveLink
{
    public sealed partial class LiveLinkApplier
    {
        const int MaxLevels = 4;  // (Blendity's LODGroupComponent::kMax)
        const double ColliderSettle = 0.5;  // (seconds a deforming mesh stands still before its Mesh or Convex collider is cooked again)
        readonly Dictionary<long, List<float>> lodScreens = new Dictionary<long, List<float>>();
        readonly Dictionary<long, bool> lodCull = new Dictionary<long, bool>();
        readonly Dictionary<long, Dictionary<string, object>> colliderMsgs = new Dictionary<long, Dictionary<string, object>>();
        readonly Dictionary<long, double> colliderStale = new Dictionary<long, double>();  // (a deformed mesh's collider: when it was last deformed)

        // ------------------------------------------------------------ LODs

        void OnLod(Dictionary<string, object> j)
        {
            long id = j.GetId("id");
            GameObject go;
            if (!Objects.TryGetValue(id, out go) || go == null)
                return;
            var screens = new List<float>();
            foreach (var s in j.GetList("screen") ?? new List<object>())
                screens.Add(System.Convert.ToSingle(s));
            lodScreens[id] = screens;
            lodCull[id] = j.GetBool("cull");
            TrimLevels(go, screens.Count);
            if (!HasGroup(id))
            {
                var group = go.GetComponent<LODGroup>();
                if (group)
                    Object.DestroyImmediate(group);
                return;
            }
            SetLods(id, go);
        }

        /* A group to keep: two levels or more, or one that culls (Unity's "cull small props far away"). */
        bool HasGroup(long id)
        {
            List<float> screens;
            return lodScreens.TryGetValue(id, out screens) && (screens.Count >= 2 || (screens.Count == 1 && lodCull[id]));
        }

        void OnLodMesh(Dictionary<string, object> j, byte[] bin)
        {
            long id = j.GetId("id");
            int level = (int)j.GetNumber("level");
            GameObject go;
            List<float> screens;
            if (!Objects.TryGetValue(id, out go) || go == null || level < 1)
                return;
            if (!lodScreens.TryGetValue(id, out screens) || level >= screens.Count)
                return;  // (a level the group no longer has: built before it shrank or went - review 380)
            var levelGo = LevelObject(go, level);
            var mf = levelGo.GetComponent<MeshFilter>();
            if (mf.sharedMesh != null)
                Object.DestroyImmediate(mf.sharedMesh);
            mf.sharedMesh = BuildMesh(GlbReader.ReadMesh(bin), go.name + "_LOD" + level);
            SetLods(id, go);
        }

        static BlendityPart Part(GameObject go, string role, int number)
        {
            foreach (Transform t in go.transform)
            {
                var p = t.GetComponent<BlendityPart>();
                if (p != null && p.Role == role && p.Number == number)
                    return p;
            }
            return null;
        }

        static GameObject LevelObject(GameObject go, int level)
        {
            var p = Part(go, BlendityPart.Level, level);
            if (p != null)
                return p.gameObject;
            var levelGo = new GameObject(go.name + "_LOD" + level, typeof(MeshFilter), typeof(MeshRenderer), typeof(BlendityPart));
            levelGo.GetComponent<BlendityPart>().Number = level;
            levelGo.transform.SetParent(go.transform, false);
            return levelGo;
        }

        static void TrimLevels(GameObject go, int levels)
        {
            for (int level = Mathf.Max(levels, 1); level < MaxLevels; level++)
            {
                var p = Part(go, BlendityPart.Level, level);
                if (p != null)
                    DestroyPart(p.gameObject);
            }
        }

        static void DestroyPart(GameObject partGo)
        {
            var mf = partGo.GetComponent<MeshFilter>();
            if (mf && mf.sharedMesh)
                Object.DestroyImmediate(mf.sharedMesh);
            Object.DestroyImmediate(partGo);
        }

        /* The group's levels: level 0 the object's own renderer, the others its level children wearing its materials and
         * named for it; each shown while the object stands at least its screen size of the view's height (Unity's
         * transition height, as Blendity's lod_level picks), the last down to nothing unless it culls. */
        void SetLods(long id, GameObject go)
        {
            if (!HasGroup(id))
                return;
            var screens = lodScreens[id];
            var own = go.GetComponent<Renderer>();
            var lods = new LOD[screens.Count];
            for (int level = 0; level < screens.Count; level++)
            {
                var r = level == 0 ? own : LevelRenderer(go, level);
                if (level > 0 && r && own)
                {
                    r.sharedMaterials = own.sharedMaterials;
                    r.name = go.name + "_LOD" + level;
                }
                float to = level + 1 < screens.Count || lodCull[id] ? screens[level] : 0f;
                lods[level] = new LOD(Mathf.Clamp01(to), r ? new[] { r } : new Renderer[0]);
            }
            var group = go.GetComponent<LODGroup>();
            if (!group)
                group = go.AddComponent<LODGroup>();
            group.SetLODs(lods);
            group.RecalculateBounds();
        }

        static Renderer LevelRenderer(GameObject go, int level)
        {
            var p = Part(go, BlendityPart.Level, level);
            return p != null ? p.GetComponent<Renderer>() : null;
        }

        // ------------------------------------------------------------ colliders

        /* The collider on a child of its own ("Blendity Collider", made again each time): the user's colliders on the
         * object are left alone. */
        void OnCollider(Dictionary<string, object> j)
        {
            long id = j.GetId("id");
            GameObject go;
            colliderMsgs[id] = j;
            colliderStale.Remove(id);
            if (!Objects.TryGetValue(id, out go) || go == null)
                return;
            var old = Part(go, BlendityPart.ColliderRole, 0);
            if (old != null)
                Object.DestroyImmediate(old.gameObject);
            string shape = j.GetString("shape");
            if (shape == "none" || shape == "")
                return;
            var holder = new GameObject("Blendity Collider", typeof(BlendityPart));
            holder.GetComponent<BlendityPart>().Role = BlendityPart.ColliderRole;
            holder.transform.SetParent(go.transform, false);
            AddCollider(holder, go, shape, j);
        }

        static void AddCollider(GameObject holder, GameObject go, string shape, Dictionary<string, object> j)
        {
            var center = Position(j.GetFloats("center", 0, 0, 0));
            if (shape == "box")
            {
                var box = holder.AddComponent<BoxCollider>();
                box.center = center;
                box.size = Scale(j.GetFloats("size", 1, 1, 1));
                return;
            }
            if (shape == "sphere")
            {
                var sphere = holder.AddComponent<SphereCollider>();
                sphere.center = center;
                sphere.radius = (float)j.GetNumber("radius", 0.5);
                return;
            }
            if (shape == "capsule")
            {
                var capsule = holder.AddComponent<CapsuleCollider>();
                capsule.center = center;
                capsule.radius = (float)j.GetNumber("radius", 0.5);
                capsule.height = (float)j.GetNumber("height", 2);
                capsule.direction = 1;  // (up Y, as Blendity's)
                return;
            }
            var mc = holder.AddComponent<MeshCollider>();
            mc.sharedMesh = OwnMesh(go);
            mc.convex = shape == "convex";
        }

        /* A mesh sent again: its Mesh or Convex collider takes the new one. */
        void ColliderAgain(long id)
        {
            Dictionary<string, object> j;
            if (colliderMsgs.TryGetValue(id, out j))
                OnCollider(j);
        }

        /* A mesh deformed: its Mesh or Convex collider is cooked again once the mesh has stood still. */
        void ColliderDeformed(long id)
        {
            Dictionary<string, object> j;
            if (colliderMsgs.TryGetValue(id, out j) && (j.GetString("shape") == "mesh" || j.GetString("shape") == "convex"))
                colliderStale[id] = UnityEditor.EditorApplication.timeSinceStartup;
        }

        /// <summary>After a batch of messages: the colliders of meshes that stopped deforming cooked again.</summary>
        public void Settle()
        {
            double now = UnityEditor.EditorApplication.timeSinceStartup;
            var due = new List<long>();
            foreach (var kv in colliderStale)
                if (now - kv.Value >= ColliderSettle)
                    due.Add(kv.Key);
            foreach (long id in due)
                ColliderAgain(id);
        }

        static Mesh OwnMesh(GameObject go)
        {
            var mf = go.GetComponent<MeshFilter>();
            if (mf)
                return mf.sharedMesh;
            var smr = go.GetComponent<SkinnedMeshRenderer>();
            return smr ? smr.sharedMesh : null;
        }
    }
}
