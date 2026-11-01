// SPDX-License-Identifier: GPL-2.0-or-later
// Round 379: Live Link's clips (docs/LIVELINK.md, `clip`) as AnimationClip assets in Assets/Blendity Live Link/Clips -
// localPosition, localRotation and localScale curves on each object's path below the animated root, a key a frame,
// mirrored into Unity's axes as a transform is.
using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace Blendity.LiveLink
{
    public sealed partial class LiveLinkApplier
    {
        public const string ClipFolder = "Assets/Blendity Live Link/Clips";
        const int SampleFloats = 10;  // (a sample: position 3, rotation 4, scale 3)
        public readonly Dictionary<long, AnimationClip> Clips = new Dictionary<long, AnimationClip>();  // (by the animated root's id)

        static readonly string[] TrsProps =
        {
            "m_LocalPosition.x", "m_LocalPosition.y", "m_LocalPosition.z",
            "m_LocalRotation.x", "m_LocalRotation.y", "m_LocalRotation.z", "m_LocalRotation.w",
            "m_LocalScale.x", "m_LocalScale.y", "m_LocalScale.z"
        };

        void OnClip(Dictionary<string, object> j, byte[] bin)
        {
            long id = j.GetId("id");
            GameObject rootGo;
            var tracks = j.GetList("tracks");
            int frames = (int)j.GetNumber("frames");
            float fps = (float)j.GetNumber("fps", 60);
            if (!Objects.TryGetValue(id, out rootGo) || rootGo == null || tracks == null || frames <= 0 || fps <= 0)
                return;
            if (bin == null || bin.Length < tracks.Count * frames * SampleFloats * 4)
                return;
            var clip = ClipAsset(j.GetString("asset"), j.GetString("name", "Clip"));
            clip.ClearCurves();
            clip.frameRate = fps;
            var f = new float[tracks.Count * frames * SampleFloats];
            Buffer.BlockCopy(bin, 0, f, 0, f.Length * 4);
            var bindings = new List<EditorCurveBinding>();
            var curves = new List<AnimationCurve>();
            for (int k = 0; k < tracks.Count; k++)
                AddCurves(rootGo.transform, System.Convert.ToInt64(tracks[k]), f, k * frames * SampleFloats, frames, fps, bindings, curves);
            AnimationUtility.SetEditorCurves(clip, bindings.ToArray(), curves.ToArray());
            var settings = AnimationUtility.GetAnimationClipSettings(clip);
            settings.loopTime = j.GetBool("loop");
            AnimationUtility.SetAnimationClipSettings(clip, settings);
            EditorUtility.SetDirty(clip);
            Clips[id] = clip;
        }

        /* One object's ten curves: a key a frame, straight lines between them as Blendity's bake samples (review 379:
         * Unity's default flat tangents eased to a stop at every key), each rotation turned the short way from the last. */
        void AddCurves(Transform rootT, long id, float[] f, int at, int frames, float fps, List<EditorCurveBinding> bindings, List<AnimationCurve> curves)
        {
            GameObject go;
            if (!Objects.TryGetValue(id, out go) || go == null || !go.transform.IsChildOf(rootT))
                return;
            string path = AnimationUtility.CalculateTransformPath(go.transform, rootT);
            var v = new float[SampleFloats][];
            for (int c = 0; c < SampleFloats; c++)
                v[c] = new float[frames];
            var last = Quaternion.identity;
            for (int i = 0; i < frames; i++)
            {
                int o = at + i * SampleFloats;
                var p = Position(new[] { f[o], f[o + 1], f[o + 2] });
                var q = Rotation(new[] { f[o + 3], f[o + 4], f[o + 5], f[o + 6] });
                var s = Scale(new[] { f[o + 7], f[o + 8], f[o + 9] });
                if (i > 0 && Quaternion.Dot(q, last) < 0f)
                    q = new Quaternion(-q.x, -q.y, -q.z, -q.w);
                last = q;
                float[] row = { p.x, p.y, p.z, q.x, q.y, q.z, q.w, s.x, s.y, s.z };
                for (int c = 0; c < SampleFloats; c++)
                    v[c][i] = row[c];
            }
            for (int c = 0; c < SampleFloats; c++)
            {
                bindings.Add(EditorCurveBinding.FloatCurve(path, typeof(Transform), TrsProps[c]));
                curves.Add(LinearCurve(v[c], fps));
            }
        }

        static AnimationCurve LinearCurve(float[] v, float fps)
        {
            var keys = new Keyframe[v.Length];
            for (int i = 0; i < v.Length; i++)
            {
                float left = i > 0 ? (v[i] - v[i - 1]) * fps : 0f, right = i + 1 < v.Length ? (v[i + 1] - v[i]) * fps : 0f;
                keys[i] = new Keyframe(i / fps, v[i], i > 0 ? left : right, i + 1 < v.Length ? right : left);
            }
            return new AnimationCurve(keys);
        }

        /* The clip's asset: one per Blendity clip (named for its .anim), found again on disk - a reconnect, a domain
         * reload or a switch back to an earlier clip reuses it (review 379: each had made "Wave 1", "Wave 2"...). */
        static AnimationClip ClipAsset(string asset, string name)
        {
            string stem = string.IsNullOrEmpty(asset) ? name : Path.GetFileNameWithoutExtension(asset);
            string path = ClipFolder + "/" + SafeName(stem) + ".anim";
            var clip = AssetDatabase.LoadAssetAtPath<AnimationClip>(path);
            if (clip != null)
                return clip;
            Directory.CreateDirectory(ClipFolder);
            clip = new AnimationClip();
            AssetDatabase.CreateAsset(clip, path);
            return clip;
        }

        static string SafeName(string name)
        {
            foreach (char c in Path.GetInvalidFileNameChars())
                name = name.Replace(c, '_');
            return string.IsNullOrEmpty(name) ? "Clip" : name;
        }
    }
}
