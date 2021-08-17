#include <bs_common/extrinsics_lookup_online.h>

#include <bs_common/utils.h>
#include <beam_utils/log.h>
#include <beam_utils/math.h>

namespace bs_common {

ExtrinsicsLookupOnline& ExtrinsicsLookupOnline::GetInstance() {
  static ExtrinsicsLookupOnline instance;
  return instance;
}

ExtrinsicsLookupOnline::ExtrinsicsLookupOnline() {
  ExtrinsicsLookupBase::FrameIds frame_ids;

  // get parameters from global namespace
  ros::param::get("~imu_frame", frame_ids.imu);
  ros::param::get("~camera_frame", frame_ids.camera);
  ros::param::get("~lidar_frame", frame_ids.lidar);
  ros::param::get("~world_frame", frame_ids.world);
  ros::param::get("~baselink_frame", frame_ids.baselink);
  ros::param::get("~static_extrinsics", static_extrinsics_);

  extrinsics_ = std::make_shared<ExtrinsicsLookupBase>(frame_ids);
}

void ExtrinsicsLookupOnline::SaveToJson(const std::string& save_filename) {
  extrinsics_->SaveToJson(save_filename);
}

ExtrinsicsLookupBase ExtrinsicsLookupOnline::GetExtrinsicsCopy() {
  return *extrinsics_;
}

bool ExtrinsicsLookupOnline::GetTransform(Eigen::Matrix4d& T,
                                          const std::string& to_frame,
                                          const std::string& from_frame,
                                          const ros::Time& time) {
  // if dynamic extrinsics, then we want to lookup the extrinsics now and
  // replace the most recent estimate in ExtrinsicsLookupBase
  if (!static_extrinsics_) {
    if (!LookupTransform(T, to_frame, from_frame, time)) {
      return false;
    }
    extrinsics_->SetTransform(T, to_frame, from_frame);
    return true;
  }

  // else try to lookup in ExtrinsicsLookupBase
  if (extrinsics_->GetTransform(T, to_frame, from_frame)) {
    return true;
  }

  // if that failed, then the transform isn't set, so lets look it up and set it
  if (!LookupTransform(T, to_frame, from_frame)) {
    return false;
  }
  extrinsics_->SetTransform(T, to_frame, from_frame);
  return true;
}

bool ExtrinsicsLookupOnline::GetT_CAMERA_IMU(Eigen::Matrix4d& T,
                                             const ros::Time& time) {
  return GetTransform(T, extrinsics_->GetCameraFrameId(),
                      extrinsics_->GetImuFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_IMU_CAMERA(Eigen::Matrix4d& T,
                                             const ros::Time& time) {
  return GetTransform(T, extrinsics_->GetImuFrameId(),
                      extrinsics_->GetCameraFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_CAMERA_LIDAR(Eigen::Matrix4d& T,
                                               const ros::Time& time) {
  return GetTransform(T, extrinsics_->GetCameraFrameId(),
                      extrinsics_->GetLidarFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_LIDAR_CAMERA(Eigen::Matrix4d& T,
                                               const ros::Time& time) {
  return GetTransform(T, extrinsics_->GetLidarFrameId(),
                      extrinsics_->GetCameraFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_IMU_LIDAR(Eigen::Matrix4d& T,
                                            const ros::Time& time) {
  return GetTransform(T, extrinsics_->GetImuFrameId(),
                      extrinsics_->GetLidarFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_LIDAR_IMU(Eigen::Matrix4d& T,
                                            const ros::Time& time) {
  return GetTransform(T, extrinsics_->GetLidarFrameId(),
                      extrinsics_->GetImuFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_BASELINK_IMU(Eigen::Matrix4d& T,
                                               const ros::Time& time) {
  if (extrinsics_->GetBaselinkFrameId() == extrinsics_->GetImuFrameId()) {
    T = Eigen::Matrix4d::Identity();
    return true;
  }

  return GetTransform(T, extrinsics_->GetBaselinkFrameId(),
                      extrinsics_->GetImuFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_IMU_BASELINK(Eigen::Matrix4d& T,
                                               const ros::Time& time) {
  if (extrinsics_->GetBaselinkFrameId() == extrinsics_->GetImuFrameId()) {
    T = Eigen::Matrix4d::Identity();
    return true;
  }

  return GetTransform(T, extrinsics_->GetImuFrameId(),
                      extrinsics_->GetBaselinkFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_BASELINK_CAMERA(Eigen::Matrix4d& T,
                                                  const ros::Time& time) {
  if (extrinsics_->GetBaselinkFrameId() == extrinsics_->GetCameraFrameId()) {
    T = Eigen::Matrix4d::Identity();
    return true;
  }

  return GetTransform(T, extrinsics_->GetBaselinkFrameId(),
                      extrinsics_->GetCameraFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_CAMERA_BASELINK(Eigen::Matrix4d& T,
                                                  const ros::Time& time) {
  if (extrinsics_->GetBaselinkFrameId() == extrinsics_->GetCameraFrameId()) {
    T = Eigen::Matrix4d::Identity();
    return true;
  }

  return GetTransform(T, extrinsics_->GetCameraFrameId(),
                      extrinsics_->GetBaselinkFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_BASELINK_LIDAR(Eigen::Matrix4d& T,
                                                 const ros::Time& time) {
  if (extrinsics_->GetBaselinkFrameId() == extrinsics_->GetLidarFrameId()) {
    T = Eigen::Matrix4d::Identity();
    return true;
  }

  return GetTransform(T, extrinsics_->GetBaselinkFrameId(),
                      extrinsics_->GetLidarFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_LIDAR_BASELINK(Eigen::Matrix4d& T,
                                                 const ros::Time& time) {
  if (extrinsics_->GetBaselinkFrameId() == extrinsics_->GetLidarFrameId()) {
    T = Eigen::Matrix4d::Identity();
    return true;
  }

  return GetTransform(T, extrinsics_->GetLidarFrameId(),
                      extrinsics_->GetBaselinkFrameId(), time);
}

bool ExtrinsicsLookupOnline::GetT_BASELINK_SENSOR(
    Eigen::Matrix4d& T, const std::string& sensor_frame,
    const ros::Time& time) {
  if (!extrinsics_->IsSensorFrameIdValid(sensor_frame)) {
    BEAM_ERROR("Invalid sensor frame id.");
    return false;
  }

  return GetTransform(T, extrinsics_->GetBaselinkFrameId(), sensor_frame, time);
}

bool ExtrinsicsLookupOnline::GetT_SENSOR_BASELINK(
    Eigen::Matrix4d& T, const std::string& sensor_frame,
    const ros::Time& time) {
  if (!extrinsics_->IsSensorFrameIdValid(sensor_frame)) {
    BEAM_ERROR("Invalid sensor frame id.");
    return false;
  }

  return GetTransform(T, sensor_frame, extrinsics_->GetBaselinkFrameId(), time);
}

std::string ExtrinsicsLookupOnline::GetImuFrameId() const {
  return extrinsics_->GetImuFrameId();
}

std::string ExtrinsicsLookupOnline::GetCameraFrameId() const {
  return extrinsics_->GetCameraFrameId();
}

std::string ExtrinsicsLookupOnline::GetLidarFrameId() const {
  return extrinsics_->GetLidarFrameId();
}

std::string ExtrinsicsLookupOnline::GetWorldFrameId() const {
  return extrinsics_->GetWorldFrameId();
}

std::string ExtrinsicsLookupOnline::GetBaselinkFrameId() const {
  return extrinsics_->GetBaselinkFrameId();
}

bool ExtrinsicsLookupOnline::IsStatic() const { return static_extrinsics_; }

bool ExtrinsicsLookupOnline::IsSensorFrameIdValid(
    const std::string& sensor_frame) {
  return extrinsics_->IsSensorFrameIdValid(sensor_frame);
}

bool ExtrinsicsLookupOnline::LookupTransform(Eigen::Matrix4d& T,
                                             const std::string& to_frame,
                                             const std::string& from_frame,
                                             const ros::Time& time) {
  tf::StampedTransform TROS;
  try {
    tf_listener_.lookupTransform(to_frame, from_frame, time, TROS);
  } catch (tf::TransformException& ex) {
    if (static_extrinsics_) {
      BEAM_WARN("Cannot lookup static extrinsics between frames: {} , {}",
                to_frame, from_frame);
    } else {
      BEAM_WARN(
          "Cannot lookup dynamic extrinsics between {} and {} for t = %.10f",
          to_frame, from_frame, time.toSec());
    }
    return false;
  }

  ROSStampedTransformToEigenTransform(TROS, T);

  return true;
}

}  // namespace bs_common