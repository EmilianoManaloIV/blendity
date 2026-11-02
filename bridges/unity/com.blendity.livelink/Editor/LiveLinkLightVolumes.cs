// SPDX-License-Identifier: GPL-2.0-or-later
// Round 397 (task 0374): VRC Light Volumes (red.sim.lightvolumes 3, MIT) through Live Link. With the package in the
// project, a Blendity Light Volume becomes its LightVolumeInstance (the box is the object's transform, as the package's
// is) and a light with Light Volume on gets a PointLightVolumeInstance beside Unity's own; then the package's own scene
// setup registers them with the scene's Light Volume Manager, making one when there is none. The package's types and
// editor routines are reached by name and reflection, as VRChat's SDK is, so the bridge compiles - and makes nothing of
// the package's - without it. In a world project the package's components are UdonSharp behaviours: they are added
// through UdonSharp's own Undo helper and their fields copied to Udon, as the package does.
using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Blendity.LiveLink
{
    public sealed partial class LiveLinkApplier
    {
        public const string LightVolumeType = "VRCLightVolumes.LightVolumeInstance";
        public const string PointLightVolumeType = "VRCLightVolumes.PointLightVolumeInstance";
        const BindingFlags AnyStatic = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
        static readonly Dictionary<string, Type> anyTypes = new Dictionary<string, Type>();  // (non-component types by name: the package's editor classes)

        static Type FindAnyType(string fullName)
        {
            Type found;
            if (anyTypes.TryGetValue(fullName, out found))
                return found;
            foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
                found = found ?? asm.GetType(fullName);
            return anyTypes[fullName] = found;
        }

        /* The package's component on `go`: found, or added as the package adds it (UdonSharp's Undo helper in a world
         * project, Unity's otherwise); `added` says which. */
        static Component PackageComponent(GameObject go, Type type, out bool added)
        {
            var have = go.GetComponent(type);
            added = have == null;
            if (have != null)
                return have;
            var udonUndo = FindAnyType("UdonSharpEditor.UdonSharpUndo");
            var add = udonUndo != null ? udonUndo.GetMethod("AddComponent", AnyStatic, null, new[] { typeof(GameObject), typeof(Type) }, null) : null;
            if (add != null)
                return add.Invoke(null, new object[] { go, type }) as Component;
            return Undo.AddComponent(go, type);
        }

        /* The package's own steps after a change: a new component registered with the scene's Manager
         * (LightVolumeSceneSetup.OnboardHierarchy makes the Manager when there is none), the data the shaders read made
         * again, and the proxy's fields to its Udon behaviour. */
        static void Register(Component c, bool added)
        {
            var setup = FindAnyType("VRCLightVolumes.LightVolumeSceneSetup");
            var onboard = added && setup != null ? setup.GetMethod("OnboardHierarchy", AnyStatic) : null;
            if (onboard != null)
                onboard.Invoke(null, new object[] { c.transform.root.gameObject, null, true });
            Refresh(c);
            EditorUtility.SetDirty(c);
        }

        /* Review 397: what the package's editor updater does after its own edits - a volume's world matrix, blend and state
         * (LightVolumeTools.ApplyRuntimeState) and a light's position, direction and cone (EditorApplyAuthoringData), the
         * Manager told - since the link's edits aren't the Inspector's and publish no change event for it to hear. */
        static void Refresh(Component c)
        {
            var tools = FindAnyType("VRCLightVolumes.LightVolumeTools");
            var apply = c.GetType().FullName == LightVolumeType && tools != null ? tools.GetMethod("ApplyRuntimeState", AnyStatic) : null;
            if (apply != null)
                apply.Invoke(null, new object[] { c, true });
            var authoring = c.GetType().GetMethod("EditorApplyAuthoringData", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
            if (authoring != null)
                authoring.Invoke(c, new object[] { false, false, true });
            var backend = FindAnyType("VRCLightVolumes.LightVolumeManagerEditorBackend");
            var copy = backend != null ? backend.GetMethod("CopyProxyToUdon", AnyStatic) : null;
            if (copy != null)
                copy.Invoke(null, new object[] { c });
        }

        /* A Point Light Volume switched off where VRChat sees it too (re-check 397): the proxy, its backing Udon behaviour
         * (UdonSharp copies the proxy's state only at its own setup) and the Manager, through the package's refresh. */
        static void SwitchOff(Behaviour p)
        {
            try
            {
                Undo.RecordObject(p, "Blendity Live Link");
                p.enabled = false;
                var util = FindAnyType("UdonSharpEditor.UdonSharpEditorUtility");
                var backing = util != null ? util.GetMethod("GetBackingUdonBehaviour", AnyStatic) : null;
                var udon = backing != null ? backing.Invoke(null, new object[] { p }) as Behaviour : null;
                if (udon != null)
                {
                    Undo.RecordObject(udon, "Blendity Live Link");
                    udon.enabled = false;
                }
                Refresh(p);
            }
            catch (Exception e)
            {
                Debug.LogWarning("Blendity Live Link: VRC Point Light Volume not switched off (" + (e.InnerException ?? e).Message + ")");
            }
        }

        // ------------------------------------------------------------ Blendity's bake (round 398)

        /// <summary>The last bake refused and why (tests and the window read it); "" when the last one landed.</summary>
        public string LastBakeRefusal = "";
        /// <summary>The bakes landed, by Blendity's object id: their checksums.</summary>
        public readonly Dictionary<long, uint> BakesLanded = new Dictionary<long, uint>();

        /* A volume's bake from Blendity (its floats in Unity's axes and voxel order: L0, L1 red, green, blue, then the
         * validity), saved as the package saves its own custom bakes - LightVolumeBaker.SaveCustomProbesBaked packs, dilates
         * and writes the three textures - after it is checked: the grid the volume has here, its checksum, its size, every
         * value a number; the textures it replaces copied into a Backup folder beside them first. The volume's Bake is
         * turned off so the package's bake leaves it, and its baked rotation set as the package's bake sets it. Refused:
         * nothing is written, and the console says why. */
        void OnLightVolumeBake(Dictionary<string, object> j, byte[] bin)
        {
            GameObject go;
            var type = FindType(LightVolumeType);
            if (type == null)
            {
                LastBakeRefusal = "VRC Light Volumes isn't in this project";
                Debug.LogWarning("Blendity Live Link: a Light Volume bake came, and VRC Light Volumes isn't in this project - nothing written");
                return;
            }
            if (!Objects.TryGetValue(j.GetId("id"), out go) || go == null)
                return;  // (its object isn't here yet: it comes again at the next sync)
            var v = go.GetComponent(type);
            string why = v == null ? "no Light Volume on " + go.name : CheckBake(j, bin, v);
            if (why == null && string.IsNullOrEmpty(go.scene.path))
                why = "save the Unity scene first (the package writes its bake beside the scene)";
            if (why == null && Member(v, "LightVolumeManager") == null)
                why = "the volume has no Light Volume Manager yet (the package's setup makes one: save the scene and send again)";
            uint crc = (uint)(long)j.GetNumber("crc");
            if (why == null && LandedCrc(v) == crc)
            {
                BakesLanded[j.GetId("id")] = crc;  // (review 398: already there - a reconnect's resend writes nothing)
                LastBakeRefusal = "";
                return;
            }
            if (why != null)
            {
                LastBakeRefusal = why;
                Debug.LogWarning("Blendity Live Link: the Light Volume bake for " + go.name + " was refused: " + why);
                return;
            }
            try
            {
                SaveBake(go, v, j, bin);
            }
            catch (Exception e)
            {
                LastBakeRefusal = (e.InnerException ?? e).Message;
                Debug.LogWarning("Blendity Live Link: the Light Volume bake for " + go.name + " wasn't saved (" + LastBakeRefusal + ")");
            }
        }

        /// <summary>Tests: why a bake would be refused, or null.</summary>
        public string CheckBakeForTest(Dictionary<string, object> j, byte[] bin, Component v) { return CheckBake(j, bin, v); }

        /* Why a bake can't be taken, or null: the grid, the size, the checksum, the numbers. */
        static string CheckBake(Dictionary<string, object> j, byte[] bin, Component v)
        {
            var r = j.GetFloats("resolution", 0, 0, 0);
            int rx = (int)r[0], ry = (int)r[1], rz = (int)r[2];
            Refresh(v);  // (an adaptive grid made from the box as it is here)
            var have = (Vector3Int)v.GetType().GetField("Resolution").GetValue(v);
            if (have != new Vector3Int(rx, ry, rz))
                return "it was baked at " + rx + " x " + ry + " x " + rz + " voxels, the volume here has " + have.x + " x " + have.y + " x " + have.z + " (bake again in Blendity)";
            long n = (long)rx * ry * rz;
            if (bin == null || n < 1 || bin.LongLength != n * 13 * 4)
                return "its data isn't its grid's size";
            if (Crc32(bin) != (uint)(long)j.GetNumber("crc"))
                return "its data doesn't match its checksum";
            for (int i = 0; i < bin.Length; i += 4)
                if (float.IsNaN(BitConverter.ToSingle(bin, i)) || float.IsInfinity(BitConverter.ToSingle(bin, i)))
                    return "it holds a value that isn't a number";
            return null;
        }

        void SaveBake(GameObject go, Component v, Dictionary<string, object> j, byte[] bin)
        {
            var r = j.GetFloats("resolution", 0, 0, 0);
            int n = (int)r[0] * (int)r[1] * (int)r[2];
            var l0 = Vectors(bin, 0, n);
            var l1r = Vectors(bin, n * 12, n);
            var l1g = Vectors(bin, n * 24, n);
            var l1b = Vectors(bin, n * 36, n);
            var validity = new float[n];
            Buffer.BlockCopy(bin, n * 48, validity, 0, n * 4);
            BackUp(v);
            var baker = FindAnyType("VRCLightVolumes.LightVolumeBaker");
            var save = baker != null ? baker.GetMethod("SaveCustomProbesBaked", AnyStatic) : null;
            if (save == null)
                throw new MissingMethodException("VRCLightVolumes.LightVolumeBaker", "SaveCustomProbesBaked");
            /* The Manager's own settings, as the package's bake takes them (review 398): its dilation and its denoise. */
            var manager = Member(v, "LightVolumeManager");
            object dilate = Member(manager, "DilateInvalidProbes"), denoise = Member(manager, "Denoise");
            bool useValidity = !(dilate is bool) || (bool)dilate;
            if (!(bool)save.Invoke(null, new object[] { v, l0, l1r, l1g, l1b, useValidity ? validity : null, denoise is bool && (bool)denoise }))
                throw new InvalidOperationException("the package didn't save it (its console message says why)");
            SetMember(v, "Bake", false);
            /* The rotation the volume had when Blendity baked it (its L1 is in world axes), as the package's bake sets it. */
            SetMember(v, "InvBakedRotation", Quaternion.Inverse(Rotation(j.GetFloats("rotation", 0, 0, 0, 1))));
            Refresh(v);
            LastBakeRefusal = "";
            uint crc = (uint)(long)j.GetNumber("crc");
            BakesLanded[j.GetId("id")] = crc;
            RecordLanded(v, crc);
            Debug.Log("Blendity Live Link: " + go.name + "'s Light Volume baked in Blendity (" + r[0] + " x " + r[1] + " x " + r[2] + " voxels)");
        }

        static Vector3[] Vectors(byte[] bin, int at, int n)
        {
            var v = new Vector3[n];
            for (int i = 0; i < n; i++, at += 12)
                v[i] = new Vector3(BitConverter.ToSingle(bin, at), BitConverter.ToSingle(bin, at + 4), BitConverter.ToSingle(bin, at + 8));
            return v;
        }

        static object Member(object o, string name)
        {
            var f = o != null ? o.GetType().GetField(name, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance) : null;
            return f != null ? f.GetValue(o) : null;
        }

        /* Which of Blendity's bakes a volume's textures hold: its checksum, kept in the project's Library (never in
         * Assets) by Texture0's GUID; 0 when they aren't one of Blendity's (Unity's own bake, or none). */
        static string LandedPath(Component v)
        {
            var tex = Member(v, "Texture0") as UnityEngine.Object;
            string guid = tex != null ? AssetDatabase.AssetPathToGUID(AssetDatabase.GetAssetPath(tex)) : "";
            return string.IsNullOrEmpty(guid) ? "" : System.IO.Path.Combine("Library", "BlendityLightVolumes", guid + ".crc");
        }

        /* The textures as they are on disk: their sizes and times (re-check 398: Unity's own bake writes the same files under
         * the same GUID - a record of Blendity's bake only counts while the textures are the ones it wrote). */
        static string Fingerprint(Component v)
        {
            var parts = new List<string>();
            for (int k = 0; k < 3; k++)
            {
                var tex = Member(v, "Texture" + k) as UnityEngine.Object;
                string file = tex != null ? AssetDatabase.GetAssetPath(tex) : "";
                var info = file.Length > 0 ? new System.IO.FileInfo(file) : null;
                parts.Add(info != null && info.Exists ? info.Length + "@" + info.LastWriteTimeUtc.Ticks : "-");
            }
            return string.Join(",", parts);
        }

        static uint LandedCrc(Component v)
        {
            string path = LandedPath(v);
            if (path.Length == 0 || !System.IO.File.Exists(path))
                return 0;
            var lines = System.IO.File.ReadAllLines(path);
            uint crc;
            return lines.Length == 2 && lines[1] == Fingerprint(v) && uint.TryParse(lines[0].Trim(), out crc) ? crc : 0;
        }

        static void RecordLanded(Component v, uint crc)
        {
            string path = LandedPath(v);
            if (path.Length == 0)
                return;
            System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path));
            System.IO.File.WriteAllLines(path, new[] { crc.ToString(), Fingerprint(v) });
        }

        /* The volume's current textures - Unity's own bake - copied to <their folder>/Backup before they are replaced, never
         * deleted; an earlier bake of Blendity's isn't (review 398: every resend had added three copies). */
        static void BackUp(Component v)
        {
            if (LandedCrc(v) != 0)
                return;
            string stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");
            for (int k = 0; k < 3; k++)
            {
                var tex = v.GetType().GetField("Texture" + k).GetValue(v) as UnityEngine.Object;
                string path = tex != null ? AssetDatabase.GetAssetPath(tex) : "";
                if (string.IsNullOrEmpty(path))
                    continue;
                string dir = System.IO.Path.GetDirectoryName(path).Replace('\\', '/');
                if (!AssetDatabase.IsValidFolder(dir + "/Backup"))
                    AssetDatabase.CreateFolder(dir, "Backup");
                AssetDatabase.CopyAsset(path, dir + "/Backup/" + System.IO.Path.GetFileNameWithoutExtension(path) + "_" + stamp + System.IO.Path.GetExtension(path));
            }
        }

        static uint[] crcTable;
        /* CRC-32 (IEEE, as Blendity's crc32 and zip's). */
        public static uint Crc32(byte[] data)
        {
            if (crcTable == null)
            {
                crcTable = new uint[256];
                for (uint i = 0; i < 256; i++)
                {
                    uint c = i;
                    for (int b = 0; b < 8; b++)
                        c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    crcTable[i] = c;
                }
            }
            uint crc = 0xFFFFFFFFu;
            foreach (byte x in data)
                crc = crcTable[(crc ^ x) & 0xFF] ^ (crc >> 8);
            return crc ^ 0xFFFFFFFFu;
        }

        /* A linked object moved: its volume's or light's package data with it. */
        static void RefreshMoved(GameObject go)
        {
            foreach (var name in new[] { LightVolumeType, PointLightVolumeType })
            {
                var type = FindType(name);
                var c = type != null ? go.GetComponent(type) : null;
                if (c == null)
                    continue;
                try { Refresh(c); }
                catch (Exception e) { Debug.LogWarning("Blendity Live Link: VRC Light Volumes not updated (" + (e.InnerException ?? e).Message + ")"); }
            }
        }

        void OnLightVolume(Dictionary<string, object> j)
        {
            GameObject go;
            var type = FindType(LightVolumeType);
            if (type == null || !Objects.TryGetValue(j.GetId("id"), out go) || go == null || j.GetBool("none"))
                return;  // (taken off in Blendity: the volume stays - it may hold Unity's bake; the user removes it)
            try
            {
                bool added;
                var v = PackageComponent(go, type, out added);
                if (v == null)
                    return;
                SetMember(v, "IsDynamic", j.GetBool("dynamic"));
                SetMember(v, "IsAdditive", j.GetBool("additive"));
                var c = j.GetFloats("color", 1, 1, 1);
                SetMember(v, "Color", new Color(c[0], c[1], c[2]).gamma);  // (Live Link's colours are linear)
                SetMember(v, "Intensity", (float)j.GetNumber("intensity", 1));
                SetMember(v, "SmoothBlending", (float)j.GetNumber("smooth_blending", 0.25));
                SetMember(v, "Exposure", (float)j.GetNumber("exposure", 0));
                SetMember(v, "Shadows", (float)j.GetNumber("shadows", 0));
                SetMember(v, "Highlights", (float)j.GetNumber("highlights", 0));
                SetMember(v, "Bake", j.GetBool("bake", true));  // (off when Blendity bakes it: the package's bake leaves it)
                SetMember(v, "AdaptiveResolution", j.GetBool("adaptive", true));
                SetMember(v, "VoxelsPerUnit", (float)j.GetNumber("voxels_per_unit", 3));
                var r = j.GetFloats("resolution", 16, 16, 16);
                SetMember(v, "Resolution", new Vector3Int(Mathf.Max(1, (int)r[0]), Mathf.Max(1, (int)r[1]), Mathf.Max(1, (int)r[2])));
                Register(v, added);
            }
            catch (Exception e)
            {
                Debug.LogWarning("Blendity Live Link: VRC Light Volume not set up (" + (e.InnerException ?? e).Message + ")");
            }
        }

        /* A light with Light Volume on: the package's Point Light Volume beside Unity's light - point, spot or area, its
         * colour, a brightness at Unity's Intensity 1 being the package's default 100, a spot's half angle in radians (an
         * area's size is its object's scale there: the package takes it from the transform). Turned off - or the light
         * directional - the link's one is switched off, kept (review 397: it had gone on lighting the world). */
        void OnLightVolumeLight(GameObject go, Dictionary<string, object> j, string type)
        {
            var fields = j.GetDict("fields");
            var plType = FindType(PointLightVolumeType);
            if (plType == null || fields == null)
                return;
            bool on = fields.GetBool("Light Volume") && type != "directional";
            var have = go.GetComponent(plType) as Behaviour;
            if (!on)
            {
                if (have != null && have.enabled)
                    SwitchOff(have);
                return;
            }
            try
            {
                bool added;
                var p = PackageComponent(go, plType, out added);
                if (p == null)
                    return;
                var behaviour = p as Behaviour;
                if (behaviour != null)
                    behaviour.enabled = true;
                SetMember(p, "LightType", type == "spot" ? 1 : type == "area" ? 2 : 0);
                var c = j.GetFloats("color", 1, 1, 1);
                SetMember(p, "Color", new Color(c[0], c[1], c[2]).gamma);
                SetMember(p, "Intensity", (float)j.GetNumber("intensity", 1) * 100f);
                SetMember(p, "Angle", (float)j.GetNumber("spot_angle", 30) * 0.5f * Mathf.Deg2Rad);
                Register(p, added);
            }
            catch (Exception e)
            {
                Debug.LogWarning("Blendity Live Link: VRC Point Light Volume not set up (" + (e.InnerException ?? e).Message + ")");
            }
        }
    }
}
