// SPDX-License-Identifier: GPL-2.0-or-later
// Round 396 (task 0373): the Unity bridge against VRChat's real SDK. run_sdk_tests.py copies this into a copy of a VRChat
// Creator Companion project (never the project itself) and runs it in batch mode:
//
//   Unity -batchmode -projectPath <copy> -executeMethod BlendityVrcChecks.Avatar|World
//         -blendityFixture <tests/fixtures/livelink_stream.bin> -blendityOut <report.json>
//
// (no -quit: it exits itself, with 1 when the SDK lists an error or a check couldn't run)
//
// It replays Blendity's recorded Live Link session through the bridge (the SDK's own types, not the stand-ins), then asks
// the SDK what it thinks: for an avatar, the control panel's checks (its errors and warnings, as the Builder tab lists
// them) and its Performance Rank per category (AvatarPerformance.CalculatePerformanceStats, as the Builder tab shows it,
// PC and Android); for a world, the scene descriptor and the world builder's checks. The SDK's types are reached by
// name and reflection, as the bridge does, so this compiles in any project. Nothing is built for upload or uploaded.
using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Linq;
using System.Text;
using Blendity.LiveLink;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class BlendityVrcChecks
{
    const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;

    public static void Avatar() { Run(true); }
    public static void World() { Run(false); }

    static void Run(bool avatar)
    {
        var report = new Dictionary<string, object>();
        int code = 1;
        try
        {
            /* Round 398: a saved scene first - the package writes a bake beside it. */
            var scene = EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects, NewSceneMode.Single);
            EditorSceneManager.SaveScene(scene, "Assets/BlendityChecks" + (avatar ? "Avatar" : "World") + ".unity");
            applier = Replay(Arg("-blendityFixture"), report, !avatar);
            code = avatar ? CheckAvatar(report) : CheckWorld(report);
        }
        catch (Exception e)
        {
            report["exception"] = e.ToString();
        }
        report["passed"] = code == 0;
        File.WriteAllText(Arg("-blendityOut"), Json(report, 0) + "\n");
        EditorApplication.Exit(code);
    }

    static string Arg(string name)
    {
        var a = Environment.GetCommandLineArgs();
        int i = Array.IndexOf(a, name);
        return i >= 0 && i + 1 < a.Length ? a[i + 1] : "";
    }

    static Type FindType(string fullName)
    {
        foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
        {
            var t = asm.GetType(fullName);
            if (t != null)
                return t;
        }
        return null;
    }

    static LiveLinkApplier applier;

    /* The session replayed; in the world, the package's Manager has Denoise off before the bake lands, so the textures
     * the package writes are Blendity's data exactly (the round-trip fixture) - the bridge follows the Manager's setting,
     * as the package's own bake does. */
    static LiveLinkApplier Replay(string path, Dictionary<string, object> report, bool world)
    {
        var data = File.ReadAllBytes(path);
        var s = new LiveLinkStream();
        s.Feed(data, data.Length);
        var a = new LiveLinkApplier();
        int n = 0;
        for (var m = s.Next(); m != null; m = s.Next(), n++)
        {
            if (m.Type == "lightvolumebake")
            {
                sentBake = m;
                if (world)
                    DenoiseOff(report);
            }
            a.Apply(m);
        }
        report["messages"] = n;
        return a;
    }

    static LiveLinkMessage sentBake;

    static void DenoiseOff(Dictionary<string, object> report)
    {
        var managerType = FindType("VRCLightVolumes.LightVolumeManager");
        foreach (var m in managerType != null ? UnityEngine.Object.FindObjectsByType(managerType, FindObjectsInactive.Include, FindObjectsSortMode.None) : new UnityEngine.Object[0])
        {
            var f = managerType.GetField("Denoise", Any);
            if (f != null)
                f.SetValue(m, false);
        }
        report["denoise"] = false;
    }

    static object Member(object o, string name)
    {
        var f = o.GetType().GetField(name, Any);
        if (f != null)
            return f.GetValue(o);
        var p = o.GetType().GetProperty(name, Any);
        return p != null ? p.GetValue(o) : null;
    }

    // ------------------------------------------------------------ avatars

    static int CheckAvatar(Dictionary<string, object> report)
    {
        var descType = FindType(LiveLinkApplier.AvatarDescriptorType);
        var found = descType != null ? UnityEngine.Object.FindObjectsByType(descType, FindObjectsInactive.Include, FindObjectsSortMode.None) : new UnityEngine.Object[0];
        var desc = found.Length == 1 ? (Component)found[0] : null;  // (the session's one avatar, on whatever root Blendity named)
        report["descriptors"] = found.Length;
        report["descriptor"] = desc != null ? Descriptor(desc) : null;
        if (desc == null)
            return 1;
        var root = desc.gameObject;
        report["root"] = root.name;
        var pbType = FindType(LiveLinkApplier.PhysBoneType);
        report["physbones"] = pbType != null ? root.GetComponentsInChildren(pbType, true).Length : 0;
        var pc = Performance(root, false);
        report["pc"] = pc;
        report["android"] = Performance(root, true);
        var issues = BuilderIssues("VRC.SDK3A.Editor.VRCSdkControlPanelAvatarBuilder", "OnGUIAvatarCheck", desc);
        report["issues"] = issues;
        return Failed(issues) || !pc.ContainsKey("ratings") ? 1 : 0;  // (review 396: an SDK error fails the check, not only a crash)
    }

    static Dictionary<string, object> Descriptor(Component desc)
    {
        var d = new Dictionary<string, object>();
        var view = (Vector3)Member(desc, "ViewPosition");
        d["view"] = new List<object> { view.x, view.y, view.z };
        d["lipSync"] = Member(desc, "lipSync").ToString();
        var face = Member(desc, "VisemeSkinnedMesh") as SkinnedMeshRenderer;
        d["visemeMesh"] = face != null ? face.name : "";
        d["visemes"] = new List<object>((string[])Member(desc, "VisemeBlendShapes") ?? new string[0]);
        d["eyeLook"] = Member(desc, "enableEyeLook");
        return d;
    }

    /* The SDK's Performance Rank of the avatar: each category's value and rating, as the Builder tab computes them. */
    static Dictionary<string, object> Performance(GameObject avatar, bool mobile)
    {
        var statsType = FindType("VRC.SDKBase.Validation.Performance.Stats.AvatarPerformanceStats");
        var perfType = FindType("VRC.SDKBase.Validation.Performance.AvatarPerformance");
        var catType = FindType("VRC.SDKBase.Validation.Performance.AvatarPerformanceCategory");
        var out_ = new Dictionary<string, object>();
        if (statsType == null || perfType == null || catType == null)
            return out_;
        var stats = Activator.CreateInstance(statsType, new object[] { mobile });
        perfType.GetMethod("CalculatePerformanceStats", BindingFlags.Public | BindingFlags.Static)
            .Invoke(null, new object[] { avatar.name, avatar, stats, mobile });
        var values = new Dictionary<string, object>();
        foreach (var f in statsType.GetFields(BindingFlags.Public | BindingFlags.Instance))
            values[f.Name] = Plain(f.GetValue(stats));
        out_["values"] = values;
        var ratings = new Dictionary<string, object>();
        var rate = statsType.GetMethod("GetPerformanceRatingForCategory");
        foreach (var c in Enum.GetValues(catType))
        {
            if (c.ToString() == "AvatarPerformanceCategoryCount" || c.ToString() == "None")
                continue;
            ratings[c.ToString()] = rate.Invoke(stats, new[] { c }).ToString();
        }
        out_["ratings"] = ratings;
        return out_;
    }

    /* A value as JSON takes it: numbers, text, the fields of a nested stats struct (PhysBone's counts). */
    static object Plain(object v)
    {
        if (v == null || v is string || v is bool || v is int || v is float || v is double || v is long)
            return v;
        if (v.GetType().IsEnum)
            return v.ToString();
        if (v.GetType().IsPrimitive)
            return Convert.ToDouble(v, CultureInfo.InvariantCulture);
        var t = v.GetType();
        if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(Nullable<>))
            return Plain(t.GetProperty("Value").GetValue(v));
        if (v is Bounds)
            return ((Bounds)v).size.ToString("F3");
        var d = new Dictionary<string, object>();
        foreach (var f in t.GetFields(BindingFlags.Public | BindingFlags.Instance))
            d[f.Name] = Plain(f.GetValue(v));
        return d;
    }

    /* The control panel's checks for one avatar or scene, run without its window: a builder registered with a panel that
     * is never shown, its private check called, the panel's lists read. */
    static Dictionary<string, object> BuilderIssues(string builderName, string check, Component subject)
    {
        var result = new Dictionary<string, object>();
        var builderType = FindType(builderName);
        var panelType = FindType("VRCSdkControlPanel");
        if (builderType == null || panelType == null)
        {
            result["exception"] = "no " + (builderType == null ? builderName : "VRCSdkControlPanel");
            return result;
        }
        var panel = ScriptableObject.CreateInstance(panelType);
        try
        {
            var builder = Activator.CreateInstance(builderType);
            builderType.GetMethod("RegisterBuilder", Any).Invoke(builder, new object[] { panel });
            builderType.GetMethod(check, Any).Invoke(builder, new object[] { subject });
            result["errors"] = Issues(panel, "GUIErrors", subject);
            result["warnings"] = Issues(panel, "GUIWarnings", subject);
        }
        catch (Exception e)
        {
            result["exception"] = (e.InnerException ?? e).ToString();
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(panel);
        }
        return result;
    }

    /* The SDK's check failed: it listed an error, or couldn't be run or read (review 396: a renamed list had read as none). */
    static bool Failed(Dictionary<string, object> issues)
    {
        var errors = issues.ContainsKey("errors") ? issues["errors"] as List<object> : null;
        return issues.ContainsKey("exception") || errors == null || errors.Count > 0;
    }

    static List<object> Issues(object panel, string list, Component subject)
    {
        var found = new List<object>();
        var dict = Member(panel, list) as IDictionary;
        if (dict == null)
            throw new MissingMemberException("VRCSdkControlPanel", list);  // (the SDK changed: said, not read as no issues)
        foreach (DictionaryEntry e in dict)
            foreach (var issue in (IEnumerable)e.Value)
                found.Add(Member(issue, "issueText"));
        return found;
    }

    // ------------------------------------------------------------ worlds

    static int CheckWorld(Dictionary<string, object> report)
    {
        var sceneType = FindType(LiveLinkApplier.SceneDescriptorType);
        if (sceneType == null)
            return 1;
        var all = UnityEngine.Object.FindObjectsByType(sceneType, FindObjectsInactive.Include, FindObjectsSortMode.None);
        report["sceneDescriptors"] = all.Length;
        var box = GameObject.Find("Box");
        report["boxStatic"] = box != null && GameObjectUtility.GetStaticEditorFlags(box).HasFlag(StaticEditorFlags.ContributeGI);
        if (all.Length != 1)
            return 1;
        var desc = (Component)all[0];
        var spawns = Member(desc, "spawns") as Transform[];
        report["spawns"] = spawns != null ? spawns.Length : -1;
        if (!LightVolumes(report))
            return 1;
        var issues = BuilderIssues("VRC.SDK3.Editor.VRCSdkControlPanelWorldBuilder", "OnGUISceneCheck", desc);
        report["issues"] = issues;
        return Failed(issues) ? 1 : 0;
    }

    /* Round 397: with VRC Light Volumes in the project, the session's Light Volume and Point Light Volume as the package's
     * own components - their fields, and the scene's one Manager the package's setup made. False when the package is
     * there and they aren't. */
    static bool LightVolumes(Dictionary<string, object> report)
    {
        var volumeType = FindType(LiveLinkApplier.LightVolumeType);
        report["lightVolumesPackage"] = volumeType != null;
        if (volumeType == null)
            return true;
        var hall = GameObject.Find("Hall Volume");
        var lamp = GameObject.Find("Lamp");
        var v = hall != null ? hall.GetComponent(volumeType) : null;
        var pointType = FindType(LiveLinkApplier.PointLightVolumeType);
        var p = lamp != null && pointType != null ? lamp.GetComponent(pointType) : null;
        var managerType = FindType("VRCLightVolumes.LightVolumeManager");
        int managers = managerType != null ? UnityEngine.Object.FindObjectsByType(managerType, FindObjectsInactive.Include, FindObjectsSortMode.None).Length : -1;
        report["lightVolume"] = v != null ? new Dictionary<string, object> { { "Intensity", Plain(Member(v, "Intensity")) }, { "Bake", Plain(Member(v, "Bake")) },
                                                                          { "scale", hall.transform.lossyScale.ToString("F2") }, { "managed", Member(v, "LightVolumeManager") != null } } : null;
        report["pointLightVolume"] = p != null ? new Dictionary<string, object> { { "LightType", Plain(Member(p, "LightType")) }, { "Intensity", Plain(Member(p, "Intensity")) },
                                                                              { "managed", Member(p, "LightVolumeManager") != null } } : null;
        report["lightVolumeManagers"] = managers;
        if (v == null || p == null)
            return false;
        /* Review 397: the package's derived data made by its own routines - the volume's inverse world matrix takes its
         * centre to the origin, the light's position is its transform's (both stay at their defaults otherwise). */
        var inv = (Matrix4x4)Member(v, "InvWorldMatrix");
        float centre = inv.MultiplyPoint3x4(hall.transform.position).magnitude;
        float lightOff = Vector3.Distance((Vector3)Member(p, "Position"), lamp.transform.position);
        report["derived"] = new Dictionary<string, object> { { "volumeCentreOff", centre }, { "lightPositionOff", lightOff } };
        return managers == 1 && centre < 1e-3f && lightOff < 1e-3f && BakeLanded(v, report);
    }

    /* Round 398: Blendity's bake of the hall saved by the package as its own - three textures, Bake off, nothing refused -
     * and its first voxel's L0 what Blendity sent (half floats: to 1%). */
    static bool BakeLanded(Component v, Dictionary<string, object> report)
    {
        if (sentBake == null)
        {
            report["bake"] = "no bake in the session";
            return false;
        }
        var t0 = Member(v, "Texture0") as Texture3D;
        var bake = new Dictionary<string, object> { { "refused", applier.LastBakeRefusal }, { "landed", applier.BakesLanded.Count },
                                                   { "texture0", t0 != null ? AssetDatabase.GetAssetPath(t0) : "" }, { "Bake", Plain(Member(v, "Bake")) } };
        report["bake"] = bake;
        if (t0 == null || applier.BakesLanded.Count != 1 || (bool)Member(v, "Bake"))
            return false;
        float sent = BitConverter.ToSingle(sentBake.Bin, 0), got = t0.GetPixels()[0].r;
        bake["voxel0L0"] = new List<object> { sent, got };
        if (Mathf.Abs(sent - got) > 0.01f * Mathf.Max(Mathf.Abs(sent), 0.01f))
            return false;
        /* A second bake with a changed checksum is refused, the textures kept. */
        var crcBefore = applier.BakesLanded.Values.First();
        var tampered = (byte[])sentBake.Bin.Clone();
        tampered[0] ^= 0x40;
        bake["tamperedRefused"] = applier.CheckBakeForTest(sentBake.Json, tampered, v) != null;
        return (bool)bake["tamperedRefused"] && crcBefore != 0;
    }

    // ------------------------------------------------------------ JSON

    static string Json(object v, int depth)
    {
        if (v == null)
            return "null";
        if (v is string)
            return Quote((string)v);
        if (v is bool)
            return (bool)v ? "true" : "false";
        if (v is int || v is long)
            return Convert.ToString(v, CultureInfo.InvariantCulture);
        if (v is float || v is double)
            return Convert.ToDouble(v).ToString("R", CultureInfo.InvariantCulture);
        string pad = new string(' ', 2 * (depth + 1)), end = new string(' ', 2 * depth);
        var sb = new StringBuilder();
        if (v is IDictionary)
        {
            sb.Append("{");
            bool first = true;
            foreach (DictionaryEntry e in (IDictionary)v)
            {
                sb.Append(first ? "\n" : ",\n").Append(pad).Append(Quote(e.Key.ToString())).Append(": ").Append(Json(e.Value, depth + 1));
                first = false;
            }
            return sb.Append(first ? "}" : "\n" + end + "}").ToString();
        }
        sb.Append("[");
        bool any = false;
        foreach (var item in (IEnumerable)v)
        {
            sb.Append(any ? ", " : "").Append(Json(item, depth + 1));
            any = true;
        }
        return sb.Append("]").ToString();
    }

    static string Quote(string s)
    {
        var sb = new StringBuilder("\"");
        foreach (char c in s)
        {
            if (c == '"' || c == '\\')
                sb.Append('\\').Append(c);
            else if (c < ' ')
                sb.Append("\\u").Append(((int)c).ToString("x4"));
            else
                sb.Append(c);
        }
        return sb.Append('"').ToString();
    }
}
