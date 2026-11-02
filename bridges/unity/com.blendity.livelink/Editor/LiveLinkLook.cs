// SPDX-License-Identifier: GPL-2.0-or-later
// Round 376: Blendity's world, look and the render camera's filters (docs/LIVELINK.md: world, render, filters).
// - The world: RenderSettings - the Gradient as Trilight ambient, the Sky as the procedural skybox, an HDRI (a PNG or
//   JPG) as the panoramic one, a plain colour as flat ambient; its strength scaling the ambient; the distance fog as
//   Unity's linear fog. Live Link's colours are linear, Unity's RenderSettings colours gamma: converted.
// - The look and the filters: a "Blendity Look" object keeps them (BlendityLook) along with the scene's own lighting
//   as it was before (its Restore Scene Lighting puts it back); in a URP project a global Volume on it carries the
//   exposure, tone mapping, Bloom, Vignette and Color Grading (shown by cameras with Post Processing on).
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEngine.Rendering;
#if BLENDITY_URP
using UnityEngine.Rendering.Universal;
#endif

namespace Blendity.LiveLink
{
    public sealed partial class LiveLinkApplier
    {
        public const string LookName = "Blendity Look";
        float exposureStops, gradeStops;
        string viewTransform = "Standard";
        List<object> effects = new List<object>();
        string skyKey = "";       // (the skybox's mode and HDRI: made again only when they change - review 376)
        Material skyMaterial;

        static Color Col(float[] c, float strength = 1f) { return (new Color(c[0], c[1], c[2]) * strength).gamma; }

        void OnWorld(Dictionary<string, object> j)
        {
            Look().RememberSceneLighting();
            string mode = j.GetString("mode", "gradient");
            float strength = (float)j.GetNumber("strength", 1);
            RenderSettings.ambientIntensity = strength;  // (the skybox's; Trilight and Flat take it in their colours)
            RenderSettings.skybox = SkyboxFor(mode, j);
            if (mode == "gradient")
            {
                RenderSettings.ambientMode = AmbientMode.Trilight;
                RenderSettings.ambientSkyColor = Col(j.GetFloats("sky", 0.45f, 0.52f, 0.62f), strength);
                RenderSettings.ambientEquatorColor = Col(j.GetFloats("equator", 0.32f, 0.34f, 0.36f), strength);
                RenderSettings.ambientGroundColor = Col(j.GetFloats("ground", 0.16f, 0.15f, 0.14f), strength);
            }
            else if (mode == "color")
            {
                RenderSettings.ambientMode = AmbientMode.Flat;
                RenderSettings.ambientLight = Col(j.GetFloats("color", 0.05f, 0.05f, 0.05f), strength);
            }
            else
                RenderSettings.ambientMode = AmbientMode.Skybox;
            var fog = j.GetDict("fog");
            RenderSettings.fog = fog != null && fog.GetBool("enabled");
            if (fog == null)
                return;
            RenderSettings.fogMode = FogMode.Linear;  // (Blendity's fog: from a distance to a distance)
            RenderSettings.fogStartDistance = (float)fog.GetNumber("start", 10);
            RenderSettings.fogEndDistance = (float)fog.GetNumber("end", 60);
            RenderSettings.fogColor = Col(fog.GetFloats("color", 0.6f, 0.65f, 0.75f));
        }

        Material SkyboxFor(string mode, Dictionary<string, object> j)
        {
            string path = j.GetString("hdri", "");
            string key = mode + "|" + (mode == "hdri" ? path : "");
            if (key != skyKey)
            {
                if (skyMaterial != null)
                    Object.DestroyImmediate(skyMaterial);
                skyMaterial = MakeSkybox(mode, path);
                skyKey = key;
            }
            if (skyMaterial == null)
                return mode == "sky" || mode == "hdri" ? RenderSettings.skybox : null;
            if (mode == "sky")
                skyMaterial.SetFloat("_AtmosphereThickness", Mathf.Clamp((float)j.GetNumber("turbidity", 3) / 3f, 0f, 5f));
            if (mode == "hdri")
                skyMaterial.SetFloat("_Rotation", (float)j.GetNumber("rotation", 0));
            return skyMaterial;
        }

        static Material MakeSkybox(string mode, string path)
        {
            if (mode == "sky")
            {
                var shader = Shader.Find("Skybox/Procedural");
                return shader != null ? new Material(shader) : null;
            }
            var pano = Shader.Find("Skybox/Panoramic");
            if (mode != "hdri" || pano == null || path == "" || !File.Exists(path))
                return null;
            var tex = new Texture2D(2, 2);
            if (!tex.LoadImage(File.ReadAllBytes(path)))  // (PNG or JPG; an .hdr or .exr isn't read here)
            {
                Object.DestroyImmediate(tex);
                return null;
            }
            var sky = new Material(pano);
            sky.SetTexture("_MainTex", tex);
            return sky;
        }

        BlendityLook Look()
        {
            var t = root.Find(LookName);
            var go = t != null ? t.gameObject : new GameObject(LookName);
            go.transform.SetParent(root, false);
            var look = go.GetComponent<BlendityLook>();
            return look ? look : go.AddComponent<BlendityLook>();
        }

        void OnRender(Dictionary<string, object> j)
        {
            exposureStops = (float)j.GetNumber("exposure", 0);
            viewTransform = j.GetString("view_transform", "Standard");
            var look = Look();
            look.Exposure = exposureStops;
            look.ViewTransform = viewTransform;
            ApplyVolume(look);
        }

        /* The render camera's stack (Live Link sends only that one; an empty one when it has none or is gone). */
        void OnFilters(Dictionary<string, object> j)
        {
            effects = j.GetList("effects") ?? new List<object>();
            var look = Look();
            look.Filters = Value(effects);
            var grade = Effect("Color Grading");
            gradeStops = grade != null ? (float)grade.GetNumber("Exposure", 0) : 0f;
            ApplyVolume(look);
        }

        Dictionary<string, object> Effect(string type)
        {
            foreach (var o in effects)
            {
                var e = o as Dictionary<string, object>;
                if (e == null || e.GetString("type", "") != type || !e.GetBool("enabled", true))
                    continue;
                return e.GetDict("fields") ?? new Dictionary<string, object>();
            }
            return null;
        }

#if BLENDITY_URP
        /* URP: a global Volume on the look's object with the exposure (the render's and a Color Grading's), the tone
         * mapper, Bloom, Vignette and the grading's contrast and saturation. */
        void ApplyVolume(BlendityLook look)
        {
            var volume = look.GetComponent<Volume>();
            if (!volume)
                volume = look.gameObject.AddComponent<Volume>();
            volume.isGlobal = true;
            if (volume.sharedProfile == null)
                volume.sharedProfile = ScriptableObject.CreateInstance<VolumeProfile>();
            var p = volume.sharedProfile;
            var color = Get<ColorAdjustments>(p);
            color.postExposure.Override(exposureStops + gradeStops);
            var grade = Effect("Color Grading");
            color.contrast.Override(grade != null ? 100f * (float)grade.GetNumber("Contrast", 0) : 0f);
            color.saturation.Override(grade != null ? 100f * (float)grade.GetNumber("Saturation", 0) : 0f);
            var tone = Get<Tonemapping>(p);
            tone.mode.Override(viewTransform.StartsWith("ACES") ? TonemappingMode.ACES : viewTransform == "Standard" ? TonemappingMode.None : TonemappingMode.Neutral);
            var bloomFields = Effect("Bloom");
            var bloom = Get<Bloom>(p);
            bloom.active = bloomFields != null;
            if (bloomFields != null)
            {
                bloom.intensity.Override((float)bloomFields.GetNumber("Intensity", 0.6));
                bloom.threshold.Override((float)bloomFields.GetNumber("Threshold", 1));
                bloom.scatter.Override((float)bloomFields.GetNumber("Scatter", 0.7));
            }
            var vigFields = Effect("Vignette");
            var vig = Get<Vignette>(p);
            vig.active = vigFields != null;
            if (vigFields != null)
            {
                vig.intensity.Override((float)vigFields.GetNumber("Intensity", 0.4));
                vig.smoothness.Override((float)vigFields.GetNumber("Smoothness", 0.5));
            }
        }

        static T Get<T>(VolumeProfile p) where T : VolumeComponent
        {
            T c;
            return p.TryGet(out c) ? c : p.Add<T>(true);
        }
#else
        void ApplyVolume(BlendityLook look) { }  // (the built-in pipeline: kept on BlendityLook only)
#endif
    }
}
