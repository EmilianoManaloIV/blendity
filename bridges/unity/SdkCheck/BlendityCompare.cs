// SPDX-License-Identifier: GPL-2.0-or-later
// Round 404 (task 0381): the Unity side of the snapshot comparison. Copied into a project copy's Assets/Editor by
// tools/unity_compare.py and run in batch mode:
//   Unity -batchmode -projectPath <copy> -executeMethod BlendityCompare.World -blendityScene <Assets/.../x.unity>
//         -blendityStations <stations.json> -blendityOut <folder> -quit
//   Unity -batchmode -projectPath <copy> -executeMethod BlendityCompare.Avatar -blendityPrefab <Assets/.../x.prefab>
//         -blendityStations <stations.json> -blendityOut <folder> -quit
// World: the scene opened, its Main Camera (its PostProcessLayer as the scene has it) moved to each station and rendered
// 1280 x 720 to <name>.png (sRGB, post-processed: what players see) and <name>.exr (linear, the layer off); Avatar: the
// prefab in the studio rig (one white sun at Euler(40, 200, 0), a flat ambient (0.45, 0.45, 0.5), a background
// (0.22, 0.22, 0.24), FOV 35) seen from each station, 900 x 1200. Both write scene_look.json (the values Unity holds) and
// timings.json (milliseconds to open and per render). Stations: {"stations":[{"name":"spawn","x":..,"y":..,"z":..,
// "yaw":..,"pitch":..,"fov":..}]} (fov 0: the camera's own).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class BlendityCompare
{
    [Serializable] public class Station { public string name; public float x, y, z, yaw, pitch, fov; }
    [Serializable] public class Stations { public Station[] stations; }

    static string Arg(string name)
    {
        var a = Environment.GetCommandLineArgs();
        for (int i = 0; i + 1 < a.Length; i++) if (a[i] == name) return a[i + 1];
        return "";
    }

    static string F(float v) { return v.ToString("R", CultureInfo.InvariantCulture); }

    public static void World() { Run(false); }
    public static void Avatar() { Run(true); }

    static void Run(bool avatar)
    {
        int code = 1;
        string outDir = Arg("-blendityOut");
        try
        {
            Directory.CreateDirectory(outDir);
            var stations = JsonUtility.FromJson<Stations>(File.ReadAllText(Arg("-blendityStations")));
            var timings = new List<string>();
            var sw = Stopwatch.StartNew();
            Camera cam;
            if (avatar)
            {
                var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(Arg("-blendityPrefab"));
                EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
                PrefabUtility.InstantiatePrefab(prefab);
                var sun = new GameObject("Sun").AddComponent<Light>();
                sun.type = LightType.Directional;
                sun.transform.rotation = Quaternion.Euler(40f, 200f, 0f);
                RenderSettings.ambientMode = UnityEngine.Rendering.AmbientMode.Flat;
                RenderSettings.ambientLight = new Color(0.45f, 0.45f, 0.5f);
                RenderSettings.fog = false;
                cam = new GameObject("Cam").AddComponent<Camera>();
                cam.clearFlags = CameraClearFlags.SolidColor;
                cam.backgroundColor = new Color(0.22f, 0.22f, 0.24f);
                cam.fieldOfView = 35f;
            }
            else
            {
                EditorSceneManager.OpenScene(Arg("-blendityScene"), OpenSceneMode.Single);
                cam = Camera.main ?? UnityEngine.Object.FindObjectsOfType<Camera>().FirstOrDefault(x => x.name == "Main Camera");
                if (cam == null) throw new Exception("no Main Camera in the scene");
            }
            timings.Add("\"open_ms\": " + sw.ElapsedMilliseconds);
            int w = avatar ? 900 : 1280, h = avatar ? 1200 : 720;
            var layer = cam.GetComponent("PostProcessLayer") as Behaviour;
            var renders = new List<string>();
            float ownFov = cam.fieldOfView;
            foreach (var st in stations.stations)
            {
                sw.Restart();
                cam.transform.position = new Vector3(st.x, st.y, st.z);
                cam.transform.rotation = Quaternion.Euler(st.pitch, st.yaw, 0f);
                cam.fieldOfView = st.fov > 0f ? st.fov : ownFov;  // (review 404: a station without a FOV takes the camera's own, not the last station's)
                Shot(cam, w, h, Path.Combine(outDir, st.name + ".png"), false);
                bool had = layer != null && layer.enabled;
                if (layer != null) layer.enabled = false;
                Shot(cam, w, h, Path.Combine(outDir, st.name + ".exr"), true);
                if (layer != null) layer.enabled = had;
                renders.Add("\"" + st.name + "\": " + sw.ElapsedMilliseconds);
            }
            File.WriteAllText(Path.Combine(outDir, "timings.json"), "{" + string.Join(", ", timings) + ", \"render_ms\": {" + string.Join(", ", renders) + "}}\n");
            var look = new List<string>();
            look.Add("\"fog\": " + (RenderSettings.fog ? "true" : "false"));
            look.Add("\"fogMode\": \"" + RenderSettings.fogMode + "\"");
            look.Add("\"fogDensity\": " + F(RenderSettings.fogDensity));
            look.Add("\"ambientMode\": \"" + RenderSettings.ambientMode + "\"");
            look.Add("\"ambientIntensity\": " + F(RenderSettings.ambientIntensity));
            look.Add("\"skybox\": \"" + (RenderSettings.skybox != null ? RenderSettings.skybox.name : "") + "\"");
            look.Add("\"cameraFov\": " + F(cam.fieldOfView));
            look.Add("\"cameraHdr\": " + (cam.allowHDR ? "true" : "false"));
            look.Add("\"postProcessing\": " + (layer != null ? "true" : "false"));
            look.Add("\"colorSpace\": \"" + QualitySettings.activeColorSpace + "\"");
            look.Add("\"lights\": " + UnityEngine.Object.FindObjectsOfType<Light>().Length);
            look.Add("\"reflectionProbes\": " + UnityEngine.Object.FindObjectsOfType<ReflectionProbe>().Length);
            look.Add("\"renderers\": " + UnityEngine.Object.FindObjectsOfType<Renderer>().Length);
            File.WriteAllText(Path.Combine(outDir, "scene_look.json"), "{" + string.Join(", ", look) + "}\n");
            code = 0;
        }
        catch (Exception e) { File.WriteAllText(Path.Combine(outDir, "error.txt"), e.ToString()); }
        EditorApplication.Exit(code);
    }

    static void Shot(Camera cam, int w, int h, string path, bool linear)
    {
        var rt = linear ? new RenderTexture(w, h, 24, RenderTextureFormat.ARGBFloat) : new RenderTexture(w, h, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.sRGB);
        cam.targetTexture = rt;
        cam.Render();
        RenderTexture.active = rt;
        if (linear)
        {
            var texf = new Texture2D(w, h, TextureFormat.RGBAFloat, false);
            texf.ReadPixels(new Rect(0, 0, w, h), 0, 0);
            texf.Apply();
            File.WriteAllBytes(path, texf.EncodeToEXR(Texture2D.EXRFlags.None));
            UnityEngine.Object.DestroyImmediate(texf);
        }
        else
        {
            var tex = new Texture2D(w, h, TextureFormat.RGB24, false);
            tex.ReadPixels(new Rect(0, 0, w, h), 0, 0);
            tex.Apply();
            File.WriteAllBytes(path, tex.EncodeToPNG());
            UnityEngine.Object.DestroyImmediate(tex);
        }
        RenderTexture.active = null;
        cam.targetTexture = null;
        rt.Release();
        UnityEngine.Object.DestroyImmediate(rt);
    }
}
