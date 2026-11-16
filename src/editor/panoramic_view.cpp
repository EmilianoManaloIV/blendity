// SPDX-License-Identifier: GPL-2.0-or-later
// A panoramic camera's picture in the rasterized views (round 538, task 0518): the cube faces its projection needs are
// drawn through render_deferred - so the CPU rasterizer and the Vulkan path alike - into one reused square target and
// resampled into the view (src/render/panoramic.h), the way EEVEE 5.3 does it.
#include "editor.h"
#include "panoramic_view.h"

#include <algorithm>
#include <cmath>

namespace bl {

PanoramicParams panoramic_params_for(const Camera &cam, int w, int h) {
  PanoramicParams p;
  p.type = cam.panoramic_type;
  p.lat_min = cam.latitude_min * kDeg2Rad, p.lat_max = cam.latitude_max * kDeg2Rad;
  p.lon_min = cam.longitude_min * kDeg2Rad, p.lon_max = cam.longitude_max * kDeg2Rad;
  p.fisheye_fov = cam.fisheye_fov * kDeg2Rad;
  p.fisheye_lens = cam.fisheye_lens;
  p.cyl_v_min = cam.cylinder_min, p.cyl_v_max = cam.cylinder_max;
  /* The sensor as Blender's EEVEE shapes it for the output: Auto fits the width to the larger side. */
  const bool horizontal = cam.sensor_fit == 1 || (cam.sensor_fit == 0 && w >= h);
  const float size = cam.sensor_fit == 2 ? cam.sensor_height : cam.sensor_width;
  const float ratio = (float)std::max(1, h) / (float)std::max(1, w);
  p.sensor_w = horizontal ? size : size / ratio;
  p.sensor_h = horizontal ? size * ratio : size;
  p.sanitize();
  return p;
}

/* camera panoramic <equirectangular | fisheye equidistant | fisheye equisolid | cylindrical | mirror ball | off> (round 538):
 * the selected camera's Projection, as the Inspector's Panorama Type sets it. */
void Editor::camera_panoramic_command(const std::vector<std::string> &t) {
  std::string name;
  for (size_t i = 2; i < t.size(); i++) name += (name.empty() ? "" : " ") + t[i];
  const std::string key = to_lower(name);
  Camera *cam = nullptr;
  for (GameObject *g : selected_objects(false))
    if (g && (cam = g->get<Camera>())) break;
  if (!cam) return Log::warn("camera panoramic: select a camera");
  int type = panoramic_type_from_name(key == "cylindrical" || key == "cylinder" ? "central cylindrical" : key == "mirror" ? "mirror ball" : key);
  if (key != "off" && key != "none" && key != "0" && type == 0)
    return Log::warn("camera panoramic <equirectangular | fisheye equidistant | fisheye equisolid | central cylindrical | mirror ball | off>");
  cam->panoramic_type = type;
  mark_changed("Panoramic Camera");
  Log::info("Camera %s: %s", cam->owner ? cam->owner->name.c_str() : "", type ? panoramic_type_name(type) : "Perspective / Orthographic");
}

void Editor::render_panoramic(Renderer3D &r3d, RenderTarget &rt, const Mat4 &v, Vec3 eye, const GameObject *owner, const Camera *cam, bool game, bool scene_lights) {
  static bool said_skipped = false;
  if (!said_skipped && (cam->dof || !camera_filters(owner).empty())) {
    said_skipped = true;
    Log::info("Panoramic camera: depth of field and the camera's filters are not applied to its picture");
  }
  const PanoramicParams p = panoramic_params_for(*cam, rt.width, rt.height);
  /* The camera's axes in the world are the rows of its view matrix. */
  PanoramicCameraFrame frame;
  frame.eye = eye;
  frame.right = {v.m[0], v.m[4], v.m[8]};
  frame.up = {v.m[1], v.m[5], v.m[9]};
  frame.forward = {v.m[2], v.m[6], v.m[10]};
  frame.near_clip = cam->near_clip, frame.far_clip = cam->far_clip;
  panoramic_render(p, rt.width, rt.height, frame, pano_[&r3d], rt.color, rt.stride, [&](int, const Mat4 &fv, const Mat4 &fp, RenderTarget &face_rt) {
    render_deferred(r3d, face_rt, fv, fp, eye, game, scene_lights, cam);
  });
  /* No depth or ids to pick from a panorama: far, nothing. */
  std::fill(rt.depth.begin(), rt.depth.end(), 1.0f);
  std::fill(rt.ids.begin(), rt.ids.end(), 0u);
  r3d.rebind(&rt, v, cam->projection(rt.width / (float)std::max(1, rt.height)));  // overlays draw on the view
}

}  // namespace bl
