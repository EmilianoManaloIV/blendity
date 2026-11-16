// SPDX-License-Identifier: GPL-2.0-or-later
// The Point Cloud component and its console command (round 530, ADR 0147).
#include "pointcloud_object.h"

#include "../core/core.h"

#include <algorithm>
#include <cstdlib>

namespace bl {

void PointCloudObject::reflect(Reflector &r) {
  /* The arrays first: a file's reader fills `data` before the Type and Degree below compare against it. */
  r.point_cloud("Data", data);
  static const char *const kTypes[] = {"Points", "Gaussian Splats"};
  int type = data ? (int)data->type : 0;
  const int type_before = type;
  r.enumeration("Type", type, kTypes, 2);
  r.help("Points: a dot per point, Point Size across. Gaussian Splats: each point a soft, turned ellipsoid whose colour changes with the "
         "view (spherical harmonics). Blender 5.3: Point Cloud > Type.");
  if (type != type_before && type >= 0 && type <= 1) {
    if (!data) data = std::make_shared<PointCloud>();
    pointcloud_make_mutable(data)->set_type((PointCloudType)type, point_size * 0.5f);
    data->touch();
  }
  int count = data ? (int)std::min<size_t>(data->count(), (size_t)INT32_MAX) : 0;
  r.greyed(true);
  r.field("Count", count, 0, INT32_MAX);
  r.help("How many points the cloud holds (an import or `pointcloud random` sets it).");
  r.greyed(false);
  if (!data || data->is_splats() || !r.inspector()) {
    int degree = data ? data->sh_degree : 0;
    const int degree_before = degree;
    r.field("SH Degree", degree, 0, kSplatMaxShDegree);
    r.help("How far the splats' colour changes with the view: 0 the base colour alone, up to 3 (15 more coefficients a splat). Lowering it "
           "drops the higher bands; raising it adds zero ones.");
    if (degree != degree_before && data && data->is_splats()) {
      pointcloud_make_mutable(data)->set_sh_degree(degree);
      data->touch();
    }
  }
  if (!data || !data->is_splats() || !r.inspector()) {
    r.field("Point Size", point_size, 0.001f, 0.0f, 1e6f);
    r.help("A Points cloud's dot size, in the object's units.");
  }
  if (!std::isfinite(point_size) || point_size < 0.0f) point_size = 0.02f;
}

GameObject *create_point_cloud_object(Scene &scene, PointCloudType type, GameObject *parent) {
  const char *base = type == PointCloudType::GaussianSplats ? "Gaussian Splats" : "Point Cloud";
  std::string name = base;
  for (int k = 1; scene.find_by_name(name); k++) name = strprintf("%s (%d)", base, k);
  GameObject *g = scene.create(name, parent);
  auto *pc = g->add<PointCloudObject>();
  pc->data = std::make_shared<PointCloud>();
  pc->data->name = name;
  pc->data->type = type;
  return g;
}

static std::string describe(const GameObject &g, const PointCloudObject &pc) {
  if (!pc.data) return strprintf("%s: an empty point cloud", g.name.c_str());
  const PointCloud &d = *pc.data;
  const AABB b = d.bounds();
  std::string s = strprintf("%s: %s, %zu points", g.name.c_str(), d.is_splats() ? "Gaussian Splats" : "Points", d.count());
  if (d.is_splats()) s += strprintf(", SH degree %d", d.sh_degree);
  else s += strprintf(", point size %g", pc.point_size);
  if (b.valid()) s += strprintf(", bounds %g %g %g .. %g %g %g", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
  return s;
}

static bool parse_type(const std::string &w, PointCloudType &t) {
  const std::string l = to_lower(w);
  if (l == "points" || l == "point") t = PointCloudType::Points;
  else if (l == "splats" || l == "splat" || l == "gsplat" || l == "gaussian") t = PointCloudType::GaussianSplats;
  else return false;
  return true;
}

std::string pointcloud_command(Scene &scene, GameObject *selected, const std::vector<std::string> &args, GameObject **made, bool &changed) {
  changed = false;
  if (made) *made = nullptr;
  const std::string sub = args.size() > 1 ? to_lower(args[1]) : "info";
  PointCloudObject *pc = selected ? selected->get<PointCloudObject>() : nullptr;
  const char *usage = "pointcloud: usage: pointcloud new [points|splats] | random <points|splats> <count> [sh_degree] [seed] [extent] | info | type <points|splats> | "
                      "degree <0-3> | size <s>";
  if (sub == "new" || sub == "random") {
    PointCloudType type = PointCloudType::Points;  // (as Create > Point Cloud > Points)
    if (args.size() > 2 && !parse_type(args[2], type)) return usage;
    if (sub == "new") {
      GameObject *g = create_point_cloud_object(scene, type);
      if (made) *made = g;
      changed = true;
      return describe(*g, *g->get<PointCloudObject>());
    }
    if (args.size() < 4) return usage;
    const double want = std::strtod(args[3].c_str(), nullptr);
    /* A test cloud is made on the main thread: 2^24 points (4 GB at degree 3) at most, well under the importers' cap. */
    constexpr double kConsoleMax = (double)(1u << 24);
    if (!(want >= 0.0) || want > kConsoleMax) return strprintf("pointcloud: a count from 0 to %.0f", kConsoleMax);
    const int degree = args.size() > 4 ? std::atoi(args[4].c_str()) : 0;
    const uint32_t seed = args.size() > 5 ? (uint32_t)std::strtoul(args[5].c_str(), nullptr, 10) : 1u;
    const float extent = args.size() > 6 ? std::strtof(args[6].c_str(), nullptr) : 1.0f;
    PointCloudPtr cloud = pointcloud_random(type, (size_t)want, degree, seed, extent);
    if (!cloud) return "pointcloud: not enough memory for that many points";
    GameObject *g = pc ? selected : create_point_cloud_object(scene, type);
    if (!pc) pc = g->get<PointCloudObject>();
    cloud->name = g->name;
    pc->data = cloud;
    cloud->touch();
    if (made) *made = g;
    changed = true;
    return describe(*g, *pc);
  }
  if (!pc) return "pointcloud: select a point cloud first";
  if (sub == "info") return describe(*selected, *pc);
  if (!pc->data) pc->data = std::make_shared<PointCloud>();
  if (sub == "type" && args.size() > 2) {
    PointCloudType t;
    if (!parse_type(args[2], t)) return usage;
    if (t != pc->data->type) {
      pointcloud_make_mutable(pc->data)->set_type(t, pc->point_size * 0.5f);
      pc->data->touch();
      changed = true;
    }
    return describe(*selected, *pc);
  }
  if (sub == "degree" && args.size() > 2) {
    if (!pc->data->is_splats()) return "pointcloud: only Gaussian Splats have an SH degree";
    const int d = std::max(0, std::min(kSplatMaxShDegree, std::atoi(args[2].c_str())));
    if (d != pc->data->sh_degree) {
      pointcloud_make_mutable(pc->data)->set_sh_degree(d);
      pc->data->touch();
      changed = true;
    }
    return describe(*selected, *pc);
  }
  if (sub == "size" && args.size() > 2) {
    const float s = std::strtof(args[2].c_str(), nullptr);
    if (!std::isfinite(s) || s < 0.0f) return "pointcloud: a size of 0 or more";
    changed = s != pc->point_size;
    pc->point_size = s;
    return describe(*selected, *pc);
  }
  return usage;
}

}  // namespace bl
