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
