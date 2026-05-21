// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
#ifndef TF2__LINEAR_MATH__QUATERNION_H_
#define TF2__LINEAR_MATH__QUATERNION_H_
#include <cmath>
namespace tf2
{
class Quaternion
{
public:
  Quaternion() = default;
  Quaternion(double x, double y, double z, double w) : x_(x), y_(y), z_(z), w_(w) {}
  void setRPY(double r, double p, double y) {
    const double cr = std::cos(r / 2), sr = std::sin(r / 2);
    const double cp = std::cos(p / 2), sp = std::sin(p / 2);
    const double cy = std::cos(y / 2), sy = std::sin(y / 2);
    w_ = cr * cp * cy + sr * sp * sy;
    x_ = sr * cp * cy - cr * sp * sy;
    y_ = cr * sp * cy + sr * cp * sy;
    z_ = cr * cp * sy - sr * sp * cy;
  }
  double x() const { return x_; }
  double y() const { return y_; }
  double z() const { return z_; }
  double w() const { return w_; }
private:
  double x_{0}, y_{0}, z_{0}, w_{1};
};
}  // namespace tf2
#endif  // TF2__LINEAR_MATH__QUATERNION_H_
