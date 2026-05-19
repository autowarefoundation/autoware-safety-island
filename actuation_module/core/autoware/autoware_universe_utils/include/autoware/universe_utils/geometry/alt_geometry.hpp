// Copyright 2020-2024 Tier IV, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef AUTOWARE__UNIVERSE_UTILS__GEOMETRY__ALT_GEOMETRY_HPP_
#define AUTOWARE__UNIVERSE_UTILS__GEOMETRY__ALT_GEOMETRY_HPP_

// Libs
#include <cmath>
#include <list>
#include <optional>
#include <utility>
#include <vector>

#include <Eigen/Core>

namespace autoware::universe_utils {

/**
 * @brief A 2D point class that represents a point in 2D space
 */
class Vector2d {
public:
  Vector2d() : x_(0.0), y_(0.0) {}
  Vector2d(const double x, const double y) : x_(x), y_(y) {}

  // TODO: autoware::universe_utils::Point2d  ??
  // explicit Vector2d(const autoware::universe_utils::Point2d & point) : x_(point.x()), y_(point.y())
  // {
  // }

  double cross(const Vector2d & other) const { return x_ * other.y() - y_ * other.x(); }
  double dot(const Vector2d & other) const { return x_ * other.x() + y_ * other.y(); }
  double norm2() const { return x_ * x_ + y_ * y_; }
  double norm() const { return std::sqrt(norm2()); }

  Vector2d vector_triple(const Vector2d & v1, const Vector2d & v2) const
  {
    const auto tmp = this->cross(v1);
    return {-v2.y() * tmp, v2.x() * tmp};
  }

  const double& x() const { return x_; }
  double& x() { return x_; }
  const double& y() const { return y_; }
  double& y() { return y_; }
private:
  double x_;
  double y_;
};

inline Vector2d operator+(const Vector2d & v1, const Vector2d & v2)
{
  return {v1.x() + v2.x(), v1.y() + v2.y()};
}

inline Vector2d operator-(const Vector2d & v1, const Vector2d & v2)
{
  return {v1.x() - v2.x(), v1.y() - v2.y()};
}

inline Vector2d operator-(const Vector2d & v)
{
  return {-v.x(), -v.y()};
}

inline Vector2d operator*(const double & s, const Vector2d & v)
{
  return {s * v.x(), s * v.y()};
}

// TODO: check if this is correct
inline bool operator==(const Vector2d & v1, const Vector2d & v2)
{
  // Use epsilon-based comparison for floating-point values
  const double epsilon = 1e-9;  // Small tolerance value
  return std::abs(v1.x() - v2.x()) < epsilon && std::abs(v1.y() - v2.y()) < epsilon;
}

/**
 * @brief A 3D point class that represents a point in 3D space
 */
class Vector3d {
public:
  Vector3d() : x_(0.0), y_(0.0), z_(0.0) {}
  Vector3d(const double x, const double y, const double z) : x_(x), y_(y), z_(z) {}
  Vector3d(const Vector2d & v, const double z) : x_(v.x()), y_(v.y()), z_(z) {}

  Vector3d cross(const Vector3d & other) const { 
    return {y_ * other.z() - z_ * other.y(), 
            z_ * other.x() - x_ * other.z(), 
            x_ * other.y() - y_ * other.x()}; 
  }
  double dot(const Vector3d & other) const { return x_ * other.x() + y_ * other.y() + z_ * other.z(); }
  double norm2() const { return x_ * x_ + y_ * y_ + z_ * z_; }
  double norm() const { return std::sqrt(norm2()); }
  double length() const { return std::sqrt(length2()); }
  double length2() const { return x_ * x_ + y_ * y_ + z_ * z_; }

  const double& x() const { return x_; }
  double& x() { return x_; }
  const double& y() const { return y_; }
  double& y() { return y_; }
  const double& z() const { return z_; }
  double& z() { return z_; }
private:
  double x_;
  double y_;
  double z_;
};

inline Vector3d operator+(const Vector3d & v1, const Vector3d & v2)
{
  return {v1.x() + v2.x(), v1.y() + v2.y(), v1.z() + v2.z()};
}

inline Vector3d operator-(const Vector3d & v1, const Vector3d & v2)
{
  return {v1.x() - v2.x(), v1.y() - v2.y(), v1.z() - v2.z()};
}

inline Vector3d operator-(const Vector3d & v)
{
  return {-v.x(), -v.y(), -v.z()};
}

inline Vector3d operator*(const double & s, const Vector3d & v)
{
  return {s * v.x(), s * v.y(), s * v.z()};
}

inline bool operator==(const Vector3d & v1, const Vector3d & v2)
{
  const double epsilon = 1e-9;  // Small tolerance value
  return std::abs(v1.x() - v2.x()) < epsilon && 
         std::abs(v1.y() - v2.y()) < epsilon && 
         std::abs(v1.z() - v2.z()) < epsilon;
}

// We use Vector2d to represent points, but we do not name the class Point2d directly
// as it has some vector operation functions.
using Point2d = Vector2d;
using Points2d = std::vector<Point2d>;
using PointList2d = std::list<Point2d>;

/**
 * @brief A polygon class that represents a polygon
 */
class Polygon2d {
public:
  static std::optional<Polygon2d> create(const PointList2d& outer, const std::vector<PointList2d>& inners) noexcept;
  static std::optional<Polygon2d> create(PointList2d&& outer, std::vector<PointList2d>&& inners) noexcept;
  // TODO: autoware::universe_utils::Polygon2d  ??
  static std::optional<Polygon2d> create(const Polygon2d& polygon) noexcept;

  const PointList2d& outer() const noexcept { return outer_; }
  PointList2d& outer() noexcept { return outer_; }
  const std::vector<PointList2d>& inners() const noexcept { return inners_; }
  std::vector<PointList2d>& inners() noexcept { return inners_; }

protected:
  Polygon2d(const PointList2d & outer, const std::vector<PointList2d> & inners)
  : outer_(outer), inners_(inners)
  {
  }

  Polygon2d(PointList2d && outer, std::vector<PointList2d> && inners)
  : outer_(std::move(outer)), inners_(std::move(inners))
  {
  }

  PointList2d outer_;
  std::vector<PointList2d> inners_;
};

/**
 * @brief A convex polygon class that represents a convex polygon
 */
class ConvexPolygon2d : public Polygon2d {
public:
  static std::optional<ConvexPolygon2d> create(const PointList2d& vertices) noexcept;
  static std::optional<ConvexPolygon2d> create(PointList2d&& vertices) noexcept;
  // TODO: autoware::universe_utils::Polygon2d  ??
  static std::optional<ConvexPolygon2d> create(const Polygon2d& polygon) noexcept;

  const PointList2d& vertices() const noexcept { return outer(); }
  PointList2d& vertices() noexcept { return outer(); }

private:
  explicit ConvexPolygon2d(const PointList2d& vertices) : Polygon2d(vertices, {}) {}
  explicit ConvexPolygon2d(PointList2d&& vertices) : Polygon2d(std::move(vertices), {}) {}
};

/**
 * @brief Calculate the area of a convex polygon
 * @param poly The convex polygon to calculate the area of
 * @return The area of the polygon
 */
double area(const ConvexPolygon2d& poly);

/**
 * @brief Calculate the convex hull of a set of points
 * @param points The points to calculate the convex hull of
 * @return The convex hull of the points
 */
std::optional<ConvexPolygon2d> convex_hull(const Points2d& points);

/**
 * @brief Correct a polygon
 * @param poly The polygon to correct
 */
void correct(Polygon2d& poly);

/**
 * @brief Check if a point is covered by a convex polygon
 * @param point The point to check
 * @param poly The convex polygon to check against
 */
bool covered_by(const Point2d& point, const ConvexPolygon2d& poly);

/**
 * @brief Check if two convex polygons are disjoint
 * @param poly1 The first convex polygon
 * @param poly2 The second convex polygon
 * @return true if the polygons are disjoint, false otherwise
 */
bool disjoint(const ConvexPolygon2d& poly1, const ConvexPolygon2d& poly2);

/**
 * @brief Calculate the distance between a point and a segment
 * @param point The point to calculate the distance to
 * @param seg_start The start point of the segment
 * @param seg_end The end point of the segment
 * @return The distance between the point and the segment
 */
double distance(
  const Point2d & point, const Point2d& seg_start, const Point2d& seg_end);

/**
 * @brief Calculate the distance between a point and a convex polygon
 * @param point The point to calculate the distance to
 * @param poly The convex polygon to calculate the distance to
 * @return The distance between the point and the polygon
 */
double distance(const Point2d & point, const ConvexPolygon2d& poly);

/**
 * @brief Calculate the envelope of a polygon
 * @param poly The polygon to calculate the envelope of
 * @return The envelope of the polygon
 */
std::optional<ConvexPolygon2d> envelope(const Polygon2d& poly);

/**
 * @brief Check if two points are equal
 * @param point1 The first point
 * @param point2 The second point
 * @return true if the points are equal, false otherwise
 */
bool equals(const Point2d& point1, const Point2d& point2);

/**
 * @brief Check if two polygons are equal
 * @param poly1 The first polygon
 * @param poly2 The second polygon
 * @return true if the polygons are equal, false otherwise
 */
bool equals(const Polygon2d& poly1, const Polygon2d& poly2);

/**
 * @brief Find the farthest point from a segment
 * @param points The points to find the farthest point from
 * @param seg_start The start point of the segment
 * @param seg_end The end point of the segment
 * @return The farthest point from the segment
 */
Points2d::const_iterator find_farthest(
  const Points2d& points, const Point2d& seg_start, const Point2d& seg_end);

/**
 * @brief Check if two segments intersect
 * @param seg1_start The start point of the first segment
 * @param seg1_end The end point of the first segment
 * @param seg2_start The start point of the second segment
 * @param seg2_end The end point of the second segment
 * @return true if the segments intersect, false otherwise
 */
bool intersects(const Point2d& seg1_start, const Point2d& seg1_end, const Point2d& seg2_start, const Point2d& seg2_end);

/**
 * @brief Check if two convex polygons intersect
 * @param poly1 The first convex polygon
 * @param poly2 The second convex polygon
 * @return true if the polygons intersect, false otherwise
 */
bool intersects(const ConvexPolygon2d & poly1, const ConvexPolygon2d & poly2);

/**
 * @brief Check if a point is above a segment
 * @param point The point to check
 * @param seg_start The start point of the segment
 * @param seg_end The end point of the segment
 * @return true if the point is above the segment, false otherwise
 */
bool is_above(const Point2d& point, const Point2d& seg_start, const Point2d& seg_end);

/**
 * @brief Check if a polygon is clockwise
 * @param vertices The vertices of the polygon
 * @return true if the polygon is clockwise, false otherwise
 */
bool is_clockwise(const PointList2d& vertices);

/**
 * @brief Check if a polygon is convex
 * @param poly The polygon to check
 * @return true if the polygon is convex, false otherwise
 */
bool is_convex(const Polygon2d& poly);

/**
 * @brief Simplify a polygon
 * @param line The polygon to simplify
 * @param max_distance The maximum distance between points
 * @return The simplified polygon
 */
PointList2d simplify(const PointList2d& line, const double max_distance);

/**
 * @brief Check if a point touches a segment
 * @param point The point to check
 * @param seg_start The start point of the segment
 * @param seg_end The end point of the segment
 * @return true if the point touches the segment, false otherwise
 */
bool touches(
  const Point2d & point, const Point2d & seg_start, const Point2d & seg_end);

/**
 * @brief Check if a point touches a convex polygon
 * @param point The point to check
 * @param poly The convex polygon to check against
 * @return true if the point touches the polygon, false otherwise
 */
bool touches(const Point2d & point, const ConvexPolygon2d & poly);

/**
 * @brief Check if a point is within a convex polygon
 * @param point The point to check
 * @param poly The convex polygon to check against
 * @return true if the point is within the polygon, false otherwise
 */
bool within(const Point2d & point, const ConvexPolygon2d & poly);

/**
 * @brief Check if a convex polygon is within another convex polygon
 * @param poly_contained The convex polygon to check
 * @param poly_containing The convex polygon to check against
 * @return true if the polygon is within the other polygon, false otherwise
 */
bool within(
  const ConvexPolygon2d& poly_contained, const ConvexPolygon2d& poly_containing);

} // namespace autoware::universe_utils

#endif  // AUTOWARE__UNIVERSE_UTILS__GEOMETRY__ALT_GEOMETRY_HPP_
