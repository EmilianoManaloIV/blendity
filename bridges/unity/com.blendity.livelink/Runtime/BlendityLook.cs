// SPDX-License-Identifier: GPL-2.0-or-later
// Round 376: Blendity's look as Live Link last sent it - the render's exposure and view transform, and the render
// camera's filter stack - on the "Blendity Look" object. In a URP project the bridge also drives a global Volume from
// it; in the built-in pipeline (no post-processing stack of its own) it is kept here for your own effects to read.
// Live Link's world takes over the scene's RenderSettings; the scene's own are kept here first, and the component's
// Restore Scene Lighting (its menu) puts them back (review 376).
using UnityEngine;
using UnityEngine.Rendering;

namespace Blendity.LiveLink
{
    [DisallowMultipleComponent]
    public sealed class BlendityLook : MonoBehaviour
    {
        [Tooltip("The render's exposure in stops (Blendity's Render > Exposure).")]
        public float Exposure;
        [Tooltip("Blendity's view transform: Standard, Filmic, ACES, AgX or an OpenColorIO view's name.")]
        public string ViewTransform = "Standard";
        [Tooltip("The render camera's filter stack as Live Link sent it (JSON): Bloom, Vignette, Color Grading and the rest.")]
        [TextArea(3, 12)]
        public string Filters = "[]";

        [Header("The scene's own lighting, before Live Link's world")]
        public bool HasSceneLighting;
        public AmbientMode AmbientMode;
        public Color AmbientSky, AmbientEquator, AmbientGround, AmbientLight;
        public float AmbientIntensity;
        public Material Skybox;
        public bool Fog;
        public FogMode FogMode;
        public float FogStart, FogEnd, FogDensity;
        public Color FogColor;

        /* The scene's RenderSettings, once: before Live Link's world first changes them. */
        public void RememberSceneLighting()
        {
            if (HasSceneLighting)
                return;
            HasSceneLighting = true;
            AmbientMode = RenderSettings.ambientMode;
            AmbientSky = RenderSettings.ambientSkyColor;
            AmbientEquator = RenderSettings.ambientEquatorColor;
            AmbientGround = RenderSettings.ambientGroundColor;
            AmbientLight = RenderSettings.ambientLight;
            AmbientIntensity = RenderSettings.ambientIntensity;
            Skybox = RenderSettings.skybox;
            Fog = RenderSettings.fog;
            FogMode = RenderSettings.fogMode;
            FogStart = RenderSettings.fogStartDistance;
            FogEnd = RenderSettings.fogEndDistance;
            FogDensity = RenderSettings.fogDensity;
            FogColor = RenderSettings.fogColor;
        }

        [ContextMenu("Restore Scene Lighting")]
        public void RestoreSceneLighting()
        {
            if (!HasSceneLighting)
                return;
            RenderSettings.ambientMode = AmbientMode;
            RenderSettings.ambientSkyColor = AmbientSky;
            RenderSettings.ambientEquatorColor = AmbientEquator;
            RenderSettings.ambientGroundColor = AmbientGround;
            RenderSettings.ambientLight = AmbientLight;
            RenderSettings.ambientIntensity = AmbientIntensity;
            RenderSettings.skybox = Skybox;
            RenderSettings.fog = Fog;
            RenderSettings.fogMode = FogMode;
            RenderSettings.fogStartDistance = FogStart;
            RenderSettings.fogEndDistance = FogEnd;
            RenderSettings.fogDensity = FogDensity;
            RenderSettings.fogColor = FogColor;
            HasSceneLighting = false;
        }
    }
}
