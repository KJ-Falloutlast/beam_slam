#include <bs_constraints/global/absolute_constraint.h>

#include <pluginlib/class_list_macros.h>
#include <boost/serialization/export.hpp>

BOOST_CLASS_EXPORT_IMPLEMENT(bs_constraints::global::AbsoluteVelocityAngular3DStampedConstraint);
BOOST_CLASS_EXPORT_IMPLEMENT(bs_constraints::global::AbsoluteVelocityLinear3DStampedConstraint);
BOOST_CLASS_EXPORT_IMPLEMENT(bs_constraints::global::AbsoluteAccelerationLinear3DStampedConstraint);
BOOST_CLASS_EXPORT_IMPLEMENT(bs_constraints::global::AbsoluteGyroBias3DStampedConstraint);
BOOST_CLASS_EXPORT_IMPLEMENT(bs_constraints::global::AbsoluteAccelBias3DStampedConstraint);

PLUGINLIB_EXPORT_CLASS(bs_constraints::global::AbsoluteVelocityAngular3DStampedConstraint, fuse_core::Constraint);
PLUGINLIB_EXPORT_CLASS(bs_constraints::global::AbsoluteVelocityLinear3DStampedConstraint, fuse_core::Constraint);
PLUGINLIB_EXPORT_CLASS(bs_constraints::global::AbsoluteAccelerationLinear3DStampedConstraint, fuse_core::Constraint);
PLUGINLIB_EXPORT_CLASS(bs_constraints::global::AbsoluteGyroBias3DStampedConstraint, fuse_core::Constraint);
PLUGINLIB_EXPORT_CLASS(bs_constraints::global::AbsoluteAccelBias3DStampedConstraint, fuse_core::Constraint);