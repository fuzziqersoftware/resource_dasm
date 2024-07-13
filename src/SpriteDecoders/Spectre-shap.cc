#include "Decoders.hh"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <deque>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <string>

using namespace std;

namespace ResourceDASM {

bool ccw(
    const Vector2<double>& a,
    const Vector2<double>& b,
    const Vector2<double>& c) {
  double i = ((c.y - a.y) * (b.x - a.x));
  double j = ((b.y - a.y) * (c.x - a.x));
  return i <= j;
}

bool orientation_for_point(const vector<Vector2<double>>& pts, size_t index) {
  if (pts.size() < 3) {
    throw runtime_error("not enough points for plane");
  }
  if (index >= pts.size()) {
    throw logic_error("invalid point index");
  }

  if (index == 0) {
    return ccw(pts[pts.size() - 1], pts[0], pts[1]);
  } else if (index == pts.size() - 1) {
    return ccw(pts[pts.size() - 2], pts[pts.size() - 1], pts[0]);
  } else {
    return ccw(pts[index - 1], pts[index], pts[index + 1]);
  }
}

Vector3<double> normal_for_point(
    const Vector3<double>* pts, size_t num_pts, size_t index) {
  if (num_pts < 3) {
    throw runtime_error("not enough points for plane");
  }
  if (index >= num_pts) {
    throw logic_error("invalid point index");
  }

  Vector3<double> ret;
  if (index == 0) {
    ret = (pts[1] - pts[0]).cross(pts[num_pts - 1] - pts[0]);
  } else if (index == num_pts - 1) {
    ret = (pts[0] - pts[num_pts - 1]).cross(pts[num_pts - 2] - pts[num_pts - 1]);
  } else {
    ret = (pts[index + 1] - pts[index]).cross(pts[index - 1] - pts[index]);
  }

  double norm = ret.norm();
  if (norm == 0.0) {
    throw runtime_error("point neighbors are colinear");
  }
  ret /= norm;

  return ret;
}

vector<Vector2<double>> project_points(
    const Vector3<double>& plane_normal, const vector<Vector3<double>>& pts) {
  // We'll treat the vectors formed by points 0, 1, and 2 as a basis for this
  // plane. We don't need them to be orthogonal - we just need to preserve the
  // orientation of the polygon, so any affine transform will do.
  auto b1 = pts[1] - pts[0];
  b1 /= b1.norm();
  auto b2 = pts[2] - pts[0];
  b2 /= b2.norm();

  vector<Vector2<double>> ret;
  ret.reserve(pts.size());
  for (const auto& pt : pts) {
    // TODO: Do we even need to project here, or can we just dot with the basis
    // vectors and call it done? It's 1AM and I don't want to figure this out
    // for realz right now.
    Vector3 v = pt - pts[0];
    double dist = v.dot(plane_normal);
    Vector3 projected = pt - (plane_normal * dist);
    ret.emplace_back(b1.dot(projected), b2.dot(projected));
  }
  return ret;
}

template <typename T>
class CycleList {
public:
  CycleList() : head_item(nullptr),
                tail_item(nullptr),
                item_count(0) {}
  ~CycleList() {
    while (this->tail_item) {
      this->remove_next(this->tail_item);
    }
  }

  struct Item {
    Item* next;
    T value;
  };

  inline Item* head() const {
    return this->head_item;
  }
  inline Item* tail() const {
    return this->tail_item;
  }
  inline size_t size() const {
    return this->item_count;
  }

  void add(const T& v) {
    if (!this->head_item) {
      this->head_item = new Item();
      this->tail_item = this->head_item;
      this->head_item->next = this->head_item;
      this->head_item->value = v;
    } else {
      this->tail_item->next = new Item();
      this->tail_item = this->tail_item->next;
      this->tail_item->next = this->head_item;
      this->tail_item->value = v;
    }
    this->item_count++;
  }

  void remove_next(Item* i) {
    if (i->next == i) {
      if (i != this->head_item) {
        throw logic_error("last node is not head");
      }
      if (i != this->tail_item) {
        throw logic_error("last node is not head");
      }
      delete i;
      this->head_item = nullptr;
      this->tail_item = nullptr;
    } else {
      Item* to_delete = i->next;
      if (this->tail_item == to_delete) {
        this->tail_item = i;
      }
      if (this->head_item == to_delete) {
        this->head_item = to_delete->next;
      }
      i->next = to_delete->next;
      delete to_delete;
    }
    this->item_count--;
  }

private:
  Item* head_item;
  Item* tail_item;
  size_t item_count;
};

vector<Vector3<size_t>> triangulate_poly(const vector<Vector2<double>>& pts) {
  // This function splits a closed planar polygon into triangles, avoiding any
  // concave vertices. This is an implementation of a simple "ear clipping"
  // algorithm; the basic idea is to find a run of three vertices that are
  // specified in clockwise order, then add them to the returned triangle list and delete the center point from the polygon (which
  // deletes that triangle, leaving the remaining two points to form a new
  // edge). There are faster ways to triangulate a possibly-concave polygon, but
  // this is likely the simplest way to do it.

  if (pts.size() < 3) {
    throw runtime_error("not enough points for a triangle");
  }

  for (size_t attempt = 0; attempt < 2; attempt++) {
    bool initial_ccw = !!attempt;

    CycleList<size_t> remaining;
    for (size_t z = 0; z < pts.size(); z++) {
      remaining.add(z);
    }
    auto* i = remaining.head();

    vector<Vector3<size_t>> ret;
    size_t consecutive_skips = 0;
    while (remaining.size() > 2) {
      size_t ix1 = i->value;
      size_t ix2 = i->next->value;
      size_t ix3 = i->next->next->value;

      // If these three consecutive points specify a triangle of the right
      // orientation, then it might be a candidate for removal
      bool match = (ccw(pts[ix1], pts[ix2], pts[ix3]) == initial_ccw);
      if (match) {
        // We also need to check that the edge between the first and third
        // points does not intersect any of the polygon's existing edges. This
        // is equivalent to saying that there are no other vertices inside the
        // triangle formed by the three points, which is equivalent to saying
        // that for all other points, at least one of the triangles formed with
        // any two of the candidate triangle's edges and that point has the
        // opposite orientation.
        for (auto* other_i = i->next->next->next; match && (other_i != i); other_i = other_i->next) {
          bool is_outside = false;
          is_outside |= (ccw(pts[ix1], pts[ix2], pts[other_i->value]) != initial_ccw);
          is_outside |= (ccw(pts[ix2], pts[ix3], pts[other_i->value]) != initial_ccw);
          is_outside |= (ccw(pts[ix3], pts[ix1], pts[other_i->value]) != initial_ccw);
          match &= is_outside;
        }
      }
      if (match) {
        ret.emplace_back(ix1, ix2, ix3);
        remaining.remove_next(i);
        consecutive_skips = 0;
      } else {
        i = i->next;
        consecutive_skips++;
      }
      if (consecutive_skips >= remaining.size()) {
        break;
      }
    }
    if (remaining.size() <= 2) {
      return ret;
    }
  }

  throw runtime_error("could not determine inside of polygon");
}

vector<Vector3<size_t>> split_faces_fan(size_t num_pts) {
  if (num_pts < 3) {
    throw runtime_error("not enough points for triangle fan");
  }
  vector<Vector3<size_t>> ret;
  for (size_t z = 2; z < num_pts; z++) {
    ret.emplace_back(0, z - 1, z);
  }
  return ret;
}

vector<Vector3<double>> collect_vertices(
    const vector<Vector3<double>>& vertices,
    const vector<size_t>& indices) {
  vector<Vector3<double>> ret;
  for (size_t index : indices) {
    ret.emplace_back(vertices.at(index));
  }
  return ret;
}

string DecodedShap3D::model_as_stl() const {
  deque<string> lines;
  lines.emplace_back("solid obj");

  for (const auto& plane : this->planes) {
    auto plane_vertices = collect_vertices(this->vertices, plane.vertex_nums);

    // We assume all points on each defined plane are coplanar and defined in
    // clockwise order, but they may represent a concave polygon. To triangulate
    // the polygon, we first have to project it into an appropriate 2D space.
    vector<Vector3<size_t>> tri_indexes;
    try {
      auto normal = normal_for_point(plane_vertices.data(), plane_vertices.size(), 0);
      auto projected = project_points(normal, plane_vertices);
      tri_indexes = triangulate_poly(projected);

    } catch (const runtime_error& e) {
      // If we can't triangulate the polygon (perhaps if it wasn't actually
      // planar), fall back to just blindly converting it to a triangle fan
      fprintf(stderr, "warning: failed to split face analytically (%s); fanning it instead\n", e.what());
      tri_indexes = split_faces_fan(plane_vertices.size());
    }

    for (const auto& tri : tri_indexes) {
      Vector3<double> tri_pts[3] = {
          plane_vertices.at(tri.x),
          plane_vertices.at(tri.y),
          plane_vertices.at(tri.z)};
      auto n = normal_for_point(tri_pts, 3, 0);
      lines.emplace_back(string_printf("facet normal %g %g %g", n.x, n.y, n.z));
      lines.emplace_back("  outer loop");
      lines.emplace_back(string_printf("    vertex %g %g %g", tri_pts[0].x, tri_pts[0].y, tri_pts[0].z));
      lines.emplace_back(string_printf("    vertex %g %g %g", tri_pts[1].x, tri_pts[1].y, tri_pts[1].z));
      lines.emplace_back(string_printf("    vertex %g %g %g", tri_pts[2].x, tri_pts[2].y, tri_pts[2].z));
      lines.emplace_back("  endloop");
      lines.emplace_back("endfacet");
    }
  }

  return join(lines, "\n");
}

string DecodedShap3D::model_as_obj() const {
  deque<string> lines;
  deque<string> face_lines;
  size_t normal_index = 1;
  for (const auto& plane : this->planes) {
    for (const auto& v : this->vertices) {
      lines.emplace_back(string_printf("v %g %g %g", v.x, v.y, v.z));
    }

    string face_line = "f";
    auto plane_vertices = collect_vertices(this->vertices, plane.vertex_nums);

    // Unlike STL, OBJ format supports non-triangular faces. However, it also
    // requires normals for each vertex, rather than a single face normal, so we
    // still have to compute the plane equation and project the face into 2D so
    // we can detect concave points (since their normals would point the wrong
    // direction if we didn't).
    auto normal = normal_for_point(plane_vertices.data(), plane_vertices.size(), 0);
    auto projected = project_points(normal, plane_vertices);
    bool initial_orientation = orientation_for_point(projected, 0);

    for (size_t z = 0; z < plane_vertices.size(); z++) {
      auto n = normal_for_point(plane_vertices.data(), plane_vertices.size(), z);
      if (orientation_for_point(projected, z) != initial_orientation) {
        n *= -1;
      }
      lines.emplace_back(string_printf("vn %g %g %g", n.x, n.y, n.z));
      face_line += string_printf(" %zu//%zu", plane.vertex_nums[z] + 1, normal_index);
      normal_index++;
    }
    face_lines.emplace_back(std::move(face_line));
  }

  for (string& s : face_lines) {
    lines.emplace_back(std::move(s));
  }
  return join(lines, "\n");
}

string DecodedShap3D::top_view_as_svg() const {
  // Compute the bounding box.
  // For some reason, the top view points have 3 dimensions. It appears the y
  // coordinates are unused, so we simply ignore them.
  double xmin = 0.0;
  double xmax = 0.0;
  double zmin = 0.0;
  double zmax = 0.0;
  if (!this->top_view_lines.empty()) {
    const auto& first_pt = this->top_view_vertices.at(this->top_view_lines[0].start);
    xmin = xmax = first_pt.x;
    zmin = zmax = first_pt.z;
    auto visit_pt = [&](const Vector3<double>& pt) {
      if (xmin < pt.x) {
        xmin = pt.x;
      }
      if (xmax > pt.x) {
        xmax = pt.x;
      }
      if (zmin < pt.z) {
        zmin = pt.z;
      }
      if (zmax > pt.z) {
        zmax = pt.z;
      }
    };
    for (const auto& line : this->top_view_lines) {
      visit_pt(this->top_view_vertices.at(line.start));
      visit_pt(this->top_view_vertices.at(line.end));
    }
  }

  // Generate the SVG contents
  deque<string> lines;
  lines.emplace_back("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>");
  lines.emplace_back("<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">");
  // width and height are in pixels (hence the int cast), but viewBox are floats
  lines.emplace_back(string_printf("<svg width=\"%" PRId64 "\" height=\"%" PRId64 "\" viewBox=\"%g %g %g %g\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">",
      static_cast<int64_t>(xmax - xmin), static_cast<int64_t>(zmax - zmin),
      xmin, zmin, xmax - xmin, zmax - zmin));
  for (const auto& line : this->top_view_lines) {
    const auto& start = this->top_view_vertices.at(line.start);
    const auto& end = this->top_view_vertices.at(line.end);
    lines.emplace_back(string_printf("<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"black\" stroke-width=\"1\" />",
        start.x, start.z, end.x, end.z));
  }
  lines.emplace_back("</svg>");

  return join(lines, "\n");
}

DecodedShap3D decode_shap(const string& data) {
  StringReader r(data);

  DecodedShap3D ret;

  {
    uint16_t num_vertices = r.get_u16b() + 1;
    while (ret.vertices.size() < num_vertices) {
      auto& v = ret.vertices.emplace_back();
      v.x = r.get<Fixed>().as_double();
      v.y = r.get<Fixed>().as_double();
      v.z = r.get<Fixed>().as_double();
    }
  }

  {
    uint16_t num_planes = r.get_u16b() + 1;
    while (ret.planes.size() < num_planes) {
      auto& plane = ret.planes.emplace_back();
      uint16_t num_vertices = r.get_u16b() + 1;
      while (plane.vertex_nums.size() < num_vertices) {
        // These appear to be one-based, not zero-based
        plane.vertex_nums.emplace_back(r.get_u16b() - 1);
      }
      plane.color_index = r.get_u16b();
    }
  }

  {
    uint16_t num_vertices = r.get_u16b() + 1;
    while (ret.top_view_vertices.size() < num_vertices) {
      auto& v = ret.top_view_vertices.emplace_back();
      v.x = r.get<Fixed>().as_double();
      v.y = r.get<Fixed>().as_double();
      v.z = r.get<Fixed>().as_double();
    }
  }

  {
    uint16_t num_lines = r.get_u16b() + 1;
    while (ret.top_view_lines.size() < num_lines) {
      auto& line = ret.top_view_lines.emplace_back();
      line.start = r.get_u16b() - 1;
      line.end = r.get_u16b() - 1;
    }
  }

  return ret;
}

} // namespace ResourceDASM
