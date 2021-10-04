#include <bs_models/imu_preintegration.h>

#include <fuse_core/transaction.h>

#include <beam_utils/math.h>

namespace bs_models {

ImuPreintegration::ImuPreintegration(const Params &params) : params_(params) {
  CheckParameters();
  SetPreintegrator();
}

ImuPreintegration::ImuPreintegration(const Params &params,
                                     const Eigen::Vector3d &init_bg,
                                     const Eigen::Vector3d &init_ba)
    : params_(params), bg_(init_bg), ba_(init_ba) {
  CheckParameters();
  SetPreintegrator();
}

void ImuPreintegration::ClearBuffer() {
  std::queue<bs_common::IMUData> empty;
  std::swap(current_imu_data_buffer_, empty);
  std::swap(total_imu_data_buffer_, empty);
}

void ImuPreintegration::AddToBuffer(const sensor_msgs::Imu &msg) {
  bs_common::IMUData imu_data(msg);
  current_imu_data_buffer_.push(imu_data);
  total_imu_data_buffer_.push(imu_data);
}

void ImuPreintegration::AddToBuffer(const bs_common::IMUData &imu_data) {
  current_imu_data_buffer_.push(imu_data);
  total_imu_data_buffer_.push(imu_data);
}

void ImuPreintegration::CheckParameters() {
  if (params_.cov_prior_noise <= 0) {
    BEAM_ERROR("Prior noise on IMU state must be positive");
    throw std::invalid_argument{"Inputs to ImuPreintegration invalid."};
  }
}

void ImuPreintegration::SetPreintegrator() {
  pre_integrator_ij.cov_w = params_.cov_gyro_noise;
  pre_integrator_ij.cov_a = params_.cov_accel_noise;
  pre_integrator_ij.cov_bg = params_.cov_gyro_bias;
  pre_integrator_ij.cov_ba = params_.cov_accel_bias;
}

void ImuPreintegration::ResetPreintegrator() {
  pre_integrator_ij.Reset();
  pre_integrator_ij.data.clear();
}

void ImuPreintegration::SetStart(
    const ros::Time &t_start,
    fuse_variables::Orientation3DStamped::SharedPtr R_WORLD_IMU,
    fuse_variables::Position3DStamped::SharedPtr t_WORLD_IMU,
    fuse_variables::VelocityLinear3DStamped::SharedPtr velocity) {
  // adjust IMU buffer
  while (t_start > current_imu_data_buffer_.front().t) {
    current_imu_data_buffer_.pop();
  }

  while (t_start > total_imu_data_buffer_.front().t) {
    total_imu_data_buffer_.pop();
  }

  // set IMU state
  bs_common::ImuState imu_state_i(t_start);

  if (R_WORLD_IMU) {
    imu_state_i.SetOrientation(R_WORLD_IMU->data());
  }

  if (t_WORLD_IMU) {
    imu_state_i.SetPosition(t_WORLD_IMU->data());
  }

  if (velocity) {
    imu_state_i.SetVelocity(velocity->data());
  }

  imu_state_i.SetGyroBias(bg_);
  imu_state_i.SetAccelBias(ba_);

  imu_state_i_ = std::move(imu_state_i);

  // copy start IMU state to initialize kth frame between keyframes
  imu_state_k_ = imu_state_i_;
}

bs_common::ImuState
ImuPreintegration::PredictState(const bs_common::PreIntegrator &pre_integrator,
                                const bs_common::ImuState &imu_state_curr,
                                const ros::Time &t_now) {
  // get commonly used variables
  const double &dt = pre_integrator.delta.t.toSec();
  const Eigen::Matrix3d &q_curr = imu_state_curr.OrientationMat();

  // predict new states
  Eigen::Quaterniond q_new(q_curr * pre_integrator.delta.q.matrix());
  Eigen::Vector3d v_new = imu_state_curr.VelocityVec() + GRAVITY_WORLD * dt +
                          q_curr * pre_integrator.delta.v;
  Eigen::Vector3d p_new =
      imu_state_curr.PositionVec() + imu_state_curr.VelocityVec() * dt +
      0.5 * GRAVITY_WORLD * dt * dt + q_curr * pre_integrator.delta.p;

  // set time
  ros::Time t_new = imu_state_curr.Stamp() + pre_integrator.delta.t;
  if (t_now != ros::Time(0)) {
    t_new = t_now;
  }

  // return predicted IMU state
  bs_common::ImuState imu_state_new(t_new, q_new, p_new, v_new,
                                    imu_state_curr.GyroBiasVec(),
                                    imu_state_curr.AccelBiasVec());
  return imu_state_new;
}

bool ImuPreintegration::GetPose(Eigen::Matrix4d &T_WORLD_IMU,
                                const ros::Time &t_now) {
  // encapsulate IMU measurements between frames
  bs_common::PreIntegrator pre_integrator_interval;

  // check requested time
  if (t_now < current_imu_data_buffer_.front().t) {
    return false;
  }

  // Populate integrators
  while (t_now > current_imu_data_buffer_.front().t) {
    pre_integrator_interval.data.emplace_back(current_imu_data_buffer_.front());
    pre_integrator_ij.data.emplace_back(current_imu_data_buffer_.front());
    current_imu_data_buffer_.pop();
  }
  // integrate between frames
  pre_integrator_interval.Integrate(t_now, imu_state_i_.GyroBiasVec(),
                                    imu_state_i_.AccelBiasVec(), false, false);

  // predict state at end of window using integrated IMU measurements
  bs_common::ImuState imu_state_k =
      PredictState(pre_integrator_interval, imu_state_k_, t_now);
  imu_state_k_ = std::move(imu_state_k);

  // populate transformation matrix
  beam::QuaternionAndTranslationToTransformMatrix(
      imu_state_k_.OrientationQuat(), imu_state_k_.PositionVec(), T_WORLD_IMU);

  return true;
}

fuse_core::Transaction::SharedPtr
ImuPreintegration::RegisterNewImuPreintegratedFactor(
    const ros::Time &t_now,
    fuse_variables::Orientation3DStamped::SharedPtr R_WORLD_IMU,
    fuse_variables::Position3DStamped::SharedPtr t_WORLD_IMU) {
  bs_constraints::relative_pose::ImuState3DStampedTransaction transaction(
      t_now);

  // check requested time
  if (t_now < current_imu_data_buffer_.front().t) {
    return nullptr;
  }

  // generate prior constraint at start
  if (first_window_) {
    Eigen::Matrix<double, 15, 15> prior_covariance{
        Eigen::Matrix<double, 15, 15>::Identity()};
    for (int i = 0; i < 15; i++) {
      prior_covariance(i, i) = params_.cov_prior_noise;
    }

    // Add relative constraints and variables for first key frame
    transaction.AddPriorImuStateConstraint(imu_state_i_, prior_covariance,
                                           "FIRST_IMU_STATE_PRIOR");
    transaction.AddImuStateVariables(imu_state_i_);

    first_window_ = false;
  }

  // populate integrator
  while (t_now > current_imu_data_buffer_.front().t &&
         !current_imu_data_buffer_.empty()) {
    pre_integrator_ij.data.emplace_back(current_imu_data_buffer_.front());
    current_imu_data_buffer_.pop();
  }

  // integrate between key frames, incrementally calculating covariance and
  // jacobians
  pre_integrator_ij.Integrate(t_now, imu_state_i_.GyroBiasVec(),
                              imu_state_i_.AccelBiasVec(), true, true);

  // predict state at end of window using integrated imu measurements
  bs_common::ImuState imu_state_j =
      PredictState(pre_integrator_ij, imu_state_i_, t_now);

  // Add relative constraints and variables between key frames
  transaction.AddRelativeImuStateConstraint(imu_state_i_, imu_state_j,
                                            pre_integrator_ij);
  transaction.AddImuStateVariables(imu_state_j);

  // update orientation and position of predicted imu state with arguments
  if (R_WORLD_IMU && t_WORLD_IMU) {
    imu_state_j.SetOrientation(R_WORLD_IMU->data());
    imu_state_j.SetPosition(t_WORLD_IMU->data());
    Eigen::Vector3d new_velocity =
        (imu_state_j.PositionVec() - imu_state_i_.PositionVec()) /
        (t_now.toSec() - imu_state_i_.Stamp().toSec());
    imu_state_j.SetVelocity(new_velocity);
  }

  // move predicted state to previous state
  imu_state_i_ = std::move(imu_state_j);

  // clear total imu data buffer for data before new state i
  while (imu_state_i_.Stamp() > total_imu_data_buffer_.front().t) {
    total_imu_data_buffer_.pop();
  }

  // copy state to kth frame
  imu_state_k_ = imu_state_i_;

  ResetPreintegrator();

  return transaction.GetTransaction();
}

void ImuPreintegration::UpdateGraph(
    fuse_core::Graph::ConstSharedPtr graph_msg) {

  // get timestamp for state i
  ros::Time stamp_i = imu_state_i_.Stamp();

  try {
    // get position
    fuse_variables::Position3DStamped::SharedPtr position =
        fuse_variables::Position3DStamped::make_shared();
    auto position_uuid = fuse_core::uuid::generate(position->type(), stamp_i,
                                                   fuse_core::uuid::NIL);
    *position = dynamic_cast<const fuse_variables::Position3DStamped &>(
        graph_msg->getVariable(position_uuid));
    imu_state_i_.SetPosition(position->data());

    // get orientaiton
    fuse_variables::Orientation3DStamped::SharedPtr orientation =
        fuse_variables::Orientation3DStamped::make_shared();
    auto orientation_uuid = fuse_core::uuid::generate(
        orientation->type(), stamp_i, fuse_core::uuid::NIL);
    *orientation = dynamic_cast<const fuse_variables::Orientation3DStamped &>(
        graph_msg->getVariable(orientation_uuid));
    imu_state_i_.SetOrientation(orientation->data());

    // get velocity
    fuse_variables::VelocityLinear3DStamped::SharedPtr velocity =
        fuse_variables::VelocityLinear3DStamped::make_shared();
    auto velocity_uuid = fuse_core::uuid::generate(velocity->type(), stamp_i,
                                                   fuse_core::uuid::NIL);
    *velocity = dynamic_cast<const fuse_variables::VelocityLinear3DStamped &>(
        graph_msg->getVariable(velocity_uuid));
    imu_state_i_.SetVelocity(velocity->data());

    // get gyro bias
    bs_variables::GyroscopeBias3DStamped::SharedPtr gyrobias =
        bs_variables::GyroscopeBias3DStamped::make_shared();
    auto gyrobias_uuid = fuse_core::uuid::generate(gyrobias->type(), stamp_i,
                                                   fuse_core::uuid::NIL);
    *gyrobias = dynamic_cast<const bs_variables::GyroscopeBias3DStamped &>(
        graph_msg->getVariable(gyrobias_uuid));
    imu_state_i_.SetGyroBias(gyrobias->data());

    // get accel bias
    bs_variables::AccelerationBias3DStamped::SharedPtr accelbias =
        bs_variables::AccelerationBias3DStamped::make_shared();
    auto accelbias_uuid = fuse_core::uuid::generate(accelbias->type(), stamp_i,
                                                    fuse_core::uuid::NIL);
    *accelbias = dynamic_cast<const bs_variables::AccelerationBias3DStamped &>(
        graph_msg->getVariable(accelbias_uuid));
    imu_state_i_.SetAccelBias(accelbias->data());

    // reset current data buffer to be the total buffer starting at state i
    current_imu_data_buffer_ = total_imu_data_buffer_;

    // copy state i to kth frame
    imu_state_k_ = imu_state_i_;

  } catch (const std::out_of_range& oor) {}
}

} // namespace bs_models
