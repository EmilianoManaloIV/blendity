// SPDX-License-Identifier: GPL-2.0-or-later
// Unity's baked lighting in Blendity (round 400, task 0377, ADR 0098): `unity lightmaps` asks the installed Unity for a
// Unity scene's lightmaps (render/unity_lightmaps.h), the Lighting window's Use Unity's Lightmaps, and the result shown as
// the scene's baked lighting - as `unity modelids` (unity_model_ids_ui.cpp) asks it for a model's part numbers.
#include "editor.h"

#include "../core/core.h"
#include "../image/image.h"
#include "../render/unity_lightmaps.h"
#include "../scene/unity_scene.h"

#include <algorithm>
#include <filesystem>
#include <thread>

namespace bl {

namespace {

const char *const kLightmapScript = R"cs(using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/* Round 400: a scene's baked lighting as Unity holds it - each lightmapped renderer's lightmap and the rect it takes
 * there, its mesh's lightmap UVs, the lightmaps' files, the lights' bake modes and the baked light probes - for
 * Blendity, which can't read LightingData.asset (Unity's own binary). Written as <out>: see unity_lightmaps.h. */
public static class BlendityLightmaps {
    static string Arg(string name) {
        string[] a = Environment.GetCommandLineArgs();
        for (int i = 0; i + 1 < a.Length; i++) if (a[i] == name) return a[i + 1];
        return "";
    }

    /* Each level's name with its place among same-named siblings: "SideRail[0]/Lamp[3]". */
    static string PathOf(Transform t) {
        var parts = new List<string>();
        for (; t != null; t = t.parent) {
            int n = 0;
            Transform p = t.parent;
            if (p != null) {
                for (int k = 0; k < p.childCount && p.GetChild(k) != t; k++) n += p.GetChild(k).name == t.name ? 1 : 0;
            } else {
                foreach (GameObject r in t.gameObject.scene.GetRootGameObjects()) {
                    if (r.transform == t) break;
                    n += r.name == t.name ? 1 : 0;
                }
            }
            parts.Add(t.name + "[" + n + "]");
        }
        parts.Reverse();
        return string.Join("/", parts);
    }

    class Out {
        readonly MemoryStream ms = new MemoryStream();
        public readonly BinaryWriter w;
        public Out() { w = new BinaryWriter(ms); }
        public void Str(string s) { byte[] b = Encoding.UTF8.GetBytes(s ?? ""); w.Write(b.Length); w.Write(b); }
        public byte[] Done() {
            w.Flush();
            byte[] body = ms.ToArray();
            uint h = 2166136261;
            foreach (byte b in body) { h ^= b; h *= 16777619; }
            var all = new byte[body.Length + 4];
            Buffer.BlockCopy(body, 0, all, 0, body.Length);
            BitConverter.GetBytes(h).CopyTo(all, body.Length);
            return all;
        }
    }

    /* The texture at `path` re-imported plain and readable in `format` (Default and Standalone, where a creator's cap or
     * compression would win over the default); null when it isn't a texture. */
    /* Round 401: a baked reflection probe's cubemap (ReflectionProbe-N.exr) re-imported readable at full size, HDR. */
    static Cubemap ReadableCube(string path) {
        /* Only readable: its mips and specular convolution as Unity baked them (re-importing it flat and unconvolved had
         * given magenta faces), its format the probe's own (RGBAHalf on PC). */
        var importer = string.IsNullOrEmpty(path) ? null : AssetImporter.GetAtPath(path) as TextureImporter;
        if (importer == null) return null;
        if (!importer.isReadable) {
            importer.isReadable = true;
            importer.SaveAndReimport();
        }
        return AssetDatabase.LoadAssetAtPath<Cubemap>(path);
    }

    static Texture2D Readable(string path, TextureImporterFormat format) {
        var importer = string.IsNullOrEmpty(path) ? null : AssetImporter.GetAtPath(path) as TextureImporter;
        if (importer == null) return null;
        importer.textureType = TextureImporterType.Default;
        importer.sRGBTexture = false;
        importer.mipmapEnabled = false;
        importer.isReadable = true;
        importer.npotScale = TextureImporterNPOTScale.None;
        importer.textureCompression = TextureImporterCompression.Uncompressed;
        importer.maxTextureSize = 16384;
        TextureImporterPlatformSettings ps = importer.GetDefaultPlatformTextureSettings();
        ps.format = format;
        ps.maxTextureSize = 16384;
        ps.textureCompression = TextureImporterCompression.Uncompressed;
        importer.SetPlatformTextureSettings(ps);
        TextureImporterPlatformSettings pc = importer.GetPlatformTextureSettings("Standalone");
        pc.overridden = true;
        pc.format = format;
        pc.maxTextureSize = 16384;
        pc.textureCompression = TextureImporterCompression.Uncompressed;
        importer.SetPlatformTextureSettings(pc);
        importer.SaveAndReimport();
        return AssetDatabase.LoadAssetAtPath<Texture2D>(path);
    }

    /* A lightmap's texels as linear half floats, bottom row first (Unity's order): the scratch project's copy re-imported
     * as a plain readable RGBAHalf texture - no lightmap encoding, no compression, full size - so Blendity needs no EXR
     * reader (Unity writes PIZ). 0 x 0 when there's no image. */
    static void WritePixels(Out o, string path) {
        var tex = Readable(path, TextureImporterFormat.RGBAHalf);
        if (tex == null) { o.w.Write(0); o.w.Write(0); return; }
        Color[] px = tex.GetPixels();
        o.w.Write(tex.width);
        o.w.Write(tex.height);
        foreach (Color c in px) { o.w.Write(Mathf.FloatToHalf(c.r)); o.w.Write(Mathf.FloatToHalf(c.g)); o.w.Write(Mathf.FloatToHalf(c.b)); }
    }

    /* A directional lightmap's texels as bytes (RGBA, bottom row first): the dominant light's direction (rgb - 0.5) and how
     * directional it is (a), as Unity's shader decodes it. 0 x 0 when the scene's lightmaps aren't directional. */
    static void WriteDirections(Out o, string path) {
        var tex = Readable(path, TextureImporterFormat.RGBA32);
        if (tex == null) { o.w.Write(0); o.w.Write(0); return; }
        Color32[] px = tex.GetPixels32();
        o.w.Write(tex.width);
        o.w.Write(tex.height);
        foreach (Color32 c in px) { o.w.Write(c.r); o.w.Write(c.g); o.w.Write(c.b); o.w.Write(c.a); }
    }

    public static void Run() {
        int code = 1;
        string outPath = Arg("-blendityOut");
        try {
            /* Linear, as VRChat's projects are: a new project is Gamma, and Unity hands its HDR lightmaps over gamma-encoded
             * there (SideRail's came out 4.6 times too bright). */
            if (PlayerSettings.colorSpace != ColorSpace.Linear) PlayerSettings.colorSpace = ColorSpace.Linear;
            EditorSceneManager.OpenScene(Arg("-blendityScene"), OpenSceneMode.Single);
            var o = new Out();
            o.w.Write(Encoding.ASCII.GetBytes("BLULIGHT"));
            o.w.Write(2);  // version (round 401: 2 - the reflection probes' cubemaps after the light probes)
            o.w.Write(-1);  // (the lightmap encoding: not public in 2022.3 - the EXR itself is linear)
            o.w.Write((int)LightmapSettings.lightmapsMode);
            /* Which bake: the LightingData.asset's path and its bytes' FNV-1a 32 (Blendity refuses the file once Unity bakes again). */
            string data = Lightmapping.lightingDataAsset != null ? AssetDatabase.GetAssetPath(Lightmapping.lightingDataAsset) : "";
            uint dh = 2166136261;
            if (data.Length > 0 && File.Exists(data)) foreach (byte b in File.ReadAllBytes(data)) { dh ^= b; dh *= 16777619; }
            o.Str(data);
            o.w.Write(dh);
            LightmapData[] maps = LightmapSettings.lightmaps;
            var colors = new List<string>();
            var dirs = new List<string>();
            foreach (LightmapData m in maps) {
                colors.Add(m.lightmapColor != null ? AssetDatabase.GetAssetPath(m.lightmapColor) : "");
                dirs.Add(m.lightmapDir != null ? AssetDatabase.GetAssetPath(m.lightmapDir) : "");
            }
            o.w.Write(maps.Length);
            for (int i = 0; i < maps.Length; i++) {
                o.Str(colors[i]);
                o.Str(dirs[i]);
                WritePixels(o, colors[i]);
                WriteDirections(o, dirs[i]);
            }
            var meshes = new List<Mesh>();
            var meshIndex = new Dictionary<Mesh, int>();
            var renderers = new List<MeshRenderer>();
            foreach (GameObject root in EditorSceneManager.GetActiveScene().GetRootGameObjects())
                foreach (MeshRenderer r in root.GetComponentsInChildren<MeshRenderer>(true)) {
                    if (r.lightmapIndex < 0 || r.lightmapIndex >= maps.Length) continue;  // (none, or a lightmap the scene doesn't list)
                    MeshFilter f = r.GetComponent<MeshFilter>();
                    if (f == null || f.sharedMesh == null) continue;
                    if (!meshIndex.ContainsKey(f.sharedMesh)) { meshIndex[f.sharedMesh] = meshes.Count; meshes.Add(f.sharedMesh); }
                    renderers.Add(r);
                }
            o.w.Write(meshes.Count);
            foreach (Mesh mesh in meshes) {
                string guid; long fid;
                o.Str(AssetDatabase.TryGetGUIDAndLocalFileIdentifier(mesh, out guid, out fid) ? guid + ":" + fid : mesh.name);
                Vector3[] pos = mesh.vertices;
                var uv = new List<Vector2>();
                mesh.GetUVs(1, uv);
                int which = 1;
                if (uv.Count != pos.Length) { mesh.GetUVs(0, uv); which = 0; }
                o.w.Write(pos.Length);
                foreach (Vector3 p in pos) { o.w.Write(p.x); o.w.Write(p.y); o.w.Write(p.z); }
                o.w.Write(which);
                for (int i = 0; i < pos.Length; i++) { Vector2 t = i < uv.Count ? uv[i] : Vector2.zero; o.w.Write(t.x); o.w.Write(t.y); }
                int[] tris = mesh.triangles;
                o.w.Write(tris.Length);
                foreach (int k in tris) o.w.Write(k);
            }
            o.w.Write(renderers.Count);
            foreach (MeshRenderer r in renderers) {
                o.Str(PathOf(r.transform));
                GlobalObjectId g = GlobalObjectId.GetGlobalObjectIdSlow(r.gameObject);
                o.w.Write((long)g.targetObjectId);
                o.w.Write((long)g.targetPrefabId);
                o.w.Write(meshIndex[r.GetComponent<MeshFilter>().sharedMesh]);
                o.w.Write(r.lightmapIndex);
                Vector4 so = r.lightmapScaleOffset;
                o.w.Write(so.x); o.w.Write(so.y); o.w.Write(so.z); o.w.Write(so.w);
                Matrix4x4 m = r.transform.localToWorldMatrix;  // (Blendity matches triangles in world space)
                for (int k = 0; k < 16; k++) o.w.Write(m[k % 4, k / 4]);
            }
            var lights = new List<Light>();
            foreach (GameObject root in EditorSceneManager.GetActiveScene().GetRootGameObjects())
                lights.AddRange(root.GetComponentsInChildren<Light>(true));
            o.w.Write(lights.Count);
            foreach (Light l in lights) {
                o.Str(PathOf(l.transform));
                o.w.Write((int)l.type);
                o.w.Write((int)l.lightmapBakeType);
            }
            LightProbes probes = LightmapSettings.lightProbes;
            Vector3[] at = probes != null ? probes.positions : new Vector3[0];
            UnityEngine.Rendering.SphericalHarmonicsL2[] sh = probes != null ? probes.bakedProbes : new UnityEngine.Rendering.SphericalHarmonicsL2[0];
            int n = Math.Min(at.Length, sh.Length);
            o.w.Write(n);
            /* Each probe as Unity's shading reads it (SphericalHarmonicsL2.Evaluate: what ShadeSH9 gives) along +X -X +Y -Y
             * +Z -Z: Blendity's ambient cube, with no convention of Unity's to reproduce. */
            Vector3[] axes = { Vector3.right, Vector3.left, Vector3.up, Vector3.down, Vector3.forward, Vector3.back };
            var seen = new Color[6];
            for (int i = 0; i < n; i++) {
                o.w.Write(at[i].x); o.w.Write(at[i].y); o.w.Write(at[i].z);
                sh[i].Evaluate(axes, seen);
                foreach (Color c in seen) { o.w.Write(c.r); o.w.Write(c.g); o.w.Write(c.b); }
            }
            /* Round 401: each ReflectionProbe's baked cubemap - its path, box and settings, then six faces +X -X +Y -Y +Z -Z
             * of size x size half RGB, each face as Cubemap.GetPixels gives it (the top row first). A probe with no bake, or
             * one whose cubemap can't be read, writes size 0. */
            var rprobes = new List<ReflectionProbe>();
            foreach (GameObject root in EditorSceneManager.GetActiveScene().GetRootGameObjects())
                rprobes.AddRange(root.GetComponentsInChildren<ReflectionProbe>(true));
            o.w.Write(rprobes.Count);
            CubemapFace[] faces = { CubemapFace.PositiveX, CubemapFace.NegativeX, CubemapFace.PositiveY, CubemapFace.NegativeY, CubemapFace.PositiveZ, CubemapFace.NegativeZ };
            foreach (ReflectionProbe rp in rprobes) {
                o.Str(PathOf(rp.transform));
                Vector3 wp = rp.transform.position;
                o.w.Write(wp.x); o.w.Write(wp.y); o.w.Write(wp.z);
                o.w.Write(rp.size.x); o.w.Write(rp.size.y); o.w.Write(rp.size.z);
                o.w.Write(rp.center.x); o.w.Write(rp.center.y); o.w.Write(rp.center.z);
                o.w.Write(rp.boxProjection ? 1 : 0);
                o.w.Write(rp.intensity);
                o.w.Write(rp.importance);
                Cubemap cube = null;
                string cpath = rp.bakedTexture != null ? AssetDatabase.GetAssetPath(rp.bakedTexture) : "";
                if (!string.IsNullOrEmpty(cpath)) cube = ReadableCube(cpath);
                int size = cube != null ? cube.width : 0;
                o.w.Write(size);
                if (cube == null) continue;
                foreach (CubemapFace f in faces) {
                    Color[] px = cube.GetPixels(f, 0);
                    foreach (Color c in px) { o.w.Write(Mathf.FloatToHalf(c.r)); o.w.Write(Mathf.FloatToHalf(c.g)); o.w.Write(Mathf.FloatToHalf(c.b)); }
                }
            }
            File.WriteAllBytes(outPath, o.Done());
            code = 0;
        } catch (Exception e) {
            File.WriteAllText(outPath + ".error.txt", e.ToString());
        }
        EditorApplication.Exit(code);
    }
}
)cs";

constexpr int kLightingRunning = -1000000;

std::string lighting_cache_dir() {
  const char *env = std::getenv("BLENDITY_CACHE_DIR");
  return fs::join(env && *env ? std::string(env) : fs::join(fs::join(fs::home_dir(), ".blendity"), "cache"), "unity-lighting");
}

/* A package's files as its .unity-guids lists them: GUID, Unity's path, Blendity's (in the project). */
struct PackageFile {
  std::string guid, unity, path;
};

std::vector<PackageFile> package_files(const std::string &dir, const std::string &project_root) {
  std::vector<PackageFile> out;
  std::string text;
  if (!fs::read_file(fs::join(dir, ".unity-guids"), text)) return out;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
    if (t2 == std::string::npos) continue;
    out.push_back({line.substr(0, t1), line.substr(t1 + 1, t2 - t1 - 1), fs::join(project_root, line.substr(t2 + 1))});
  }
  return out;
}

/* The package folder that lists `scene` (its .unity-guids), walking up from it; "" when none does. */
std::string package_of(const std::string &scene, const std::string &project_root, std::string &unity_path) {
  const std::string want = fs::normalize(scene);
  for (std::string dir = fs::parent(scene); !dir.empty() && dir != fs::parent(dir); dir = fs::parent(dir)) {
    for (const PackageFile &f : package_files(dir, project_root))
      if (fs::normalize(f.path) == want) return unity_path = f.unity, dir;
  }
  return "";
}

/* What the scratch project needs to open the scene: everything but pictures, sound, film and scripts - except the scene's own
 * lighting folder (its lightmaps). */
bool needed(const PackageFile &f, const std::string &lighting_folder) {
  if (f.unity.rfind(lighting_folder, 0) == 0) return true;
  if (fs::extension(f.unity) == ".exr" || fs::filename(f.unity) == "LightingData.asset") return true;  // (lightmaps kept elsewhere: review 400)
  static const char *const kSkip[] = {".png", ".jpg", ".jpeg", ".tga", ".psd", ".tif", ".tiff", ".bmp", ".gif", ".hdr",
                                      ".wav", ".mp3", ".ogg", ".mp4", ".webm", ".mov",
                                      ".cs", ".asmdef", ".asmref", ".dll", ".rsp"};  // (scripts: without the package's SDK they don't compile, and Unity's batch mode stops - the lighting needs none)
  const std::string ext = fs::extension(f.unity);
  return std::none_of(std::begin(kSkip), std::end(kSkip), [&](const char *s) { return ext == s; });
}

}  // namespace

/* unity lightmaps [wait]: Unity's baked lighting for the Unity scene this one came from - Unity opens it in a scratch
 * project and writes `<scene>.unity-lighting` beside it; shown once it's back. `wait` blocks (a script's). */
void Editor::read_unity_lighting_cmd(bool wait) {
  if (unity_lighting_job_) return Log::warn("Unity's lightmaps: Unity is already reading %s", fs::filename(unity_lighting_job_->scene).c_str());
  if (wait && !headless_) return Log::warn("unity lightmaps wait: a script's (it holds the editor till Unity is done) - here type it without wait");
  if (unity_lighting_scene_.empty()) return refuse("Unity's lightmaps: this scene didn't come from a Unity scene (open or import a .unity first)", "", "");
  if (baking_ || playing_) return refuse(std::string("Unity's lightmaps: not while ") + (baking_ ? "Generate Lighting runs" : "playing"), "", "");  // (review 400)
  const std::string unity = unity_editor_program();
  if (unity.empty())
    return refuse("Unity's lightmaps: no Unity found (Unity Hub's, or BLENDITY_UNITY): Unity keeps a scene's lightmaps in its own "
                  "binary file - install Unity through Unity Hub and run this again", "", "");
  auto job = std::make_shared<UnityLightingJob>();
  job->scene = unity_lighting_scene_;
  job->out = resolve_asset_path(unity_lighting_scene_) + ".unity-lighting";  // (absolute: Unity resolves a relative path against the scratch project - e2e 407)
  const std::string package = package_of(job->scene, project_root_, job->unity_scene);
  if (package.empty()) return refuse("Unity's lightmaps: " + fs::filename(job->scene) + " isn't in an unpacked Unity package (its .unity-guids)", "", "");
  const std::string lighting_folder = job->unity_scene.substr(0, job->unity_scene.size() - fs::extension(job->unity_scene).size()) + "/";
  for (const PackageFile &f : package_files(package, project_root_)) {
    std::string meta;
    if (!needed(f, lighting_folder) || !fs::exists(f.path) || fs::is_dir(f.path) || !fs::read_file(fs::join(fs::join(package, ".unity-meta"), f.guid + ".meta"), meta)) continue;
    job->files.push_back({f.path, meta, f.unity});
  }
  job->project = fs::join(lighting_cache_dir(), strprintf("%016llx", (unsigned long long)fnv1a_str(package.c_str())));
  unity_lighting_job_ = job;
  if (wait) {
    job->run(unity);
    return poll_unity_lighting();
  }
  std::thread([job, unity] { job->run(unity); }).detach();
  const std::string what = "Unity is reading " + fs::filename(job->scene) + "'s baked lighting in the background (a few minutes the first time)";
  Log::info("%s", what.c_str());
  toast(LogLevel::Info, what);
}

/* On the worker: the scratch project (made once, kept in the cache), the package's files at Unity's paths with their
 * .meta files, the script; then Unity, which opens the scene and writes the file. */
void Editor::UnityLightingJob::run(const std::string &unity) {
  const std::string log = fs::join(project, "blendity-lightmaps.log");
  if (!fs::exists(fs::join(project, "ProjectSettings"))) {
    fs::make_dirs(fs::parent(project));
    sys::run_program({unity, "-batchmode", "-nographics", "-createProject", project, "-quit", "-logFile", log}, 1800);
  }
  std::error_code ec;  // (an earlier run's files: the package's own as they are now)
  std::filesystem::remove_all(std::filesystem::u8path(fs::join(project, "Assets")), ec);
  fs::make_dirs(fs::join(project, "Assets/Editor"));
  fs::write_file(fs::join(project, "Assets/Editor/BlendityLightmaps.cs"), kLightmapScript);
  for (const File &f : files) {
    const std::string to = fs::join(project, f.unity);
    fs::make_dirs(fs::parent(to));
    if (!fs::copy_file(f.from, to) || !fs::write_file(to + ".meta", f.meta)) return why = "couldn't copy " + fs::filename(f.from) + " into " + project, code.store(-1);
  }
  const std::string fresh = out + ".new";  // (review 400: the last good file stays till Unity has written a new one)
  fs::remove_file(fresh);
  fs::remove_file(fresh + ".error.txt");  // (an earlier run's exception isn't this run's reason)
  const int ran = sys::run_program({unity, "-batchmode", "-projectPath", project, "-executeMethod", "BlendityLightmaps.Run", "-blendityScene", unity_scene,
                                    "-blendityOut", fresh, "-quit", "-logFile", log}, 3600);
  ec.clear();
  if (ran == 0 && fs::exists(fresh)) std::filesystem::rename(std::filesystem::u8path(fresh), std::filesystem::u8path(out), ec);
  if (ec) why = "couldn't put " + fs::filename(out) + " in place: " + ec.message();
  code.store(ec ? -1 : ran);
}

void Editor::poll_unity_lighting() {
  if (!unity_lighting_job_ || unity_lighting_job_->code.load() == kLightingRunning) return;
  if (baking_ || playing_) return;  // (review 400: shown when Generate Lighting or Play is done, not in the middle of either)
  const std::shared_ptr<UnityLightingJob> job = std::move(unity_lighting_job_);
  if (job->code.load() != 0 || !fs::exists(job->out)) {
    std::string err;
    fs::read_file(job->out + ".new.error.txt", err);
    return refuse("Unity's lightmaps: Unity didn't write them (" + (job->why.empty() ? err.empty() ? strprintf("exit %d", job->code.load()) : err.substr(0, 200) : job->why) +
                  "): its log is " + fs::join(job->project, "blendity-lightmaps.log"), "", "");
  }
  if (job->scene != unity_lighting_scene_) return Log::info("Unity's lightmaps for %s are ready beside it: open it again to see them", fs::filename(job->scene).c_str());
  if (apply_unity_lighting(job->out)) scene_dirty_ = true;  // (the scene's lighting changed: saved with it)
}

/* What a scene's own baked lighting is reset to before Unity's goes in - not the Light Volumes' bakes (Unity's, read
 * with the scene) and not the folder it was saved to (review 400: load_lighting_data had cleared the one and kept the
 * other, so Unity's lightmaps were never saved where a bake already was). */
void Editor::lighting_reset_for(const char *why) {
  Log::info("Baked lighting cleared for %s", why);
  render_job_wait(why);  // (round 412: a render on a worker reads the lightmaps and probes about to go)
  if (preview_running_) preview_baker_.cancel();
  preview_running_ = false;
  preview_data_ = LightingData{};
  preview_key_ = preview_view_key_ = preview_scene_key_ = 0;
  lm_mesh_baked_.clear();
  lm_mesh_hash_.clear();
  probes_ = ProbeVolumeData{};
  probe_live_ready_ = false;
  probe_live_left_ = 0;
  probe_live_key_ = probe_layout_key_ = 0;
  lighting_gen_++;
  lighting_saved_dir_.clear();
}

/* The file's lighting as the scene's baked lighting: lightmap pages from Unity's images, each matched object's UVs,
 * the lights' modes, Unity's probes as the probe volume - all keyed to the scene as it is now, so it shows as baked
 * and an object changed later loses only its own. Realtime GI goes off (Unity's scenes for VRChat bake). */
bool Editor::apply_unity_lighting(const std::string &file) {
  const double started = now_seconds();
  std::string bytes, why;
  UnityLighting u;
  if (!fs::read_file(file, bytes) || !read_unity_lighting(bytes, u, why))
    return Log::warn("Unity's lightmaps: %s not read (%s)", fs::filename(file).c_str(), why.empty() ? "missing" : why.c_str()), false;
  std::string unity_scene;
  const std::string package = package_of(unity_lighting_scene_, project_root_, unity_scene);
  for (const PackageFile &f : package_files(package, project_root_)) {  // (which bake of Unity's: refused once Unity bakes again)
    std::string data;
    if (f.unity != u.lighting_data || !fs::read_file(f.path, data) || unity_lighting_hash(data) == u.lighting_data_hash) continue;
    refuse("Unity's lightmaps: " + fs::filename(file) + " was read from an earlier bake of Unity's - read them again", "unity lightmaps", "Use Unity's Lightmaps");
    return false;
  }
  LightingData ld;
  std::vector<bool> page_ok;
  for (const UnityLighting::Page &p : u.pages) {
    Lightmap page;
    page_ok.push_back(unity_lightmap_page(p, kUnityLightmapScale, page));
    if (!page_ok.back()) Log::warn("Unity's lightmaps: %s came without its texels - its objects left out", p.color.c_str()), page.width = page.height = 1, page.texels = {Vec3(0.0f)};
    ld.pages.push_back(std::move(page));
  }
  const GameObject *under = unity_lighting_under_ ? scene_->find(unity_lighting_under_) : nullptr;
  std::map<uint64_t, std::pair<int, std::vector<Vec2>>> uvs;
  const UnityLightingMatch m = unity_lightmap_uvs(*scene_, under, u, uvs);
  if (m.matched == 0) {  // (review 400: the scene's own bake stays when none of Unity's objects are here)
    refuse(strprintf("Unity's lightmaps: none of the %d lightmapped objects in %s are in the scene", m.renderers, fs::filename(file).c_str()), "", "");
    return false;
  }
  for (auto &[id, page_uv] : uvs) {
    GameObject *g = scene_->find(id);
    const Mesh *mesh = g ? g->evaluated_mesh(1) : nullptr;
    if (!mesh || !page_ok[(size_t)page_uv.first]) continue;
    LightingData::Entry e;
    e.hash = lightmap_hash_of(*g, *mesh), e.page = page_uv.first, e.tri_uv = std::move(page_uv.second);
    ld.entries[id] = std::move(e);
  }
  const Mat4 placement = unity_lighting_placement(under);
  const size_t directional = unity_lightmap_directions(*scene_, u, ld, placement);  // (Unity's Directional mode: by each surface's normal)
  std::map<std::string, int> bake_of;
  for (const UnityLighting::LightInfo &l : u.lights) bake_of[l.path] = l.bake;
  scene_->for_each([&](GameObject &g) {
    if (!g.get<Light>() || (under && !scene_->is_ancestor(under, &g))) return;  // (review 400: the scene's own lights aren't Unity's)
    const auto b = bake_of.find(unity_lighting_path(g, under));
    if (b != bake_of.end() && (b->second == 1 || b->second == 2)) ld.light_modes[g.id] = b->second;
  });
  lighting_reset_for("Unity's lightmaps");
  scene_->lighting.baked_gi = true, scene_->lighting.realtime_gi = false;
  lighting_data_ = std::move(ld);
  lighting_data_.scene_key = lightmap_scene_key();
  scene_->for_each([&](GameObject &g) {
    const Mesh *mesh = lighting_data_.entries.count(g.id) ? g.evaluated_mesh(1) : nullptr;
    if (mesh) lm_mesh_baked_[g.id] = mesh_hash_of(*mesh);
  });
  unity_probe_volume(u, probes_, placement);
  probes_.scene_key = probes_.empty() ? 0 : lighting_data_.scene_key;
  const int reflections = apply_unity_reflection_probes(u, under);  // (round 401)
  lighting_from_unity_ = true;
  Log::info("Unity's lightmaps: %zu page(s)%s; %d of %d lightmapped renderers found (%.1f%% of their triangles matched Unity's)%s%s; %zu probe(s)",
            lighting_data_.pages.size(), directional ? ", directional" : "", m.matched, m.renderers, m.triangles ? 100.0 * (double)m.triangles_matched / (double)m.triangles : 0.0,
            m.not_found ? strprintf(", %d not found by name", m.not_found).c_str() : "", m.not_same ? strprintf(", %d with another mesh", m.not_same).c_str() : "",
            u.probes.size());
  if (reflections || !u.reflection_probes.empty()) Log::info("Unity's reflection probes: %d of %zu with a bake read", reflections, u.reflection_probes.size());
  Log::info("Unity's lightmaps applied in %.0f ms", (now_seconds() - started) * 1000.0);
  return true;
}

/* Round 401: each of Unity's baked reflection probes onto the scene's Reflection Probe at its path, replacing a bake of
 * Blendity's there. Cubemap.GetPixels gives a face's top row first (unlike Texture2D's; checked against Unity's own bake
 * in review 401: the sky in row 0 of every side face), so the rows are Blendity's as they are. How many took one. */
int Editor::apply_unity_reflection_probes(const UnityLighting &u, const GameObject *under) {
  std::map<std::string, GameObject *> at;
  scene_->for_each([&](GameObject &g) {
    if (g.get<ReflectionProbeComponent>() && (!under || scene_->is_ancestor(under, &g))) at[unity_lighting_path(g, under)] = &g;
  });
  int took = 0;
  for (const UnityLighting::ReflectionProbe &p : u.reflection_probes) {
    const auto it = at.find(p.path);
    if (it == at.end() || p.size < 1) continue;
    ReflectionProbeBake b;
    b.size = p.size, b.from_unity = true;
    for (int f = 0; f < 6; f++) {
      b.faces[f].resize((size_t)p.size * p.size);
      for (int y = 0; y < p.size; y++)
        for (int x = 0; x < p.size; x++) {
          const uint16_t *h = &p.faces[f][((size_t)y * p.size + x) * 3];  // (Unity's top row first, as Blendity's)
          const Vec3 c(unity_half_to_float(h[0]), unity_half_to_float(h[1]), unity_half_to_float(h[2]));
          b.faces[f][(size_t)y * p.size + x] = std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) ? vmax(c, Vec3(0.0f)) : Vec3(0.0f);
        }
    }
    std::string why;
    if (!b.valid(why)) continue;
    reflection_probe_bakes_[it->second->id] = std::move(b);
    took++;
  }
  if (took) reflection_probe_bakes_changed();
  return took;
}

}  // namespace bl
