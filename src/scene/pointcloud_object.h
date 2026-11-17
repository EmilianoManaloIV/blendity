// SPDX-License-Identifier: GPL-2.0-or-later
// The Point Cloud component (round 530, ADR 0147): what makes a GameObject a point cloud - Blender's Point Cloud
// object, whose Type is Points or Gaussian Splats (5.3). It holds a PointCloudPtr (pointcloud.h) the way a MeshFilter
// holds a MeshPtr: shared between undo snapshots and duplicates until one edits it (pointcloud_make_mutable).
#pragma once

#include "pointcloud.h"
#include "scene.h"

namespace bl {

struct PointCloudObject : ComponentBase<PointCloudObject> {
  static constexpr const char *kName = "PointCloud";
  PointCloudPtr data;
  /* A Points cloud's dot size in object units (Blender: the points' radius, here one size for all). */
  float point_size = 0.02f;

  RenderRole render_role() const override { return RenderRole::PointCloud; }
  /* Type and SH Degree edit the data (a copy when shared); Count is shown greyed. */
  void reflect(Reflector &r) override;  // pointcloud_object.cpp
};

/* "Point Cloud" / "Gaussian Splats": a new object under `parent` with an empty cloud of that type (what the Create
 * menus and `create` make). */
GameObject *create_point_cloud_object(Scene &scene, PointCloudType type, GameObject *parent = nullptr);

/* `pointcloud ...` (the console): "random <points|splats> <count> [sh_degree] [seed] [extent]" fills the selected
 * point cloud (or a new one) with pointcloud_random; "info" describes it; "type <points|splats>"; "degree <0-3>";
 * "size <s>". `selected` is the object the command acts on (null: none). Returns the console's answer; `changed`
 * is set when the scene changed (the caller marks an undo step). */
std::string pointcloud_command(Scene &scene, GameObject *selected, const std::vector<std::string> &args, GameObject **made, bool &changed);

}  // namespace bl
