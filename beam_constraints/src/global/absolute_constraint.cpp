#include <beam_constraints/global/absolute_constraint.h>

#include <pluginlib/class_list_macros.h>
#include <boost/serialization/export.hpp>

BOOST_CLASS_EXPORT_IMPLEMENT(beam_constraints::global::AbsoluteVelocityAngular3DStampedConstraint);
BOOST_CLASS_EXPORT_IMPLEMENT(beam_constraints::global::AbsoluteVelocityLinear3DStampedConstraint);
BOOST_CLASS_EXPORT_IMPLEMENT(beam_constraints::global::AbsoluteAccelerationLinear3DStampedConstraint);
BOOST_CLASS_EXPORT_IMPLEMENT(beam_constraints::global::AbsoluteImuBiasGyro3DStampedConstraint);
BOOST_CLASS_EXPORT_IMPLEMENT(beam_constraints::global::AbsoluteImuBiasAccel3DStampedConstraint);

PLUGINLIB_EXPORT_CLASS(beam_constraints::global::AbsoluteVelocityAngular3DStampedConstraint, fuse_core::Constraint);
PLUGINLIB_EXPORT_CLASS(beam_constraints::global::AbsoluteVelocityLinear3DStampedConstraint, fuse_core::Constraint);
PLUGINLIB_EXPORT_CLASS(beam_constraints::global::AbsoluteAccelerationLinear3DStampedConstraint, fuse_core::Constraint);
PLUGINLIB_EXPORT_CLASS(beam_constraints::global::AbsoluteImuBiasGyro3DStampedConstraint, fuse_core::Constraint);
PLUGINLIB_EXPORT_CLASS(beam_constraints::global::AbsoluteImuBiasAccel3DStampedConstraint, fuse_core::Constraint);