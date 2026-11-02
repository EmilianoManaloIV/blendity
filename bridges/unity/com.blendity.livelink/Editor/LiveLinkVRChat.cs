// SPDX-License-Identifier: GPL-2.0-or-later
// Round 381: VRChat's setup through Live Link (docs/LIVELINK.md, `avatar`, `springs`, the object's `static`). With
// VRChat's avatar SDK in the project, the avatar root's Avatar Descriptor (its view, lip sync on the vrc.v_* blend shapes,
// eye look and blink) and a PhysBone per spring chain; with its world SDK, a Scene Descriptor when the scene has none.
// The SDK's types are found by name and set through reflection, so the bridge compiles - and sets nothing VRChat's -
// without it; a field it can't find or set is left alone. The link owns the descriptor fields it sets: it writes each
// group of them when Blendity's values change (review 381: a resent face had undone the user's tuning).
using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Blendity.LiveLink
{
    public sealed partial class LiveLinkApplier
    {
        public const string AvatarDescriptorType = "VRC.SDK3.Avatars.Components.VRCAvatarDescriptor";
        public const string PhysBoneType = "VRC.SDK3.Dynamics.PhysBone.Components.VRCPhysBone";
        public const string SceneDescriptorType = "VRC.SDK3.Components.VRCSceneDescriptor";
        public const string PhysBonesName = "Blendity PhysBones";
        const StaticEditorFlags StaticFlags = StaticEditorFlags.ContributeGI | StaticEditorFlags.BatchingStatic | StaticEditorFlags.OccluderStatic |
                                              StaticEditorFlags.OccludeeStatic | StaticEditorFlags.ReflectionProbeStatic;
        static readonly string[] BlinkNames = { "vrc.blink_left", "vrc.blink_right" };
        readonly Dictionary<long, Dictionary<string, object>> avatarMsgs = new Dictionary<long, Dictionary<string, object>>();
        readonly Dictionary<long, Dictionary<string, string>> avatarWritten = new Dictionary<long, Dictionary<string, string>>();  // (each group's values as last written)
        readonly HashSet<long> madeStatic = new HashSet<long>();  // (objects whose static flags this link set: only those it clears)
        static Dictionary<string, Type> components;  // (every component type by its full name: made once a domain)

        /// <summary>A component type by its full name (VRChat's SDK, when the project has it), or null.</summary>
        public static Type FindType(string fullName)
        {
            if (components == null)
            {
                components = new Dictionary<string, Type>();
                foreach (var t in TypeCache.GetTypesDerivedFrom<Component>())
                    components[t.FullName ?? t.Name] = t;
            }
            Type found;
            return components.TryGetValue(fullName, out found) ? found : null;
        }

        /* A public field set when it is there and takes the value (an enum by its name); left alone otherwise. */
        static void SetMember(object target, string name, object value)
        {
            var f = target.GetType().GetField(name, BindingFlags.Public | BindingFlags.Instance);
            if (f == null)
                return;
            if (f.FieldType.IsEnum && value is string)
            {
                if (Enum.IsDefined(f.FieldType, value))
                    f.SetValue(target, Enum.Parse(f.FieldType, (string)value));
                return;
            }
            if (value == null ? !f.FieldType.IsValueType : f.FieldType.IsInstanceOfType(value))
                f.SetValue(target, value);
        }

        // ------------------------------------------------------------ static flags

        /* Static flags on an object Blendity marks static; taken off only where this link put them (the user's own stay). */
        void SetStatic(long id, GameObject go, bool on)
        {
            if (!on && !madeStatic.Remove(id))
                return;
            if (on)
                madeStatic.Add(id);
            var flags = GameObjectUtility.GetStaticEditorFlags(go);
            var want = on ? flags | StaticFlags : flags & ~StaticFlags;
            if (want != flags)
                GameObjectUtility.SetStaticEditorFlags(go, want);
        }

        // ------------------------------------------------------------ avatars

        void OnAvatar(Dictionary<string, object> j)
        {
            long id = j.GetId("id");
            avatarMsgs[id] = j;
            ApplyAvatar(id);
        }

        /* The Avatar Descriptor on the avatar root (the object above the armature and its meshes), made when the SDK is
         * here; an armature no longer an avatar ("none") leaves it as it is - it may be the user's. */
        void ApplyAvatar(long id)
        {
            var type = FindType(AvatarDescriptorType);
            Dictionary<string, object> j;
            GameObject rootGo;
            if (type == null || !avatarMsgs.TryGetValue(id, out j) || j.GetBool("none"))
                return;
            if (!Objects.TryGetValue(j.GetId("root") != 0 ? j.GetId("root") : id, out rootGo) || rootGo == null)
                return;
            try
            {
                var desc = rootGo.GetComponent(type) ?? rootGo.AddComponent(type);
                Dictionary<string, string> written;
                if (!avatarWritten.TryGetValue(id, out written))
                    avatarWritten[id] = written = new Dictionary<string, string>();
                Once(written, "view", Key(j.Get("view")), () => SetMember(desc, "ViewPosition", Position(j.GetFloats("view", 0, 1.6f, 0.05f))));
                Once(written, "visemes", VisemesKey(j.GetDict("visemes")), () => LipSync(desc, j.GetDict("visemes")));
                Once(written, "eyes", EyesKey(j.GetDict("eyes"), j.GetDict("blink")), () => EyeLook(desc, j.GetDict("eyes"), j.GetDict("blink")));
                EditorUtility.SetDirty(desc);
            }
            catch (Exception e)
            {
                Debug.LogWarning("Blendity Live Link: VRChat's Avatar Descriptor not set up (" + e.Message + ")");
            }
        }

        /* A parsed JSON value as text, to tell when a group's values changed. */
        static string Key(object o)
        {
            var d = o as Dictionary<string, object>;
            if (d != null)
            {
                var parts = new List<string>();
                foreach (var kv in d)
                    parts.Add(kv.Key + ":" + Key(kv.Value));
                return "{" + string.Join(",", parts) + "}";
            }
            var l = o as List<object>;
            if (l != null)
                return "[" + string.Join(",", l.ConvertAll(Key)) + "]";
            return Convert.ToString(o, System.Globalization.CultureInfo.InvariantCulture);
        }

        /* A group of fields written when its values changed since the link last wrote them. */
        static void Once(Dictionary<string, string> written, string group, string key, Action write)
        {
            string was;
            if (written.TryGetValue(group, out was) && was == key)
                return;
            write();
            written[group] = key;
        }

        /* A group's key: the values it is written from, a mesh's renderer counted only once it is there (it comes after). */
        string VisemesKey(Dictionary<string, object> visemes)
        {
            var smr = SkinnedOf(visemes);
            return Key(visemes) + (smr != null && smr.sharedMesh != null ? smr.GetHashCode().ToString() : "-");
        }

        string EyesKey(Dictionary<string, object> eyes, Dictionary<string, object> blink)
        {
            var smr = SkinnedOf(blink);
            return Key(eyes) + Key(blink) + (smr != null && smr.sharedMesh != null ? smr.GetHashCode().ToString() : "-");
        }

        SkinnedMeshRenderer SkinnedOf(Dictionary<string, object> keys)
        {
            GameObject go;
            return keys != null && Objects.TryGetValue(keys.GetId("mesh"), out go) && go != null ? go.GetComponent<SkinnedMeshRenderer>() : null;
        }

        Transform Bone(Dictionary<string, object> eyes, string side)
        {
            GameObject go;
            return eyes != null && Objects.TryGetValue(eyes.GetId(side), out go) && go != null ? go.transform : null;
        }

        /* Lip sync on the visemes mesh's 15 blend shapes; off (Default) when there are none. */
        void LipSync(Component desc, Dictionary<string, object> visemes)
        {
            var smr = SkinnedOf(visemes);
            if (smr == null || smr.sharedMesh == null)
            {
                SetMember(desc, "lipSync", "Default");
                SetMember(desc, "VisemeSkinnedMesh", null);
                return;  // (its mesh still on its way, or none: set again when it comes)
            }
            var names = new List<string>();
            foreach (var n in visemes.GetList("shapes") ?? new List<object>())
                names.Add(Convert.ToString(n));
            SetMember(desc, "lipSync", "VisemeBlendShape");
            SetMember(desc, "VisemeSkinnedMesh", smr);
            SetMember(desc, "VisemeBlendShapes", names.ToArray());
        }

        /* Eye look with both eyes and the blink (the first of VRChat's blink keys the blink mesh has); off without eyes. */
        void EyeLook(Component desc, Dictionary<string, object> eyes, Dictionary<string, object> blink)
        {
            var field = desc.GetType().GetField("customEyeLookSettings", BindingFlags.Public | BindingFlags.Instance);
            Transform left = Bone(eyes, "left"), right = Bone(eyes, "right");
            SetMember(desc, "enableEyeLook", left != null && right != null);
            if (field == null || left == null || right == null)
                return;
            object settings = field.GetValue(desc);  // (a struct: changed boxed, set back)
            if (settings == null)
                return;
            SetMember(settings, "leftEye", left);
            SetMember(settings, "rightEye", right);
            var smr = SkinnedOf(blink);
            int index = BlinkIndex(smr);
            SetMember(settings, "eyelidType", index >= 0 ? "Blendshapes" : "None");
            if (index >= 0)
            {
                SetMember(settings, "eyelidsSkinnedMesh", smr);
                SetMember(settings, "eyelidsBlendshapes", new[] { index, -1, -1 });  // (VRChat's blink, looking up, looking down)
            }
            field.SetValue(desc, settings);
        }

        static int BlinkIndex(SkinnedMeshRenderer smr)
        {
            if (smr == null || smr.sharedMesh == null)
                return -1;
            foreach (var n in BlinkNames)
            {
                int i = smr.sharedMesh.GetBlendShapeIndex(n);
                if (i >= 0)
                    return i;
            }
            return -1;
        }

        /* A mesh came: an avatar naming it for its visemes or blink is set again (its groups' keys now count it). */
        void AvatarsAgain(long meshId)
        {
            foreach (var kv in new List<KeyValuePair<long, Dictionary<string, object>>>(avatarMsgs))
            {
                var v = kv.Value.GetDict("visemes");
                var b = kv.Value.GetDict("blink");
                if ((v != null && v.GetId("mesh") == meshId) || (b != null && b.GetId("mesh") == meshId))
                    ApplyAvatar(kv.Key);
            }
        }

        // ------------------------------------------------------------ PhysBones

        /* A PhysBone per spring chain on the armature's child "Blendity PhysBones" (a BlendityPart, made again each time),
         * its root the chain's first bone - the importer's mapping turned round (unity_scene.cpp): Pull the Stiffness,
         * Spring what the Damping leaves, Gravity and Radius as they are. */
        void OnSprings(Dictionary<string, object> j)
        {
            var type = FindType(PhysBoneType);
            GameObject go;
            if (type == null || !Objects.TryGetValue(j.GetId("id"), out go) || go == null)
                return;
            var old = Part(go, BlendityPart.PhysBonesRole, 0);
            if (old != null)
                UnityEngine.Object.DestroyImmediate(old.gameObject);
            var chains = j.GetList("chains");
            if (chains == null || chains.Count == 0)
                return;
            var holder = new GameObject(PhysBonesName, typeof(BlendityPart));
            holder.GetComponent<BlendityPart>().Role = BlendityPart.PhysBonesRole;
            holder.transform.SetParent(go.transform, false);
            try
            {
                foreach (Dictionary<string, object> c in chains)
                    PhysBone(holder.AddComponent(type), c);
            }
            catch (Exception e)
            {
                Debug.LogWarning("Blendity Live Link: VRChat's PhysBones not set up (" + e.Message + ")");
            }
        }

        void PhysBone(Component pb, Dictionary<string, object> c)
        {
            GameObject bone;
            if (Objects.TryGetValue(c.GetId("root"), out bone) && bone != null)
                SetMember(pb, "rootTransform", bone.transform);
            SetMember(pb, "pull", Mathf.Clamp01((float)c.GetNumber("stiffness")));
            SetMember(pb, "spring", Mathf.Clamp01(1f - (float)c.GetNumber("damping")));
            SetMember(pb, "gravity", Mathf.Clamp((float)c.GetNumber("gravity"), -1f, 1f));
            SetMember(pb, "radius", Mathf.Max(0f, (float)c.GetNumber("radius")));
        }

        // ------------------------------------------------------------ worlds

        /* With VRChat's world SDK: a Scene Descriptor on the Live Link root when the scene has none (a world's VRCWorld
         * prefab has one - review 381: two are refused), its spawn the root. */
        void EnsureSceneDescriptor()
        {
            var type = FindType(SceneDescriptorType);
            if (type == null || UnityEngine.Object.FindObjectsByType(type, FindObjectsInactive.Include, FindObjectsSortMode.None).Length > 0)
                return;
            SetMember(root.gameObject.AddComponent(type), "spawns", new[] { root });
        }
    }
}
