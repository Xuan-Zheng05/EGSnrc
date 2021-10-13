/*
###############################################################################
#
#  EGSnrc egs++ mesh geometry library implementation.
#
#  Copyright (C) 2020 Mevex Corporation
#
#  This file is part of EGSnrc.
#
#  Parts of this file, namely, the closest_point_triangle and
#  closest_point_tetrahedron functions, are adapted from Chapter 5 of
#  "Real-Time Collision Detection" by Christer Ericson with the consent
#  of the author and of the publisher.
#
#  EGSnrc is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Affero General Public License as published by the
#  Free Software Foundation, either version 3 of the License, or (at your
#  option) any later version.
#
#  EGSnrc is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
#  more details.
#
#  You should have received a copy of the GNU Affero General Public License
#  along with EGSnrc. If not, see <http://www.gnu.org/licenses/>.
#
###############################################################################
#
#  Authors:          Dave Macrillo,
#                    Matt Ronan,
#                    Nigel Vezeau,
#                    Lou Thompson,
#                    Max Orok
#
###############################################################################
*/

// TODO
#include "egs_input.h"
#include "egs_mesh.h"
#include "egs_vector.h"

#include "mesh_neighbours.h"
#include "msh_parser.h"

#include <cassert>
#include <chrono>
#include <deque>
#include <limits>
#include <unordered_map>

// Have to define the destructor here instead of in egs_mesh.h because of the
// unique_ptr to forward declared EGS_Mesh_Octree members.
EGS_Mesh::~EGS_Mesh() = default;

// anonymous namespace
namespace {

const EGS_Float eps = 1e-8;

inline bool approx_eq(double a, double b, double e = eps) {
    return (std::abs(a - b) <= e * (std::abs(a) + std::abs(b) + 1.0));
}

inline bool is_zero(const EGS_Vector &v) {
    return approx_eq(0.0, v.length(), eps);
}

inline EGS_Float min3(EGS_Float a, EGS_Float b, EGS_Float c) {
    return std::min(std::min(a, b), c);
}

inline EGS_Float max3(EGS_Float a, EGS_Float b, EGS_Float c) {
    return std::max(std::max(a, b), c);
}

void print_egsvec(const EGS_Vector& v, std::ostream& out = std::cout) {
    out << std::setprecision(std::numeric_limits<double>::max_digits10) <<
    "{\n  x: " << v.x << "\n  y: " << v.y << "\n  z: " << v.z << "\n}\n";
}

inline EGS_Float dot(const EGS_Vector &x, const EGS_Vector &y) {
    return x * y;
}

inline EGS_Vector cross(const EGS_Vector &x, const EGS_Vector &y) {
    return x.times(y);
}

inline EGS_Float distance2(const EGS_Vector &x, const EGS_Vector &y) {
    return (x - y).length2();
}

inline EGS_Float distance(const EGS_Vector &x, const EGS_Vector &y) {
    return std::sqrt(distance2(x, y));
}

EGS_Vector closest_point_triangle(const EGS_Vector &P, const EGS_Vector &A, const EGS_Vector& B, const EGS_Vector& C)
{
    // vertex region A
    EGS_Vector ab = B - A;
    EGS_Vector ac = C - A;
    EGS_Vector ao = P - A;

    EGS_Float d1 = dot(ab, ao);
    EGS_Float d2 = dot(ac, ao);
    if (d1 <= 0.0 && d2 <= 0.0)
        return A;

    // vertex region B
    EGS_Vector bo = P - B;
    EGS_Float d3 = dot(ab, bo);
    EGS_Float d4 = dot(ac, bo);
    if (d3 >= 0.0 && d4 <= d3)
        return B;

    // edge region AB
    EGS_Float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        EGS_Float v = d1 / (d1 - d3);
        return A + v * ab;
    }

    // vertex region C
    EGS_Vector co = P - C;
    EGS_Float d5 = dot(ab, co);
    EGS_Float d6 = dot(ac, co);
    if (d6 >= 0.0 && d5 <= d6)
        return C;

    // edge region AC
    EGS_Float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        EGS_Float w = d2 / (d2 - d6);
        return A + w * ac;
    }

    // edge region BC
    EGS_Float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        EGS_Float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return B + w * (C - B);
    }

    // inside the face
    EGS_Float denom = 1.0 / (va + vb + vc);
    EGS_Float v = vb * denom;
    EGS_Float w = vc * denom;
    return A + v * ab + w * ac;
}

// Returns true if the point is on the outside of the plane defined by ABC using
// reference point D, i.e. if D and P are on opposite sides of the plane of ABC.
inline bool point_outside_of_plane(EGS_Vector P, EGS_Vector A, EGS_Vector B, EGS_Vector C, EGS_Vector D) {
    return dot(P - A, cross(B - A, C - A)) * dot(D - A, cross(B - A, C - A)) < 0.0;
}

EGS_Vector closest_point_tetrahedron(const EGS_Vector &P, const EGS_Vector &A, const EGS_Vector &B, const EGS_Vector &C, const EGS_Vector &D)
{
    EGS_Vector min_point = P;
    EGS_Float min = std::numeric_limits<EGS_Float>::max();

    auto maybe_update_min_point = [&](const EGS_Vector& A, const EGS_Vector& B, const EGS_Vector& C) {
        EGS_Vector q = closest_point_triangle(P, A, B, C);
        EGS_Float dis = distance2(q, P);
        if (dis < min) {
            min = dis;
            min_point = q;
        }
    };

    if (point_outside_of_plane(P, A, B, C, D)) {
        maybe_update_min_point(A, B, C);
    }

    if (point_outside_of_plane(P, A, C, D, B)) {
        maybe_update_min_point(A, C, D);
    }

    if (point_outside_of_plane(P, A, B, D, C)) {
        maybe_update_min_point(A, B, D);
    }
    if (point_outside_of_plane(P, B, D, C, A)) {
        maybe_update_min_point(B, D, C);
    }

    return min_point;
}

/// TODO: combine with interior_triangle_ray_intersection
/// TODO: replace with Woop 2013 watertight algorithm.
///
/// Triangle-ray intersection algorithm for finding intersections with a ray
/// outside the tetrahedron.
/// Inputs:
/// * particle position p,
/// * normalized velocity v_norm
/// * triangle points A, B, C (any ordering)
///
/// Returns 1 if there is an intersection and 0 if not. If there is an intersection,
/// the out parameter dist will be the distance along v_norm to the intersection point.
///
/// Implementation of double-sided Möller-Trumbore ray-triangle intersection
/// <http://www.graphics.cornell.edu/pubs/1997/MT97.pdf>
int exterior_triangle_ray_intersection(const EGS_Vector &p,
    const EGS_Vector &v_norm, const EGS_Vector& a, const EGS_Vector& b,
    const EGS_Vector& c, EGS_Float& dist)
{
    const EGS_Float eps = 1e-10;
    EGS_Vector ab = b - a;
    EGS_Vector ac = c - a;

    EGS_Vector pvec = cross(v_norm, ac);
    EGS_Float det = dot(ab, pvec);

    if (det > -eps && det < eps) {
        return 0;
    }
    EGS_Float inv_det = 1.0 / det;
    EGS_Vector tvec = p - a;
    EGS_Float u = dot(tvec, pvec) * inv_det;
    if (u < 0.0 || u > 1.0) {
        return 0;
    }
    EGS_Vector qvec = cross(tvec, ab);
    EGS_Float v = dot(v_norm, qvec) * inv_det;
    if (v < 0.0 || u + v > 1.0) {
        return 0;
    }
    // intersection found
    dist = dot(ac, qvec) * inv_det;
    if (dist < 0.0) {
        return 0;
    }
    return 1;
}

/// TODO: combine with exterior_triangle_ray_intersection
/// Triangle-ray intersection algorithm for finding intersections with a ray
/// inside the tetrahedron.
int interior_triangle_ray_intersection(const EGS_Vector &p,
    const EGS_Vector &v_norm, const EGS_Vector& face_norm, const EGS_Vector& a,
    const EGS_Vector& b, const EGS_Vector& c, EGS_Float& dist)
{
    const EGS_Float eps = 1e-10;
    if (dot(v_norm, face_norm) > -eps) {
        return 0;
    }

    if (dot(face_norm, p - a) < 0.0) {
        return 0;
    }

    EGS_Vector ab = b - a;
    EGS_Vector ac = c - a;
    EGS_Vector pvec = cross(v_norm, ac);
    EGS_Float det = dot(ab, pvec);

    if (det > -eps && det < eps) {
        return 0;
    }
    EGS_Float inv_det = 1.0 / det;
    EGS_Vector tvec = p - a;
    EGS_Float u = dot(tvec, pvec) * inv_det;
    if (u < 0.0 || u > 1.0) {
        return 0;
    }
    EGS_Vector qvec = cross(tvec, ab);
    EGS_Float v = dot(v_norm, qvec) * inv_det;
    if (v < 0.0 || u + v > 1.0) {
        return 0;
    }
    // intersection found
    dist = dot(ac, qvec) * inv_det;
    if (dist < 0.0) {
        dist = 0.0;
        return 0;
    }
    return 1;
}

/// Parse the body of a msh4.1 file into an EGS_Mesh using the msh_parser API.
///
/// Throws a std::runtime_error if parsing fails.
EGS_Mesh* parse_msh41_body(std::istream& input) {
    std::vector<msh_parser::internal::msh41::Node> nodes;
    std::vector<msh_parser::internal::msh41::MeshVolume> volumes;
    std::vector<msh_parser::internal::msh41::PhysicalGroup> groups;
    std::vector<msh_parser::internal::msh41::Tetrahedron> elements;

    std::string parse_err;
    std::string input_line;
    while (std::getline(input, input_line)) {
        msh_parser::internal::rtrim(input_line);
        // stop reading if we hit another mesh file
        if (input_line == "$MeshFormat") {
            break;
        }
        if (input_line == "$Entities") {
           volumes = msh_parser::internal::msh41::parse_entities(input);
        } else if (input_line == "$PhysicalNames") {
            groups = msh_parser::internal::msh41::parse_groups(input);
        } else if (input_line == "$Nodes") {
            nodes = msh_parser::internal::msh41::parse_nodes(input);
        } else if (input_line == "$Elements") {
            elements = msh_parser::internal::msh41::parse_elements(input);
        }
    }
    if (volumes.empty()) {
        throw std::runtime_error("No volumes were parsed from $Entities section");
    }
    if (nodes.empty()) {
        throw std::runtime_error("No nodes were parsed, missing $Nodes section");
    }
    if (groups.empty()) {
        throw std::runtime_error("No groups were parsed from $PhysicalNames section");
    }
    if (elements.empty()) {
        throw std::runtime_error("No tetrahedrons were parsed from $Elements section");
    }

    // ensure each entity has a valid group
    std::unordered_set<int> group_tags;
    group_tags.reserve(groups.size());
    for (auto g: groups) {
        group_tags.insert(g.tag);
    }
    std::unordered_map<int, int> volume_groups;
    volume_groups.reserve(volumes.size());
    for (auto v: volumes) {
        if (group_tags.find(v.group) == group_tags.end()) {
            throw std::runtime_error("volume " + std::to_string(v.tag) + " had unknown physical group tag " + std::to_string(v.group));
        }
        volume_groups.insert({ v.tag, v.group });
    }

    // ensure each element has a valid entity and therefore a valid physical group
    std::vector<int> element_groups;
    element_groups.reserve(elements.size());
    for (auto e: elements) {
        auto elt_group = volume_groups.find(e.volume);
        if (elt_group == volume_groups.end()) {
            throw std::runtime_error("tetrahedron " + std::to_string(e.tag) + " had unknown volume tag " + std::to_string(e.volume));
        }
        element_groups.push_back(elt_group->second);
    }

    std::vector<EGS_Mesh::Tetrahedron> mesh_elts;
    mesh_elts.reserve(elements.size());
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto& elt = elements[i];
        mesh_elts.push_back(EGS_Mesh::Tetrahedron(
            elt.tag, element_groups[i], elt.a, elt.b, elt.c, elt.d
        ));
    }

    std::vector<EGS_Mesh::Node> mesh_nodes;
    mesh_nodes.reserve(nodes.size());
    for (const auto& n: nodes) {
        mesh_nodes.push_back(EGS_Mesh::Node(
            n.tag, n.x, n.y, n.z
        ));
    }

    std::vector<EGS_Mesh::Medium> media;
    media.reserve(groups.size());
    for (const auto& g: groups) {
        media.push_back(EGS_Mesh::Medium(g.tag, g.name));
    }

    // TODO: check all 3d physical groups were used by elements
    // TODO: ensure all element node tags are valid
    return new EGS_Mesh(
        std::move(mesh_elts),
        std::move(mesh_nodes),
        std::move(media)
    );
}
} // anonymous namespace

class EGS_Mesh_Octree {
private:
    static double tet_min_x(const EGS_Mesh::Nodes& n) {
        return std::min(n.A.x, std::min(n.B.x, std::min(n.C.x, n.D.x)));
    }
    static double tet_max_x(const EGS_Mesh::Nodes& n) {
        return std::max(n.A.x, std::max(n.B.x, std::max(n.C.x, n.D.x)));
    }
    static double tet_min_y(const EGS_Mesh::Nodes& n) {
        return std::min(n.A.y, std::min(n.B.y, std::min(n.C.y, n.D.y)));
    }
    static double tet_max_y(const EGS_Mesh::Nodes& n) {
        return std::max(n.A.y, std::max(n.B.y, std::max(n.C.y, n.D.y)));
    }
    static double tet_min_z(const EGS_Mesh::Nodes& n) {
        return std::min(n.A.z, std::min(n.B.z, std::min(n.C.z, n.D.z)));
    }
    static double tet_max_z(const EGS_Mesh::Nodes& n) {
        return std::max(n.A.z, std::max(n.B.z, std::max(n.C.z, n.D.z)));
    }

    // An axis-aligned bounding box.
    struct BoundingBox {
        double min_x = 0.0;
        double max_x = 0.0;
        double min_y = 0.0;
        double max_y = 0.0;
        double min_z = 0.0;
        double max_z = 0.0;
        BoundingBox() = default;
        BoundingBox(double min_x, double max_x, double min_y, double max_y,
            double min_z, double max_z) : min_x(min_x), max_x(max_x),
                min_y(min_y), max_y(max_y), min_z(min_z), max_z(max_z) {}
        double mid_x() const {
            return (min_x + max_x) / 2.0;
        }
        double mid_y() const {
            return (min_y + max_y) / 2.0;
        }
        double mid_z() const {
            return (min_z + max_z) / 2.0;
        }
        void expand(double delta) {
            min_x -= delta;
            min_y -= delta;
            min_z -= delta;
            max_x += delta;
            max_y += delta;
            max_z += delta;
        }
        void print(std::ostream& out = std::cout) const {
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "min_x: " << min_x << "\n";
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "max_x: " << max_x << "\n";
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "min_y: " << min_y << "\n";
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "max_y: " << max_y << "\n";
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "min_z: " << min_z << "\n";
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "max_z: " << max_z << "\n";
        }

        // Adapted from Ericson section 5.2.9 "Testing AABB Against Triangle".
        // Uses a separating axis approach, as originally presented in Akenine-
        // Möller's "Fast 3D Triangle-Box Overlap Testing" with 13 axes checked
        // in total. There are three axis categories, and it is suggested the
        // fastest way to check is 3, 1, 2.
        //
        // We use a more straightforward but less optimized formulation of the
        // separating axis test than Ericson presents, because this test is
        // intended to be done as part of the octree setup but not during the
        // actual simulation.
        //
        // This routine should be robust for ray edges parallel with bounding
        // box edges (category 3) but does not attempt to be robust for the case
        // of degenerate triangle face normals (category 2). See Ericson 5.2.1.1
        //
        // The non-robustness of some cases should not be an issue for the most
        // part as these will likely be false positives (harmless extra checks)
        // instead of false negatives (missed intersections, a huge problem if
        // present).
        bool intersects_triangle(const EGS_Vector& a, const EGS_Vector& b,
            const EGS_Vector& c) const
        {
            if (min3(a.x, b.x, c.x) >= max_x ||
                min3(a.y, b.y, c.y) >= max_y ||
                min3(a.z, b.z, c.z) >= max_z ||
                max3(a.x, b.x, c.x) <= min_x ||
                max3(a.y, b.y, c.y) <= min_y ||
                max3(a.z, b.z, c.z) <= min_z)
            {
                return false;
            }

            EGS_Vector centre(mid_x(), mid_y(), mid_z());
            // extents
            EGS_Float ex = (max_x - min_x) / 2.0;
            EGS_Float ey = (max_y - min_y) / 2.0;
            EGS_Float ez = (max_z - min_z) / 2.0;
            //std::cout << "extents : " << ex << " " << ey << " " << ez << "\n";

            // move triangle to bounding box origin
            EGS_Vector v0 = a - centre;
            EGS_Vector v1 = b - centre;
            EGS_Vector v2 = c - centre;

            // find triangle edge vectors
            const std::array<EGS_Vector, 3> edge_vecs { v1-v0, v2-v1, v0-v2 };

            // Test the 9 category 3 axes (cross products between axis-aligned
            // bounding box unit vectors and triangle edge vectors)
            const EGS_Vector ux {1, 0 ,0}, uy {0, 1, 0}, uz {0, 0, 1};
            const std::array<EGS_Vector, 3> unit_vecs { ux, uy, uz};
            for (const EGS_Vector& u : unit_vecs) {
                for (const EGS_Vector& f : edge_vecs) {
                    const EGS_Vector a = cross(u, f);
                    if (is_zero(a)) {
                        //std::cout << "warning a near zero\n";
                        // Ignore testing this axis, likely won't be a separating
                        // axis. This may lead to false positives, but not false
                        // negatives.
                        continue;
                    }
                    // find box projection radius
                    const EGS_Float r = ex * std::abs(dot(ux, a)) +
                        ey * std::abs(dot(uy, a)) + ez * std::abs(dot(uz, a));
                    // find three projections onto axis a
                    const EGS_Float p0 = dot(v0, a);
                    const EGS_Float p1 = dot(v1, a);
                    const EGS_Float p2 = dot(v2, a);
                    if (std::max(-max3(p0, p1, p2), min3(p0, p1, p2)) + eps > r) {
                        //std::cout << "found separating axis\n";
             //           print_egsvec(a);
                        return false;
                    }
                }
            }
            //std::cout << "passed 9 edge-edge axis tests\n";
            // category 1 - test overlap with AABB face normals
            if (max3(v0.x, v1.x, v2.x) <= -ex || min3(v0.x, v1.x, v2.x) >= ex ||
                max3(v0.y, v1.y, v2.y) <= -ey || min3(v0.y, v1.y, v2.y) >= ey ||
                max3(v0.z, v1.z, v2.z) <= -ez || min3(v0.z, v1.z, v2.z) >= ez)
            {
                return false;
            }
            //std::cout << "passed 3 overlap tests\n";

            // category 2 - test overlap with triangle face normal using AABB
            // plane test (5.2.3)

            // Cross product robustness issues are ignored here (assume
            // non-degenerate and non-oversize triangles)
            const EGS_Vector n = cross(edge_vecs[0], edge_vecs[1]);
            //std::cout << "plane normal: \n";
            //print_egsvec(n);
            if (is_zero(n)) {
                std::cout << "n near zero!\n";
            }
            // projection radius
            const EGS_Float r = ex * std::abs(n.x) + ey * std::abs(n.y) +
                ez * std::abs(n.z);
            // distance from box centre to plane
            //
            // We have to use `a` here and not `v0` as in my printing since the
            // bounding box was not translated to the origin. This is a known
            // erratum, see http://realtimecollisiondetection.net/books/rtcd/errata/
            const EGS_Float s = dot(n, centre) - dot(n, a);
            // intersection if s falls within projection radius
            return std::abs(s) <= r;
        }

        bool intersects_tetrahedron(const EGS_Mesh::Nodes& tet) const {
            return intersects_triangle(tet.A, tet.B, tet.C) ||
                   intersects_triangle(tet.A, tet.C, tet.D) ||
                   intersects_triangle(tet.A, tet.B, tet.D) ||
                   intersects_triangle(tet.B, tet.C, tet.D);
        }

        // Adapted from Ericson section 5.3.3 "Intersecting Ray or Segment
        // Against Box".
        //
        // Returns 1 if there is an intersection and 0 if not. If there is an
        // intersection, the out parameter dist will be the distance along v to
        // the intersection point q.
        int ray_intersection(const EGS_Vector &p, const EGS_Vector &v,
            EGS_Float& dist, EGS_Vector &q) const
        {
            // check intersection of ray with three bounding box slabs
            EGS_Float tmin = 0.0;
            EGS_Float tmax = std::numeric_limits<EGS_Float>::max();
            std::array<EGS_Float, 3> p_vec {p.x, p.y, p.z};
            std::array<EGS_Float, 3> v_vec {v.x, v.y, v.z};
            std::array<EGS_Float, 3> mins {min_x, min_y, min_z};
            std::array<EGS_Float, 3> maxs {max_x, max_y, max_z};
            for (std::size_t i = 0; i < 3; i++) {
                // Parallel to slab. Point must be within slab bounds to hit
                // the bounding box
                if (std::abs(v_vec[i]) < eps) {
                    // Outside slab bounds
                    if (p_vec[i] < mins[i] || p_vec[i] > maxs[i]) { return 0; }
                } else {
                    // intersect ray with slab planes
                    EGS_Float inv_vel = 1.0 / v_vec[i];
                    EGS_Float t1 = (mins[i] - p_vec[i]) * inv_vel;
                    EGS_Float t2 = (maxs[i] - p_vec[i]) * inv_vel;
                    // convention is t1 is near plane, t2 is far plane
                    if (t1 > t2) { std::swap(t1, t2); }
                    tmin = std::max(tmin, t1);
                    tmax = std::min(tmax, t2);
                    if (tmin > tmax) { return 0; }
                }
            }
            q = p + v * tmin;
            dist = tmin;
            return 1;
        }

        // Given an interior point, return the minimum distance to a boundary.
        // This is a helper method for hownear, as the minimum of the boundary
        // distance and tetrahedron distance will be hownear's result.
        //
        // TODO maybe need to clamp to 0.0 here for results slightly under zero?
        EGS_Float min_interior_distance(const EGS_Vector& point) const {
            return std::min(point.x - min_x, std::min(point.y - min_y,
                   std::min(point.z - min_z, std::min(max_x - point.x,
                   std::min(max_y - point.y, max_z - point.z)))));
        }

        // Returns the closest point on the bounding box to the given point.
        // If the given point is inside the bounding box, it is considered the
        // closest point. This method is intended for use by hownear, to decide
        // where to search first.
        //
        // See section 5.1.3 Ericson.
        EGS_Vector closest_point(const EGS_Vector& point) const {
            std::array<EGS_Float, 3> p = {point.x, point.y, point.z};
            std::array<EGS_Float, 3> mins = {min_x, min_y, min_z};
            std::array<EGS_Float, 3> maxs = {max_x, max_y, max_z};
            // set q to p, then clamp it to min/max bounds as needed
            std::array<EGS_Float, 3> q = p;
            for (int i = 0; i < 3; i++) {
                if (p[i] < mins[i]) {
                    q[i] = mins[i];
                }
                if (p[i] > maxs[i]) {
                    q[i] = maxs[i];
                }
            }
            return EGS_Vector(q[0], q[1], q[2]);
        }

        bool contains(const EGS_Vector& point) const {
            // Inclusive at the lower bound, non-inclusive at the upper bound,
            // so points on the interface between two bounding boxes only belong
            // to one of them:
            //
            //  +---+---+
            //  |   x   |
            //  +---+---+
            //        ^ belongs here
            //
            return point.x >= min_x && point.x < max_x &&
                   point.y >= min_y && point.y < max_y &&
                   point.z >= min_z && point.z < max_z;
        }

        bool is_indivisible() const {
            // check if we're running up against precision limits
            return approx_eq(min_x, mid_x()) ||
                approx_eq(max_x, mid_x()) ||
                approx_eq(min_y, mid_y()) ||
                approx_eq(max_y, mid_y()) ||
                approx_eq(min_z, mid_z()) ||
                approx_eq(max_z, mid_z());

        }

        // Split into 8 equal octants. Octant numbering follows an S, i.e:
        //
        //        -z         +z
        //     +---+---+  +---+---+
        //     | 2 | 3 |  | 6 | 7 |
        //  y  +---+---+  +---+---+
        //  ^  | 0 | 1 |  | 4 | 5 |
        //  |  +---+---+  +---+---+
        //  + -- > x
        //
        std::array<BoundingBox, 8> divide8() const {
            return {
                BoundingBox (
                    min_x, mid_x(),
                    min_y, mid_y(),
                    min_z, mid_z()
                ),
                BoundingBox(
                    mid_x(), max_x,
                    min_y, mid_y(),
                    min_z, mid_z()
                ),
                BoundingBox(
                    min_x, mid_x(),
                    mid_y(), max_y,
                    min_z, mid_z()
                ),
                BoundingBox(
                    mid_x(), max_x,
                    mid_y(), max_y,
                    min_z, mid_z()
                ),
                BoundingBox(
                    min_x, mid_x(),
                    min_y, mid_y(),
                    mid_z(), max_z
                ),
                BoundingBox(
                    mid_x(), max_x,
                    min_y, mid_y(),
                    mid_z(), max_z
                ),
                BoundingBox(
                    min_x, mid_x(),
                    mid_y(), max_y,
                    mid_z(), max_z
                ),
                BoundingBox(
                    mid_x(), max_x,
                    mid_y(), max_y,
                    mid_z(), max_z
                )
            };
        }
    };
    struct Node {
        std::vector<int> elts_;
        std::vector<Node> children_;
        BoundingBox bbox_;

        Node() = default;
        // TODO: think about passing in EGS_Mesh as parameter
        Node(const std::vector<int> &elts, const BoundingBox& bbox,
            std::size_t n_max, const EGS_Mesh& mesh) : bbox_(bbox)
        {
            // TODO: max level and precision warning
            if (bbox_.is_indivisible() || elts.size() < n_max) {
                elts_ = elts;
                return;
            }

            std::array<std::vector<int>, 8> octants;
            std::array<BoundingBox, 8> bbs = bbox_.divide8();

            // elements may be in more than one bounding box
            for (const auto &e : elts) {
                for (int i = 0; i < 8; i++) {
                    if (bbs[i].intersects_tetrahedron(mesh.element_nodes(e))) {
                        octants[i].push_back(e);
                    }
                }
            }
            for (int i = 0; i < 8; i++) {
                children_.push_back(Node(
                    std::move(octants[i]), bbs[i], n_max, mesh
                ));
            }
        }

        bool isLeaf() const {
            return children_.empty();
        }

        void print(std::ostream& out, int level) const {
            out << "Level " << level << "\n";
            bbox_.print(out);
            if (children_.empty()) {
            out << "num_elts: " << elts_.size() << "\n";
                for (const auto& e: elts_) {
                    out << e << " ";
                }
                out << "\n";
                return;
            }
            for (int i = 0; i < 8; i++) {
                children_.at(i).print(out, level + 1);
            }
        }

        int findOctant(const EGS_Vector &p) const {
            // Our choice of octant ordering (see BoundingBox.divide8) means we
            // can determine the correct octant with three checks. E.g. octant 0
            // is (-x, -y, -z), octant 1 is (+x, -y, -z), octant 4 is (-x, -y, +z)
            // octant 7 is (+x, +y, +z), etc.
            std::size_t octant = 0;
            if (p.x >= bbox_.mid_x()) { octant += 1; };
            if (p.y >= bbox_.mid_y()) { octant += 2; };
            if (p.z >= bbox_.mid_z()) { octant += 4; };
            return octant;
        }

        // Octants are returned ordered by minimum intersection distance
        std::vector<int> findOtherIntersectedOctants(const EGS_Vector& p,
                const EGS_Vector& v, int exclude_octant) const
        {
            if (isLeaf()) {
                throw std::runtime_error(
                    "findOtherIntersectedOctants called on leaf node");
            }
            std::vector<std::pair<EGS_Float, int>> intersections;
            for (int i = 0; i < 8; i++) {
                if (i == exclude_octant) {
                    continue;
                }
                EGS_Vector intersection;
                EGS_Float dist;
                if (children_[i].bbox_.ray_intersection(p, v, dist, intersection)) {
                    intersections.push_back({dist, i});
                }
            }
            std::sort(intersections.begin(), intersections.end());
            std::vector<int> octants;
            for (const auto& i : intersections) {
                octants.push_back(i.second);
            }
            return octants;
        }

        // Leaf node: search all bounded elements, returning the minimum
        // distance to a boundary tetrahedron or a bounding box surface.
        EGS_Float hownear_leaf_search(const EGS_Vector& p, EGS_Mesh& mesh) const
        {
            const EGS_Float best_dist = bbox_.min_interior_distance(p);
            // Use squared distance to avoid computing square roots in the
            // loop. This has the added bonus of ridding ourselves of any
            // negatives from near-zero floating-point issues
            EGS_Float best_dist2 = best_dist * best_dist;
            for (const auto &e: elts_) {
                const auto& n = mesh.element_nodes(e);
                best_dist2 = std::min(best_dist2, distance2(p,
                    closest_point_tetrahedron(p, n.A, n.B, n.C, n.D)));
            }
            return std::sqrt(best_dist2);
        }

        EGS_Float hownear_exterior(const EGS_Vector& p, EGS_Mesh& mesh) const
        {
            // Leaf node: find a lower bound on the mesh exterior distance
            // closest distance
            if (isLeaf()) {
                return hownear_leaf_search(p, mesh);
            }
            // Parent node: decide which octant to search and descend the tree
            const auto octant = findOctant(p);
            return children_[octant].hownear_exterior(p, mesh);
        }

        // Does not mutate the EGS_Mesh.
        int isWhere(const EGS_Vector &p, /*const*/ EGS_Mesh &mesh) const {
            // Leaf node: search all bounded elements, returning -1 if the
            // element wasn't found.
            if (isLeaf()) {
                for (const auto &e: elts_) {
                    if (mesh.insideElement(e, p)) {
                        return e;
                    }
                }
                return -1;
            }

            // Parent node: decide which octant to search and descend the tree
            return children_[findOctant(p)].isWhere(p, mesh);
        }

        // TODO split into two functions
        int howfar_exterior(const EGS_Vector &p, const EGS_Vector &v,
            const EGS_Float &max_dist, EGS_Float &t, /* const */ EGS_Mesh& mesh)
            const
        {
            // Leaf node: check for intersection with any boundary elements
            EGS_Float min_dist = std::numeric_limits<EGS_Float>::max();
            int min_elt = -1;
            if (isLeaf()) {
                for (const auto &e: elts_) {
                    if (!mesh.is_boundary(e)) {
                        continue;
                    }
                    // closest_boundary_face only counts intersections where the
                    // point is on the outside of the face, when it's possible
                    // to intersect the boundary face directly
                    auto intersection = mesh.closest_boundary_face(e, p, v);
                    if (intersection.dist < min_dist) {
                        min_elt = e;
                        min_dist = intersection.dist;
                    }
                }
                t = min_dist;
                return min_elt; // min_elt may be -1 if there is no intersection
            }
            // Parent node: decide which octant to search and descend the tree
            EGS_Vector intersection;
            EGS_Float dist;
            auto hit = bbox_.ray_intersection(p, v, dist, intersection);
            // case 1: there's no intersection with this bounding box, return
            if (!hit) {
                return -1;
            }
            // case 2: we have a hit. Descend into the most likely intersecting
            // child octant's bounding box to find any intersecting elements
            auto octant = findOctant(intersection);
            auto elt = children_[octant].howfar_exterior(
                p, v, max_dist, t, mesh
            );
            // If we find a valid element, return it
            if (elt != -1) {
                return elt;
            }
            // Otherwise, if there was no intersection in the most likely
            // octant, examine the other octants that are intersected by
            // the ray:
            for (const auto& o : findOtherIntersectedOctants(p, v, octant)) {
                auto elt = children_[o].howfar_exterior(
                    p, v, max_dist, t, mesh
                );
                // If we find a valid element, return it
                if (elt != -1) {
                    return elt;
                }
            }
            return -1;
        }
    };

    Node root_;
public:
    EGS_Mesh_Octree() = default;
    EGS_Mesh_Octree(const std::vector<int> &elts, std::size_t n_max,
        const EGS_Mesh& mesh)
    {
        if (elts.empty()) {
            throw std::runtime_error("EGS_Mesh_Octree: empty elements vector");
        }
        std::cout << "making octree of " << elts.size() << " elements\n";
        if (elts.size() > std::numeric_limits<int>::max()) {
            throw std::runtime_error("EGS_Mesh_Octree: num elts must fit into an int");
        }

        const EGS_Float INF = std::numeric_limits<EGS_Float>::infinity();
        BoundingBox g_bounds(INF, -INF, INF, -INF, INF, -INF);
        for (const auto& e : elts) {
            const auto& nodes = mesh.element_nodes(e);
            g_bounds.min_x = std::min(g_bounds.min_x, tet_min_x(nodes));
            g_bounds.max_x = std::max(g_bounds.max_x, tet_max_x(nodes));
            g_bounds.min_y = std::min(g_bounds.min_y, tet_min_y(nodes));
            g_bounds.max_y = std::max(g_bounds.max_y, tet_max_y(nodes));
            g_bounds.min_z = std::min(g_bounds.min_z, tet_min_z(nodes));
            g_bounds.max_z = std::max(g_bounds.max_z, tet_max_z(nodes));
        }
        // Add a small delta around the bounding box to avoid numerical problems
        // at the boundary
        g_bounds.expand(1e-8);
        root_ = Node(elts, g_bounds, n_max, mesh);
    }

    int isWhere(const EGS_Vector& p, /*const*/ EGS_Mesh& mesh) const {
        if (!root_.bbox_.contains(p)) {
            return -1;
        }
        return root_.isWhere(p, mesh);
    }

    void print(std::ostream& out) const {
        root_.print(out, 0);
    }

    int howfar_exterior(const EGS_Vector &p, const EGS_Vector &v,
        const EGS_Float &max_dist, EGS_Float &t, EGS_Mesh& mesh) const
    {
        EGS_Vector intersection;
        EGS_Float dist;
        auto hit = root_.bbox_.ray_intersection(p, v, dist, intersection);
        if (!hit || dist > max_dist) {
            return -1;
        }
        return root_.howfar_exterior(p, v, max_dist, t, mesh);
    }

    // Returns a lower bound on the distance to the mesh exterior boundary.
    // The actual distance to the mesh may be larger, i.e. a distance to an
    // axis-aligned bounding box might be returned instead. This is allowed by
    // the HOWNEAR spec, PIRS-701 section 3.6, "Specifications for HOWNEAR":
    //
    // > In complex geometries, the mathematics of HOWNEAR can become difficult
    // and sometimes almost impossible! If it is easier for the user to
    // compute some lower bound to the nearest distance, this could be used...
    EGS_Float hownear_exterior(const EGS_Vector& p, EGS_Mesh& mesh) const {
        // If the point is outside the octree bounding box, return the distance
        // to the bounding box.
        if (!root_.bbox_.contains(p)) {
            return distance(root_.bbox_.closest_point(p), p);
        }
        // Otherwise, descend the octree
        return root_.hownear_exterior(p, mesh);
    }
};

// msh4.1 parsing
//
// TODO parse into MeshSpec struct instead of EGS_Mesh directly
// to better delineate errors
EGS_Mesh* EGS_Mesh::parse_msh_file(std::istream& input) {
    auto version = msh_parser::internal::parse_msh_version(input);
    // TODO auto mesh_data;
    switch(version) {
        case msh_parser::internal::MshVersion::v41:
            try {
                return parse_msh41_body(input);
            } catch (const std::runtime_error& err) {
                throw std::runtime_error("msh 4.1 parsing failed\n" + std::string(err.what()));
            }
            break;
    }
    throw std::runtime_error("couldn't parse msh file");
}

EGS_Mesh::EGS_Mesh(std::vector<EGS_Mesh::Tetrahedron> elements,
    std::vector<EGS_Mesh::Node> nodes, std::vector<EGS_Mesh::Medium> materials)
        : EGS_BaseGeometry(EGS_BaseGeometry::getUniqueName())
{
    initializeElements(std::move(elements), std::move(nodes), std::move(materials));
    initializeNeighbours();
    initializeOctrees();
    initializeNormals();
}

void EGS_Mesh::initializeElements(std::vector<EGS_Mesh::Tetrahedron> elements,
    std::vector<EGS_Mesh::Node> nodes, std::vector<EGS_Mesh::Medium> materials)
{
    std::size_t n_max = std::numeric_limits<int>::max();
    if (elements.size() >= n_max) {
        throw std::runtime_error("maximum number of elements (" +
            std::to_string(n_max) + ") exceeded (" +
                std::to_string(elements.size()) + ")");
    }
    if (nodes.size() >= n_max) {
        throw std::runtime_error("maximum number of nodes (" +
            std::to_string(n_max) + ") exceeded (" +
                std::to_string(nodes.size()) + ")");
    }
    EGS_BaseGeometry::nreg = elements.size();

    _elt_tags.reserve(elements.size());
    _elt_node_indices.reserve(elements.size());
    _nodes.reserve(nodes.size());

    std::unordered_map<int, int> node_map;
    node_map.reserve(nodes.size());
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
        const auto& n = nodes[i];
        node_map.insert({n.tag, i});
        _nodes.push_back(EGS_Vector(n.x, n.y, n.z));
    }
    if (node_map.size() != nodes.size()) {
        throw std::runtime_error("duplicate nodes in node list");
    }
    // Find the matching node indices for every tetrahedron
    auto find_node = [&](int node_tag) -> int {
        auto node_it = node_map.find(node_tag);
        if (node_it == node_map.end()) {
            throw std::runtime_error("No mesh node with tag: " + std::to_string(node_tag));
        }
        return node_it->second;
    };
    for (int i = 0; i < static_cast<int>(elements.size()); i++) {
        const auto& e = elements[i];
        _elt_tags.push_back(e.tag);
        _elt_node_indices.push_back({
            find_node(e.a), find_node(e.b), find_node(e.c), find_node(e.d)
        });
    }

    // map from medium tags to offsets
    std::unordered_map<int, int> medium_offsets;
    for (std::size_t i = 0; i < materials.size(); i++) {
        _medium_names.push_back(materials[i].medium_name);
        auto material_tag = materials[i].tag;
        bool inserted = medium_offsets.insert({material_tag, i}).second;
        if (!inserted) {
            throw std::runtime_error("duplicate medium tag: " + std::to_string(material_tag));
        }
    }

    _medium_indices.reserve(elements.size());
    for (const auto& e: elements) {
        // TODO handle vacuum tag (-1)?
        _medium_indices.push_back(medium_offsets.at(e.medium_tag));
    }
}

void EGS_Mesh::initializeNeighbours() {
    std::vector<mesh_neighbours::Tetrahedron> neighbour_elts;
    neighbour_elts.reserve(num_elements());
    for (const auto& e: _elt_node_indices) {
        neighbour_elts.emplace_back(mesh_neighbours::Tetrahedron(e[0], e[1], e[2], e[3]));
    }
    _neighbours = mesh_neighbours::tetrahedron_neighbours(neighbour_elts);

    _boundary_faces.reserve(num_elements() * 4);
    for (const auto& ns: _neighbours) {
        for (const auto& n: ns) {
            _boundary_faces.push_back(n == mesh_neighbours::NONE);
        }
    }
}

void EGS_Mesh::initializeNormals() {
    _face_normals.reserve(num_elements());
    for (int i = 0; i < static_cast<int>(num_elements()); i++) {
        auto get_normal = [](const EGS_Vector& a, const EGS_Vector& b,
            const EGS_Vector& c, const EGS_Vector& d) -> EGS_Vector
        {
            EGS_Vector normal = cross(b - a, c - a);
            normal.normalize();
            if (dot(normal, d-a) < 0) {
                normal *= -1.0;
            }
            return normal;
        };
        const auto& n = element_nodes(i);
        _face_normals.push_back({
            get_normal(n.B, n.C, n.D, n.A),
            get_normal(n.A, n.C, n.D, n.B),
            get_normal(n.A, n.B, n.D, n.C),
            get_normal(n.A, n.B, n.C, n.D)
        });
    }
}

void EGS_Mesh::initializeOctrees() {
    std::vector<int> elts;
    std::vector<int> boundary_elts;
    elts.reserve(num_elements());
    for (int i = 0; i < num_elements(); i++) {
        elts.push_back(i);
        if (is_boundary(i)) {
            boundary_elts.push_back(i);
        }
    }
    std::cout << "before making octree\n";
    // Max element sizes from Furuta et al section 2.1.1
    std::size_t n_vol = 200;
    _volume_tree = std::unique_ptr<EGS_Mesh_Octree>(
        new EGS_Mesh_Octree(elts, n_vol, *this)
    );
    std::size_t n_surf = 100;
    _surface_tree = std::unique_ptr<EGS_Mesh_Octree>(
        new EGS_Mesh_Octree(boundary_elts, n_surf, *this)
    );
    std::cout << "after making octree\n";
}

bool EGS_Mesh::isInside(const EGS_Vector &x) {
    return isWhere(x) != -1;
}

int EGS_Mesh::inside(const EGS_Vector &x) {
    return isInside(x) ? 0 : -1;
}

int EGS_Mesh::medium(int ireg) const {
    return _medium_indices.at(ireg);
}

bool EGS_Mesh::insideElement(int i, const EGS_Vector &x) /* const */ {
    const auto& n = element_nodes(i);
    if (point_outside_of_plane(x, n.A, n.B, n.C, n.D)) {
        return false;
    }
    if (point_outside_of_plane(x, n.A, n.C, n.D, n.B)) {
        return false;
    }
    if (point_outside_of_plane(x, n.A, n.B, n.D, n.C)) {
        return false;
    }
    if (point_outside_of_plane(x, n.B, n.C, n.D, n.A)) {
        return false;
    }
    return true;
}

int EGS_Mesh::isWhere(const EGS_Vector &x) {
    return _volume_tree->isWhere(x, *this);
}

EGS_Float EGS_Mesh::hownear(int ireg, const EGS_Vector& x) {
    if (ireg > 0 && ireg > num_elements() - 1) {
        throw std::runtime_error("ireg " + std::to_string(ireg) + " out of bounds for mesh with " + std::to_string(num_elements()) + " regions");
    }
    // inside
    if (ireg >= 0) {
        return min_interior_face_dist(ireg, x);
    }
    // outside
    return min_exterior_face_dist(x);
}

// Assumes the input normal is normalized. Returns the absolute value of the
// distance.
EGS_Float distance_to_plane(const EGS_Vector &x,
    const EGS_Vector& unit_plane_normal, const EGS_Vector& plane_point)
{
    return std::abs(dot(unit_plane_normal, x - plane_point));
}

EGS_Float EGS_Mesh::min_interior_face_dist(int ireg, const EGS_Vector& x) {
    const auto& n = element_nodes(ireg);

    // First face is BCD, second is ACD, third is ABD, fourth is ABC
    EGS_Float min_dist = distance_to_plane(x, _face_normals[ireg][0], n.B);
    min_dist = std::min(min_dist,
        distance_to_plane(x, _face_normals[ireg][1], n.A));
    min_dist = std::min(min_dist,
        distance_to_plane(x, _face_normals[ireg][2], n.A));
    min_dist = std::min(min_dist,
        distance_to_plane(x, _face_normals[ireg][3], n.A));

    return min_dist;
}

EGS_Float EGS_Mesh::min_exterior_face_dist(const EGS_Vector& x) {
    return _surface_tree->hownear_exterior(x, *this);
}

int EGS_Mesh::howfar(int ireg, const EGS_Vector &x, const EGS_Vector &u,
    EGS_Float &t, int *newmed /* =0 */, EGS_Vector *normal /* =0 */)
{
    if (ireg < 0) {
        return howfar_exterior(ireg, x, u, t, newmed, normal);
    }
    return howfar_interior(ireg, x, u, t, newmed, normal);
}

// howfar_interior is the most complicated EGS_BaseGeometry method. Apart from
// the intersection logic, there are exceptional cases that must be carefully
// handled. In particular, the region number and the position `x` may not agree.
// For example, the region may be 1, but the position x may be slightly outside
// of region 1 because of numerical undershoot. The region number takes priority
// because it is where EGS thinks the particle should be based on the simulation
// so far, and we assume steps like boundary crossing calculations have already
// taken place. So we have to do our best to calculate intersections as if the
// position really is inside the given tetrahedron. There are three cases:
//
// 1. The position is inside the region: calculate the intersection without
// further complications.
// 2. The position is outside the region but will intersect one of the interior
// faces: calculate the intersection, ignoring any backwards facing faces.
// 3. The position is outside the region and won't intersect any of the interior
// faces: return 0.0 as the howfar distance and set the new region number to
// match the position.
//
//     Case 1      |        Case 2       |        Case 3
//                 |                     |
//       /\        |          /\         |          /\
//      /  \       |         /  \        |         /  \
//     /    \      |        /    \       |        /    \
//    / * -> X     |  * -> /      X      |  <- * /      \
//   /________\    |      /________\     |      /________\
//                 |                     |
//  Intersection   |     Intersection    |    No intersection, return 0.0
//
// This is the recommended way to implement howfar following Bielajew's "HOWFAR
// and HOWNEAR: Geometry Modelling for Monte Carlo Particle Transport" (see
// section 2, "Boundary Crossing" and section 4, "Solutions for simple
// surfaces").
//
// Case 1 and 2 are currently both handled the same way: calculate triangle-ray
// intersections with the subset of the surface triangles facing the query point
// and return the first intersection. Unlike calculating the distance to face
// planes, we can return immediately after finding an intersection because there
// should not be a smaller intersection distance.
//
// TODO: check whether using ray-plane intersections for Case 1 is faster.
int EGS_Mesh::howfar_interior(int ireg, const EGS_Vector &x, const EGS_Vector &u,
    EGS_Float &t, int *newmed, EGS_Vector *normal)
{
    // Do we have to reset howfar for this particle? Set false if the particle
    // will intersect a tetrahedron face as if it was inside the tetrahedron.
    bool is_lost = true;
    EGS_Float dist = 1e30;

    const auto& n = element_nodes(ireg);
    std::array<std::array<EGS_Vector, 3>, 4> face_nodes {{
        {n.B, n.C, n.D}, {n.A, n.C, n.D}, {n.A, n.B, n.D}, {n.A, n.B, n.C}
    }};

    for (std::size_t i = 0; i < 4; i++) {
        if (!(dot(_face_normals[ireg][i], x - face_nodes[i][0]) >= 0.0 &&
            interior_triangle_ray_intersection(x, u, _face_normals[ireg][i],
                face_nodes[i][0], face_nodes[i][1], face_nodes[i][2], dist)))
        {
            // no intersection with this triangle
            continue;
        }

        // intersection found
        is_lost = false;
        if (dist > t) {
            continue;
        }

        if (dist <= EGS_BaseGeometry::halfBoundaryTolerance) {
            // if a point is within the thick plane, the distance to the next
            // region is exactly 0.0
            dist = 0.0;
        }

        t = dist;
        int newreg = _neighbours[ireg][i];
        update_medium(newreg, newmed);
        update_normal(_face_normals[ireg][i], u, normal);
        return newreg;
    }
    // If the particle isn't lost but won't intersect a boundary because it's
    // too far away, return the current region.
    if (!is_lost) {
        return ireg;
    }
    // If the particle is not where ireg says and there is no intersection with
    // any triangle face, we are in a situation like this (hopefully from
    // numerical undershoot during transport).
    //
    //         /\
    //   <- * /  \
    //       /____\
    //
    // Protocol is to set the intersection distance to 0.0 and return the region
    // where the particle is numerically.
    t = 0.0;
    int newreg = howfar_interior_find_lost_particle(ireg, x, u);
    update_medium(newreg, newmed);
    // We can't determine which normal to display (which is only for egs_view in
    // any case), so we don't update the normal for this exceptional case.
    return newreg;
}

// Determine where the lost particle from hownear_interior is numerically.
//
//         /\
//   <- * /  \
//       /____\
//
//  ^^^^ i.e., which region is this particle actually in?
int EGS_Mesh::howfar_interior_find_lost_particle(int ireg, const EGS_Vector &x,
    const EGS_Vector &u)
{
    // If a particle is slightly outside the bounds of an element, it will most
    // likely be in a neighbouring element, so check those first.
    for (const auto& neighbour : _neighbours[ireg]) {
        if (neighbour == -1) {
            continue;
        }
        if (insideElement(neighbour, x)) {
            return neighbour;
        }
    }
    // If the particle is not in a neighbouring element, use isWhere to find out
    // where it should be. If isWhere returns the current region, that is a
    // serious problem in the implementation (infinite loop), so crash. We could
    // also consider issuing a warning and discarding the particle.
    int newreg = isWhere(x);
    if (newreg == ireg) {
        egsFatal("EGS_Mesh::howfar: infinite loop detected in region %d\n"
                 "x=(%.17g,%.17g,%.17g) u=(%.17g,%.17g,%.17g)\n", ireg, x.x,
                 x.y, x.z, u.x, u.y, u.z);
    }
    return newreg;
}

EGS_Mesh::Intersection EGS_Mesh::closest_boundary_face(int ireg, const EGS_Vector &x,
    const EGS_Vector &u)
{
    assert(is_boundary(ireg));
    EGS_Float min_dist = std::numeric_limits<EGS_Float>::max();

    auto dist = min_dist;
    auto closest_face = -1;

    auto check_face_intersection = [&](int face, const EGS_Vector& A, const EGS_Vector& B,
            const EGS_Vector& C, const EGS_Vector& D)
    {
        if (_boundary_faces[4*ireg + face] &&
            // check if the point is on the outside looking in (rather than just
            // clipping the edge of a boundary face)
            point_outside_of_plane(x, A, B, C, D) &&
            dot(_face_normals[ireg][face], u) > 0.0 && // point might be in a thick plane
            exterior_triangle_ray_intersection(x, u, A, B, C, dist) &&
            dist < min_dist)
        {
            min_dist = dist;
            closest_face = face;
        }
    };

    const auto& n = element_nodes(ireg);
    // face 0 (BCD), face 1 (ACD) etc.
    check_face_intersection(0, n.B, n.C, n.D, n.A);
    check_face_intersection(1, n.A, n.C, n.D, n.B);
    check_face_intersection(2, n.A, n.B, n.D, n.C);
    check_face_intersection(3, n.A, n.B, n.C, n.D);

    return EGS_Mesh::Intersection(min_dist, closest_face);
}

int EGS_Mesh::howfar_exterior(int ireg, const EGS_Vector &x, const EGS_Vector &u,
    EGS_Float &t, int *newmed, EGS_Vector *normal)
{
    EGS_Float min_dist = 1e30;
    auto min_reg = _surface_tree->howfar_exterior(x, u, t, min_dist, *this);

    // no intersection
    if (min_dist > t || min_reg == -1) {
        return -1;
    }

    // intersection found, update out parameters
    t = min_dist;
    if (newmed) {
        *newmed = medium(min_reg);
    }
    if (normal) {
        EGS_Vector tmp_normal;
        const auto& n = element_nodes(min_reg);
        auto intersection = closest_boundary_face(min_reg, x, u);
        switch(intersection.face_index) {
            case 0: tmp_normal = cross(n.C - n.B, n.D - n.B); break;
            case 1: tmp_normal = cross(n.C - n.A, n.D - n.A); break;
            case 2: tmp_normal = cross(n.B - n.A, n.D - n.A); break;
            case 3: tmp_normal = cross(n.B - n.A, n.C - n.A); break;
            default: throw std::runtime_error("Bad intersection, got face index: " +
                std::to_string(intersection.face_index));
        }
        // egs++ convention is normal pointing opposite view ray
        if (dot(tmp_normal, u) > 0) {
            tmp_normal = -1.0 * tmp_normal;
        }
        tmp_normal.normalize();
        *normal = tmp_normal;
    }
    return min_reg;
}

// TODO deduplicate
static char EGS_MESH_LOCAL geom_class_msg[] = "createGeometry(EGS_Mesh): %s\n";
const std::string EGS_Mesh::type = "EGS_Mesh";

void EGS_Mesh::printInfo() const {
    EGS_BaseGeometry::printInfo();
    std::ostringstream oss;
    printElement(0, oss);
    egsInformation(oss.str().c_str());
}

extern "C" {
    EGS_MESH_EXPORT EGS_BaseGeometry *createGeometry(EGS_Input *input) {
        if (!input) {
            egsWarning("createGeometry(EGS_Mesh): null input\n");
            return nullptr;
        }
        std::string mesh_file;
        int err = input->getInput("file", mesh_file);
        if (err) {
            egsWarning("createGeometry(EGS_Mesh): no mesh file key `file` in input\n");
            return nullptr;
        }
        if (!(mesh_file.length() >= 4 && mesh_file.rfind(".msh") == mesh_file.length() - 4)) {
            egsWarning("createGeometry(EGS_Mesh): unknown file extension for file `%s`,"
                "only `.msh` is allowed\n", mesh_file.c_str());
            return nullptr;
        }
        std::ifstream input_file(mesh_file);
        if (!input_file) {
            egsWarning("createGeometry(EGS_Mesh): unable to open file: `%s`\n"
                "\thelp => try using the absolute path to the mesh file",
                mesh_file.c_str());
            return nullptr;
        }
        EGS_Mesh* mesh = EGS_Mesh::parse_msh_file(input_file);
        if (!mesh) {
            egsWarning("createGeometry(EGS_Mesh): Gmsh msh file parsing failed\n");
            return nullptr;
        }
        mesh->setFilename(mesh_file);
        mesh->setBoundaryTolerance(input);
        mesh->setName(input);
        mesh->setLabels(input);
        for (const auto& medium: mesh->medium_names()) {
            mesh->addMedium(medium);
        }
        return mesh;
    }
}

