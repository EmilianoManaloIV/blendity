// SPDX-License-Identifier: GPL-2.0-or-later
// Round 420 (task 0396): Unity's editor frames on the same scene Blendity measures - the Scene view, then the Game view,
// each asked to draw again as soon as it has drawn while the view's camera is nudged (so every frame draws the scene, not
// a held picture); the interval between two of the view's actual draws is recorded (the Scene view's repaint events,
// the main camera's renders) - an editor tick that didn't draw the view isn't a frame. Run by tools/unity_frames.py on a COPY of a project (never a VCC project the
// user works in), the editor opened with a window (not -batchmode: the views need the GPU):
//   Unity.exe -projectPath <copy> -executeMethod BlendityFrameTiming.Run -logFile <log>
//     -blendityAsset <Assets/...unity | Assets/...prefab> -blendityOut <out.json> [-blendityFrames 300]
// No editor preference is changed (Unity's EditorPrefs are the user's, shared by every project): the views are asked to
// draw again as soon as they have drawn, which keeps the loop busy without it.
#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class BlendityFrameTiming
{
    static string asset, outPath;
    static int frames = 300, warm = 30;
    static int phase;  // 0 Scene view, 1 Game view, 2 done
    static int tick;
    static readonly Stopwatch clock = new Stopwatch();
    static double last;
    static readonly List<double> sceneMs = new List<double>(), gameMs = new List<double>();
    static EditorWindow gameView;

    static string Arg(string name, string fallback)
    {
        string[] a = Environment.GetCommandLineArgs();
        for (int i = 0; i + 1 < a.Length; i++)
            if (a[i] == name) return a[i + 1];
        return fallback;
    }

    public static void Run()
    {
        asset = Arg("-blendityAsset", "");
        outPath = Arg("-blendityOut", "blendity_frames.json");
        if (!int.TryParse(Arg("-blendityFrames", "300"), out frames) || frames < 10)
        {
            Finish("-blendityFrames must be a number of at least 10");
            return;
        }
        Bounds bounds = new Bounds();
        bool any = false;
        try
        {
            if (asset.EndsWith(".unity", StringComparison.OrdinalIgnoreCase)) EditorSceneManager.OpenScene(asset, OpenSceneMode.Single);
            else if (asset.EndsWith(".prefab", StringComparison.OrdinalIgnoreCase))
            {
                EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects, NewSceneMode.Single);
                var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(asset);
                if (prefab == null)
                {
                    Finish("no prefab at " + asset);
                    return;
                }
                PrefabUtility.InstantiatePrefab(prefab);
            }
            else
            {
                Finish("not a .unity scene or a .prefab: " + asset);
                return;
            }
        }
        catch (Exception e)
        {
            Finish("could not open " + asset + ": " + e.Message);
            return;
        }
        foreach (Renderer r in UnityEngine.Object.FindObjectsOfType<Renderer>())
        {
            if (!any) bounds = r.bounds;
            else bounds.Encapsulate(r.bounds);
            any = true;
        }
        SceneView sv = SceneView.lastActiveSceneView;
        if (sv == null) sv = EditorWindow.GetWindow<SceneView>();
        sv.Focus();
        if (any) sv.Frame(bounds, true);  // (the whole scene in view, as bench_world.py's Scene view frames it)
        clock.Start();
        last = -1;
        SceneView.duringSceneGui += OnSceneGui;
        Camera.onPostRender += OnCameraRendered;
        EditorApplication.update += Tick;
    }

    /* A Scene view draw: the time since the last one. */
    static void OnSceneGui(SceneView sv)
    {
        if (phase != 0 || Event.current == null || Event.current.type != EventType.Repaint) return;
        double now = clock.Elapsed.TotalMilliseconds;
        if (last >= 0 && ++tick > warm) sceneMs.Add(now - last);
        last = now;
    }

    /* The main camera drawn for the Game view: the time since the last one. */
    static void OnCameraRendered(Camera cam)
    {
        if (phase != 1 || cam != Camera.main) return;
        double now = clock.Elapsed.TotalMilliseconds;
        if (last >= 0 && ++tick > warm) gameMs.Add(now - last);
        last = now;
    }

    static int ticks;

    /* Every editor tick: ask the view to draw again, the camera nudged. */
    static void Tick()
    {
        ticks++;
        if (phase == 0)
        {
            SceneView sv = SceneView.lastActiveSceneView;
            if (sv == null) { Finish("no Scene view"); return; }
            sv.rotation = Quaternion.Euler(20f, (ticks % 3600) * 0.01f, 0f);  // (a nudge: every frame draws the scene again)
            sv.Repaint();
            if (sceneMs.Count >= frames || ticks > frames * 50)
            {
                phase = 1;
                tick = 0;
                last = -1;
                ticks = 0;
                Type gv = typeof(EditorWindow).Assembly.GetType("UnityEditor.GameView");
                gameView = EditorWindow.GetWindow(gv);
                gameView.Focus();
            }
            return;
        }
        if (phase == 1)
        {
            Camera cam = Camera.main;
            if (cam != null) cam.transform.Rotate(0f, 0.01f, 0f);
            if (gameView != null) gameView.Repaint();
            if (cam == null) { Finish("no main camera (a camera tagged MainCamera) for the Game view"); return; }
            if (gameMs.Count >= frames || ticks > frames * 50) Finish(null);
        }
    }

    static string Stats(List<double> v)
    {
        if (v.Count == 0) return "{\"frames\":0}";
        var s = v.OrderBy(x => x).ToList();
        double mean = v.Average(), median = s[s.Count / 2], p95 = s[Math.Min(s.Count - 1, (int)(s.Count * 0.95))];
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        return "{\"frames\":" + v.Count + ",\"mean_ms\":" + mean.ToString("F2", inv) + ",\"median_ms\":" + median.ToString("F2", inv) +
               ",\"p95_ms\":" + p95.ToString("F2", inv) + ",\"worst_ms\":" + s[s.Count - 1].ToString("F2", inv) + "}";
    }

    static void Finish(string error)
    {
        EditorApplication.update -= Tick;
        SceneView.duringSceneGui -= OnSceneGui;
        Camera.onPostRender -= OnCameraRendered;
        string json = "{\"asset\":\"" + asset.Replace("\\", "/") + "\",\"unity\":\"" + Application.unityVersion + "\"," +
                      "\"error\":" + (error == null ? "null" : "\"" + error.Replace("\"", "'") + "\"") + "," +
                      "\"scene_view\":" + Stats(sceneMs) + ",\"game_view\":" + Stats(gameMs) + "}";
        File.WriteAllText(outPath, json);
        EditorApplication.Exit(error == null ? 0 : 1);
    }
}
#endif
