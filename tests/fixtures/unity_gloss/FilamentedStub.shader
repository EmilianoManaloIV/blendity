// SPDX-License-Identifier: GPL-2.0-or-later
// Round 484: a stand-in that declares property names only, so a test material can name a shader Blendity
// knows by its name. No shader code.
Shader "Silent/Filamented" {
  Properties {
    _Color ("Color", Color) = (1,1,1,1)
    _MainTex ("MainTex", 2D) = "white" {}
    _Cutoff ("Cutoff", Float) = 0
    _Glossiness ("Glossiness", Float) = 0
    _GlossMapScale ("GlossMapScale", Float) = 0
    _SmoothnessTextureChannel ("SmoothnessTextureChannel", Float) = 0
    _Metallic ("Metallic", Float) = 0
    _MetallicGlossMap ("MetallicGlossMap", 2D) = "white" {}
    _SpecularHighlights ("SpecularHighlights", Float) = 0
    _GlossyReflections ("GlossyReflections", Float) = 0
    _BumpScale ("BumpScale", Float) = 0
    _BumpMap ("BumpMap", 2D) = "white" {}
    _OcclusionStrength ("OcclusionStrength", Float) = 0
    _OcclusionMap ("OcclusionMap", 2D) = "white" {}
    _EmissionColor ("EmissionColor", Color) = (1,1,1,1)
    _EmissionMap ("EmissionMap", 2D) = "white" {}
    _ExposureOcclusion ("ExposureOcclusion", Float) = 0
    _LightmapSpecular ("LightmapSpecular", Float) = 0
    _LightmapSpecularMaxSmoothness ("LightmapSpecularMaxSmoothness", Float) = 0
    _Mode ("Mode", Float) = 0
    _Cull ("Cull", Float) = 0
  }
  SubShader { }
}
