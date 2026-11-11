// SPDX-License-Identifier: GPL-2.0-or-later
// Round 484: a stand-in that declares property names only, so a test material can name a shader Blendity
// knows by its name. No shader code.
Shader "FX/MirrorReflection" {
  Properties {
    _MainTex ("MainTex", 2D) = "white" {}
  }
  SubShader { }
}
