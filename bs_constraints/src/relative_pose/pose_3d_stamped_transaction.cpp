#include <bs_constraints/relative_pose/pose_3d_stamped_transaction.h>

#include <bs_common/conversions.h>
#include <bs_variables/orientation_3d.h>
#include <bs_variables/position_3d.h>

namespace bs_constraints {

Pose3DStampedTransaction::Pose3DStampedTransaction(
    const ros::Time& transaction_stamp, bool override_constraints,
    bool override_variables)
    : override_constraints_(override_constraints),
      override_variables_(override_variables) {
  transaction_ = fuse_core::Transaction::make_shared();
  transaction_->stamp(transaction_stamp);
}

fuse_core::Transaction::SharedPtr
    Pose3DStampedTransaction::GetTransaction() const {
  if (transaction_->empty()) { return nullptr; }
  return transaction_;
}

void Pose3DStampedTransaction::AddPoseConstraint(
    const fuse_variables::Position3DStamped& position1,
    const fuse_variables::Position3DStamped& position2,
    const fuse_variables::Orientation3DStamped& orientation1,
    const fuse_variables::Orientation3DStamped& orientation2,
    const fuse_variables::Position3DStamped& position2_relative,
    const fuse_variables::Orientation3DStamped& orientation2_relative,
    const Eigen::Matrix<double, 6, 6>& covariance, const std::string& source) {
  // convert relative pose to vector
  fuse_core::Vector7d pose_relative_mean;
  pose_relative_mean << position2_relative.x(), position2_relative.y(),
      position2_relative.z(), orientation2_relative.w(),
      orientation2_relative.x(), orientation2_relative.y(),
      orientation2_relative.z();

  // build and add constraint
  auto constraint =
      fuse_constraints::RelativePose3DStampedConstraint::make_shared(
          source, position1, orientation1, position2, orientation2,
          pose_relative_mean, covariance);
  transaction_->addConstraint(constraint, override_constraints_);
}

void Pose3DStampedTransaction::AddPosePrior(
    const fuse_variables::Position3DStamped& position,
    const fuse_variables::Orientation3DStamped& orientation,
    const fuse_core::Matrix6d& prior_covariance,
    const std::string& prior_source) {
  fuse_core::Vector7d mean;
  mean << position.x(), position.y(), position.z(), orientation.w(),
      orientation.x(), orientation.y(), orientation.z();

  auto prior =
      std::make_shared<fuse_constraints::AbsolutePose3DStampedConstraint>(
          prior_source, position, orientation, mean, prior_covariance);
  transaction_->addConstraint(prior, override_constraints_);
}

void Pose3DStampedTransaction::AddPosePrior(
    const fuse_variables::Position3DStamped& position,
    const fuse_variables::Orientation3DStamped& orientation,
    double prior_covariance_noise, const std::string& prior_source) {
  fuse_core::Matrix6d prior_covariance_matrix{fuse_core::Matrix6d::Identity()};
  for (int i = 0; i < 6; i++) {
    prior_covariance_matrix(i, i) = prior_covariance_noise;
  }
  AddPosePrior(position, orientation, prior_covariance_matrix, prior_source);
}

void Pose3DStampedTransaction::AddPoseVariables(
    const fuse_variables::Position3DStamped& position,
    const fuse_variables::Orientation3DStamped& orientation,
    const ros::Time& stamp) {
  transaction_->addInvolvedStamp(stamp);

  // add to transaction
  transaction_->addVariable(
      fuse_variables::Position3DStamped::make_shared(position),
      override_variables_);
  transaction_->addVariable(
      fuse_variables::Orientation3DStamped::make_shared(orientation),
      override_variables_);
}

} // namespace bs_constraints