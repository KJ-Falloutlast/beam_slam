#include <bs_models/frame_initializers/odometry_frame_initializer.h>

#include <boost/algorithm/string.hpp>

#include <beam_utils/log.h>

#include <bs_common/conversions.h>

namespace bs_models { namespace frame_initializers {

OdometryFrameInitializer::OdometryFrameInitializer(
    const std::string& topic, int queue_size, int64_t poses_buffer_time,
    const std::string& sensor_frame_id_override,
    const Eigen::Matrix4d& T_ORIGINAL_OVERRIDE) {
  authority_ = "odometry";
  poses_ = std::make_shared<tf2::BufferCore>(ros::Duration(poses_buffer_time));
  pose_lookup_ = std::make_shared<bs_common::PoseLookup>(poses_);
  T_ORIGINAL_OVERRIDE_ = T_ORIGINAL_OVERRIDE;

  ros::NodeHandle n;
  odometry_subscriber_ = n.subscribe<nav_msgs::Odometry>(
      topic, queue_size,
      boost::bind(&OdometryFrameInitializer::OdometryCallback, this, _1));

  if (!sensor_frame_id_override.empty()) {
    if (!extrinsics_.IsSensorFrameIdValid(sensor_frame_id_override)) {
      BEAM_ERROR("Sensor frame id override [{}] invalid. Exiting.",
                 sensor_frame_id_override);
      throw std::invalid_argument{"Invalid sensor frame id override."};
    } else {
      BEAM_INFO("Overriding sensor frame id in odometry messages to: {}",
                sensor_frame_id_override);
      sensor_frame_id_ = sensor_frame_id_override;
      override_sensor_frame_id_ = true;
    }
  } else {
    sensor_frame_id_ = extrinsics_.GetBaselinkFrameId();
  }
}

void OdometryFrameInitializer::CheckOdometryFrameIDs(
    const nav_msgs::OdometryConstPtr message) {
  check_world_baselink_frames_ = false;

  std::string parent_frame_id = message->header.frame_id;
  std::string child_frame_id = message->child_frame_id;

  // check that parent frame supplied by odometry contains world frame
  if (!boost::algorithm::contains(parent_frame_id,
                                  extrinsics_.GetWorldFrameId())) {
    BEAM_WARN(
        "World frame in extrinsics does not match parent frame in odometry "
        "messages. Using extrinsics.");
  }

  // check that child frame supplied by odometry contains one of the sensor
  // frames
  if (!override_sensor_frame_id_) {
    if (boost::algorithm::contains(child_frame_id,
                                   extrinsics_.GetImuFrameId())) {
      sensor_frame_id_ = extrinsics_.GetImuFrameId();
    } else if (boost::algorithm::contains(child_frame_id,
                                          extrinsics_.GetCameraFrameId())) {
      sensor_frame_id_ = extrinsics_.GetCameraFrameId();
    } else if (boost::algorithm::contains(child_frame_id,
                                          extrinsics_.GetLidarFrameId())) {
      sensor_frame_id_ = extrinsics_.GetLidarFrameId();
    } else {
      BEAM_ERROR("Sensor frame id in odometry message ({}) not equal to any "
                 "sensor frame in extrinsics. Please provide a "
                 "sensor_frame_id_override. Available sensor frame ids: {}",
                 child_frame_id, extrinsics_.GetFrameIdsString());
      throw std::invalid_argument{"Invalid frame id"};
    }
  }
}

void OdometryFrameInitializer::OdometryCallback(
    const nav_msgs::OdometryConstPtr message) {
  if (check_world_baselink_frames_) { CheckOdometryFrameIDs(message); }

  // if sensor_frame is already baselink, then we can directly copy
  if (sensor_frame_id_ == extrinsics_.GetBaselinkFrameId()) {
    geometry_msgs::TransformStamped tf_stamped;
    bs_common::OdometryMsgToTransformedStamped(
        *message, message->header.stamp, message->header.seq,
        extrinsics_.GetWorldFrameId(), extrinsics_.GetBaselinkFrameId(),
        tf_stamped);
    poses_->setTransform(tf_stamped, authority_, false);
    return;
  }

  // Otherwise, transform sensor frame to baselink and then publish
  Eigen::Matrix4d T_SENSOR_BASELINK;
  if (extrinsics_.GetT_SENSOR_BASELINK(T_SENSOR_BASELINK, sensor_frame_id_,
                                       message->header.stamp)) {
    Eigen::Matrix4d T_WORLD_ORIGINAL;
    bs_common::OdometryMsgToTransformationMatrix(*message, T_WORLD_ORIGINAL);

    Eigen::Matrix4d T_WORLD_SENSOR = T_WORLD_ORIGINAL * T_ORIGINAL_OVERRIDE_;

    Eigen::Matrix4d T_WORLD_BASELINK = T_WORLD_SENSOR * T_SENSOR_BASELINK;

    geometry_msgs::TransformStamped tf_stamped;
    bs_common::EigenTransformToTransformStampedMsg(
        T_WORLD_BASELINK, message->header.stamp, message->header.seq,
        extrinsics_.GetWorldFrameId(), extrinsics_.GetBaselinkFrameId(),
        tf_stamped);
    poses_->setTransform(tf_stamped, authority_, false);
    return;
  } else {
    BEAM_WARN("Skipping odometry message.");
    return;
  }
}

}} // namespace bs_models::frame_initializers
