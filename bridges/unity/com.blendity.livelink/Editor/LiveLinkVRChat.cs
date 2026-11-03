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
        public const string PipelineManagerType = "VRC.Core.PipelineManager";
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
                /* Round 396 (the real SDK's checks): an avatar's root needs an Animator ("This avatar does not contain an
                 * Animator") and one pipeline manager - added when missing, never replaced. */
                if (rootGo.GetComponent<Animator>() == null)
                    rootGo.AddComponent<Animator>();
                AddPipelineManager(rootGo);
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

        // ------------------------------------------------------------ round 402: the avatar's components in full

        public const string PhysBoneColliderType = "VRC.SDK3.Dynamics.PhysBone.Components.VRCPhysBoneCollider";
        public const string ContactSenderType = "VRC.SDK3.Dynamics.Contact.Components.VRCContactSender";
        public const string ContactReceiverType = "VRC.SDK3.Dynamics.Contact.Components.VRCContactReceiver";
        readonly Dictionary<long, string> vrcWritten = new Dictionary<long, string>();  // (each object's last components message: set again only when it changed)

        /* The avatar's VRChat components as Blendity holds them (round 402, ADR 0100): the descriptor's every field, each
         * PhysBone, PhysBone Collider, Contact Sender / Receiver and VRC Constraint - set on the real SDK's components by
         * reflection (enums by their names, object references by Blendity's ids, assets by their paths); "none" takes off
         * what this link put on. The link owns what it sets: a message equal to the last is left alone. */
        void OnVrcComponents(Dictionary<string, object> j)
        {
            long id = j.GetId("id");
            GameObject go;
            if (!Objects.TryGetValue(id, out go) || go == null)
                return;
            string key = Key(j);
            string last;
            if (vrcWritten.TryGetValue(id, out last) && last == key)
                return;
            vrcWritten[id] = key;
            if (j.GetBool("none"))
                return;
            var list = j.Get("components") as List<object>;
            if (list == null)
                return;
            var kept = new HashSet<Component>();
            foreach (var o in list)
            {
                var c = o as Dictionary<string, object>;
                if (c == null)
                    continue;
                try
                {
                    Component made = null;
                    switch (c.GetString("kind", ""))
                    {
                        case "descriptor": made = SetDescriptor(go, c); break;
                        case "physbone": made = SetPhysBone(go, c, kept); break;
                        case "collider": made = SetPhysBoneCollider(go, c, kept); break;
                        case "sender": made = SetContact(go, c, false, kept); break;
                        case "receiver": made = SetContact(go, c, true, kept); break;
                        case "constraint": made = SetConstraint(go, c, kept); break;
                    }
                    if (made != null)
                        EditorUtility.SetDirty(made);
                }
                catch (Exception e)
                {
                    Debug.LogWarning("Blendity Live Link: a VRChat component of " + go.name + " not set (" + e.Message + ")");
                }
            }
        }

        static object EnumOf(Type t, string name)
        {
            return t.IsEnum && Enum.IsDefined(t, name) ? Enum.Parse(t, name) : null;
        }

        /* A field set by its name: a number, a bool, a string, an enum by its name, a Transform by Blendity's id, a Vector3
         * / Quaternion from its numbers, an asset by its path. */
        void Set(object target, string field, object value)
        {
            if (target == null)
                return;
            var f = target.GetType().GetField(field, BindingFlags.Public | BindingFlags.Instance);
            if (f == null)
                return;
            var t = f.FieldType;
            object v = null;
            if (t.IsEnum && value is string) v = EnumOf(t, (string)value);
            else if (t == typeof(float) && value is double) v = (float)(double)value;
            else if (t == typeof(int) && value is double) v = (int)(double)value;
            else if (t == typeof(bool) && value is bool) v = value;
            else if (t == typeof(string) && value is string) v = value;
            else if (t == typeof(Transform) && value is double) v = TransformOf((long)(double)value);
            else if (t == typeof(Vector3) && value is List<object>) { var a = Floats(value); v = a.Length >= 3 ? (object)new Vector3(a[0], a[1], a[2]) : null; }
            else if (t == typeof(Quaternion) && value is List<object>) { var a = Floats(value); v = a.Length >= 4 ? (object)new Quaternion(a[0], a[1], a[2], a[3]) : null; }
            else if (typeof(Component).IsAssignableFrom(t) && value is Component && t.IsInstanceOfType(value)) v = value;
            else if (t == typeof(string[]) && value is List<object>) v = ((List<object>)value).ConvertAll(x => x as string ?? "").ToArray();
            else if (t == typeof(int[]) && value is List<object>) { var a = Floats(value); var ints = new int[a.Length]; for (int i = 0; i < a.Length; i++) ints[i] = (int)a[i]; v = ints; }
            else if (t == typeof(int[]) && value is int[]) v = value;
            else if (t == typeof(string[]) && value is string[]) v = value;
            else if (typeof(UnityEngine.Object).IsAssignableFrom(t) && value is string) v = ((string)value).Length > 0 ? AssetDatabase.LoadAssetAtPath((string)value, t) : null;
            if (v != null)  // (review 402: never a null or an empty path over what Unity holds - a reference Blendity couldn't name stays)
                f.SetValue(target, v);
        }

        static float[] Floats(object value)
        {
            var l = value as List<object>;
            if (l == null)
                return new float[0];
            var a = new float[l.Count];
            for (int i = 0; i < l.Count; i++)
                a[i] = l[i] is double ? (float)(double)l[i] : 0f;
            return a;
        }

        Transform TransformOf(long id)
        {
            GameObject go;
            return id != 0 && Objects.TryGetValue(id, out go) && go != null ? go.transform : null;
        }

        /* Fields from the message's keys with the same names (the SDK's serialized names). */
        void SetAll(object target, Dictionary<string, object> c, params string[] names)
        {
            foreach (var n in names)
                if (c.ContainsKey(n))
                    Set(target, n, c[n]);
        }

        /* A component of the SDK's type on the object, one per Blendity component in the message's order (the extras this
         * link made earlier are reused in turn). */
        Component Nth(GameObject go, Type type, HashSet<Component> kept)
        {
            if (type == null)
                return null;
            foreach (var c in go.GetComponents(type))
                if (!kept.Contains(c))
                {
                    kept.Add(c);
                    return c;
                }
            var made = go.AddComponent(type);
            kept.Add(made);
            return made;
        }

        Component SetDescriptor(GameObject go, Dictionary<string, object> c)
        {
            var type = FindType(AvatarDescriptorType);
            if (type == null)
                return null;
            var desc = go.GetComponent(type) ?? go.AddComponent(type);
            if (go.GetComponent<Animator>() == null)
                go.AddComponent<Animator>();
            AddPipelineManager(go);
            Set(desc, "ViewPosition", c["view"]);
            Set(desc, "lipSync", c.GetString("lipSync", "Default"));
            Set(desc, "lipSyncJawBone", c["jaw"]);
            Set(desc, "lipSyncJawClosed", c["jawClosed"]);
            Set(desc, "lipSyncJawOpen", c["jawOpen"]);
            var mesh = TransformOf((long)c.GetNumber("visemeMesh", 0));
            Set(desc, "VisemeSkinnedMesh", mesh != null ? mesh.GetComponent<SkinnedMeshRenderer>() : null);
            Set(desc, "MouthOpenBlendShapeName", c.GetString("mouthOpen", ""));
            Set(desc, "lipSyncJawBlendShapeName", c.GetString("jawFlapShape", ""));
            var visemes = c.Get("visemes") as List<object>;
            if (visemes != null)
                Set(desc, "VisemeBlendShapes", visemes.ConvertAll(v => v as string ?? "").ToArray());
            Set(desc, "enableEyeLook", c.GetBool("eyeLook"));
            var field = type.GetField("customEyeLookSettings", BindingFlags.Public | BindingFlags.Instance);
            object settings = field != null ? field.GetValue(desc) : null;
            if (settings != null)
            {
                var move = settings.GetType().GetField("eyeMovement", BindingFlags.Public | BindingFlags.Instance);
                object m = move != null ? move.GetValue(settings) : null;
                if (m != null)
                {
                    Set(m, "confidence", c["confidence"]);
                    Set(m, "excitement", c["excitement"]);
                    move.SetValue(settings, m);
                }
                Set(settings, "leftEye", c["leftEye"]);
                Set(settings, "rightEye", c["rightEye"]);
                string[] looks = { "eyesLookingStraight", "eyesLookingUp", "eyesLookingDown", "eyesLookingLeft", "eyesLookingRight" };
                string[] keys = { "straight", "up", "down", "left", "right" };
                for (int k = 0; k < 5; k++)
                {
                    var lf = settings.GetType().GetField(looks[k], BindingFlags.Public | BindingFlags.Instance);
                    var lj = c.GetDict(keys[k]);
                    object look = lf != null ? lf.GetValue(settings) : null;
                    if (look == null || lj == null)
                        continue;
                    Set(look, "linked", lj.GetBool("linked", true));
                    Set(look, "left", lj["left"]);
                    Set(look, "right", lj["right"]);
                    lf.SetValue(settings, look);
                }
                Set(settings, "eyelidType", c.GetString("eyelidType", "None"));
                var bones = Floats(c.Get("eyelidBones"));
                string[] lids = { "upperLeftEyelid", "upperRightEyelid", "lowerLeftEyelid", "lowerRightEyelid" };
                for (int k = 0; k < 4 && k < bones.Length; k++)
                    Set(settings, lids[k], (double)bones[k]);
                var lidMesh = TransformOf((long)c.GetNumber("eyelidsMesh", 0));
                Set(settings, "eyelidsSkinnedMesh", lidMesh != null ? lidMesh.GetComponent<SkinnedMeshRenderer>() : null);
                var shapes = Floats(c.Get("eyelidShapes"));
                if (shapes.Length >= 3)
                    Set(settings, "eyelidsBlendshapes", new[] { (int)shapes[0], (int)shapes[1], (int)shapes[2] });
                field.SetValue(desc, settings);
            }
            Set(desc, "customizeAnimationLayers", c.GetBool("customizeLayers"));
            SetLayers(desc, c.Get("layers") as List<object>);
            Set(desc, "customExpressions", c.GetBool("customExpressions"));
            Set(desc, "expressionsMenu", c.GetString("menu", ""));
            Set(desc, "expressionParameters", c.GetString("parameters", ""));
            return desc;
        }

        /* The playable layers: the 5 base and 3 special, each a struct of the SDK's CustomAnimLayer. */
        void SetLayers(Component desc, List<object> layers)
        {
            if (layers == null || layers.Count < 8)
                return;
            foreach (var pair in new[] { new { name = "baseAnimationLayers", from = 0, count = 5 }, new { name = "specialAnimationLayers", from = 5, count = 3 } })
            {
                var f = desc.GetType().GetField(pair.name, BindingFlags.Public | BindingFlags.Instance);
                var arr = f != null ? f.GetValue(desc) as Array : null;
                if (arr == null || arr.Length < pair.count)
                    continue;
                for (int k = 0; k < pair.count; k++)
                {
                    var lj = layers[pair.from + k] as Dictionary<string, object>;
                    object layer = arr.GetValue(k);
                    if (lj == null || layer == null)
                        continue;
                    Set(layer, "isEnabled", lj.GetBool("enabled"));
                    var tf = layer.GetType().GetField("type", BindingFlags.Public | BindingFlags.Instance);
                    if (tf != null && tf.FieldType.IsEnum)
                        tf.SetValue(layer, Enum.ToObject(tf.FieldType, (int)lj.GetNumber("type", 0)));
                    Set(layer, "animatorController", lj.GetString("controller", ""));
                    Set(layer, "mask", lj.GetString("mask", ""));
                    Set(layer, "isDefault", lj.GetBool("default", true));
                    arr.SetValue(layer, k);
                }
                f.SetValue(desc, arr);
            }
        }

        Component SetPhysBone(GameObject go, Dictionary<string, object> c, HashSet<Component> kept)
        {
            var pb = Nth(go, FindType(PhysBoneType), kept);
            if (pb == null)
                return null;
            SetAll(pb, c, "integrationType", "multiChildType", "pull", "spring", "stiffness", "gravity", "gravityFalloff", "immobileType", "immobile", "allowCollision",
                   "radius", "limitType", "maxAngleX", "maxAngleZ", "allowGrabbing", "allowPosing", "grabMovement", "maxStretch", "maxSquish", "stretchMotion",
                   "snapToHand", "isAnimated", "resetWhenDisabled", "parameter", "showGizmos");
            Set(pb, "rootTransform", c["root"]);
            Set(pb, "endpointPosition", c["endpoint"]);
            Set(pb, "limitRotation", c["limitRotation"]);
            SetTransformList(pb, "ignoreTransforms", c.Get("ignore"));
            SetComponentList(pb, "colliders", c.Get("colliders"), FindType(PhysBoneColliderType));
            ((Behaviour)pb).enabled = c.GetBool("enabled", true);
            return pb;
        }

        void SetTransformList(object target, string field, object ids)
        {
            var f = target.GetType().GetField(field, BindingFlags.Public | BindingFlags.Instance);
            if (f == null || !typeof(System.Collections.IList).IsAssignableFrom(f.FieldType))
                return;
            var list = f.GetValue(target) as System.Collections.IList ?? (System.Collections.IList)Activator.CreateInstance(f.FieldType);
            list.Clear();
            foreach (var v in Floats(ids))
            {
                var t = TransformOf((long)v);
                if (t != null)
                    list.Add(t);
            }
            f.SetValue(target, list);
        }

        void SetComponentList(object target, string field, object ids, Type type)
        {
            var f = target.GetType().GetField(field, BindingFlags.Public | BindingFlags.Instance);
            if (f == null || type == null || !typeof(System.Collections.IList).IsAssignableFrom(f.FieldType))
                return;
            var list = f.GetValue(target) as System.Collections.IList ?? (System.Collections.IList)Activator.CreateInstance(f.FieldType);
            list.Clear();
            foreach (var v in Floats(ids))
            {
                var t = TransformOf((long)v);
                var comp = t != null ? t.GetComponent(type) : null;
                if (comp != null)
                    list.Add(comp);
            }
            f.SetValue(target, list);
        }

        Component SetPhysBoneCollider(GameObject go, Dictionary<string, object> c, HashSet<Component> kept)
        {
            var pc = Nth(go, FindType(PhysBoneColliderType), kept);
            if (pc == null)
                return null;
            SetAll(pc, c, "shapeType", "insideBounds", "radius", "height", "bonesAsSpheres");
            Set(pc, "rootTransform", c["root"]);
            Set(pc, "position", c["position"]);
            Set(pc, "rotation", c["rotation"]);
            ((Behaviour)pc).enabled = c.GetBool("enabled", true);
            return pc;
        }

        Component SetContact(GameObject go, Dictionary<string, object> c, bool receiver, HashSet<Component> kept)
        {
            var ct = Nth(go, FindType(receiver ? ContactReceiverType : ContactSenderType), kept);
            if (ct == null)
                return null;
            SetAll(ct, c, "shapeType", "radius", "height", "allowSelf", "allowOthers", "localOnly", "receiverType", "parameter", "value", "minVelocity");
            Set(ct, "rootTransform", c["root"]);
            Set(ct, "position", c["position"]);
            Set(ct, "rotation", c["rotation"]);
            var tags = c.Get("tags") as List<object>;
            var tf = ct.GetType().GetField("collisionTags", BindingFlags.Public | BindingFlags.Instance);
            if (tags != null && tf != null)
            {
                var list = tf.GetValue(ct) as System.Collections.IList ?? (System.Collections.IList)Activator.CreateInstance(tf.FieldType);
                list.Clear();
                foreach (var t in tags)
                    if (t is string)
                        list.Add((string)t);
                tf.SetValue(ct, list);
            }
            ((Behaviour)ct).enabled = c.GetBool("enabled", true);
            return ct;
        }

        /* A VRC Constraint of the message's kind: its scalars; the sources through the SDK's keyable list (Clear and Add by
         * reflection, each a VRCConstraintSource with its transform, weight and the Parent offsets). */
        Component SetConstraint(GameObject go, Dictionary<string, object> c, HashSet<Component> kept)
        {
            string kind = c.GetString("constraint", "Parent");
            var type = FindType("VRC.SDK3.Dynamics.Constraint.Components.VRC" + kind + "Constraint");
            var vc = Nth(go, type, kept);
            if (vc == null)
                return null;
            Set(vc, "IsActive", c.GetBool("isActive", true));
            Set(vc, "GlobalWeight", c["globalWeight"]);
            Set(vc, "TargetTransform", c["target"]);
            Set(vc, "SolveInLocalSpace", c.GetBool("solveInLocalSpace"));
            Set(vc, "FreezeToWorld", c.GetBool("freezeToWorld"));
            Set(vc, "Locked", c.GetBool("locked"));
            Set(vc, "PositionOffset", c["positionOffset"]);
            Set(vc, "PositionAtRest", c["positionAtRest"]);
            Set(vc, "RotationOffset", c["rotationOffset"]);
            Set(vc, "RotationAtRest", c["rotationAtRest"]);
            Set(vc, "ScaleOffset", c["scaleOffset"]);
            Set(vc, "ScaleAtRest", c["scaleAtRest"]);
            var affects = c.Get("affects") as List<object>;
            var affectsRot = c.Get("affectsRotation") as List<object>;
            string part = kind == "Position" || kind == "Parent" ? "Position" : kind == "Rotation" ? "Rotation" : kind == "Scale" ? "Scale" : "";
            if (affects != null && affects.Count >= 3 && part.Length > 0)
                for (int k = 0; k < 3; k++)
                    Set(vc, "Affects" + part + "XYZ"[k], affects[k] is bool ? (bool)affects[k] : true);
            if (kind == "Parent" && affectsRot != null && affectsRot.Count >= 3)
                for (int k = 0; k < 3; k++)
                    Set(vc, "AffectsRotation" + "XYZ"[k], affectsRot[k] is bool ? (bool)affectsRot[k] : true);
            var sf = type.GetField("Sources", BindingFlags.Public | BindingFlags.Instance);
            var sources = c.Get("sources") as List<object>;
            object list = sf != null ? sf.GetValue(vc) : null;
            var srcType = FindSourceType();
            if (list != null && sources != null && srcType != null)
            {
                var clear = list.GetType().GetMethod("Clear", new Type[0]);
                var add = list.GetType().GetMethod("Add", new[] { srcType });
                if (clear != null && add != null)
                {
                    clear.Invoke(list, null);
                    foreach (var o in sources)
                    {
                        var sj = o as Dictionary<string, object>;
                        if (sj == null)
                            continue;
                        object src = Activator.CreateInstance(srcType);
                        Set(src, "SourceTransform", sj["transform"]);
                        Set(src, "Weight", sj["weight"]);
                        Set(src, "ParentPositionOffset", sj["positionOffset"]);
                        Set(src, "ParentRotationOffset", sj["rotationOffset"]);
                        add.Invoke(list, new[] { src });
                    }
                    sf.SetValue(vc, list);
                }
            }
            ((Behaviour)vc).enabled = c.GetBool("enabled", true);
            return vc;
        }

        static Type FindSourceType()
        {
            foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                var t = asm.GetType("VRC.Dynamics.VRCConstraintSource");
                if (t != null)
                    return t;
            }
            return null;
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
            AddPipelineManager(root.gameObject);  // (round 396: "There must be exactly one on the same game object as the scene descriptor")
        }

        /* VRChat's pipeline manager (its upload ID's holder) beside a descriptor, when the SDK has one and it isn't there. */
        static void AddPipelineManager(GameObject go)
        {
            var type = FindType(PipelineManagerType);
            if (type != null && go.GetComponent(type) == null)
                go.AddComponent(type);
        }
    }
}
