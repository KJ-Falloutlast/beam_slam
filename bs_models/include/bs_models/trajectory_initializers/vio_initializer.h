#pragma once

// libbeam
#include <beam_calibration/CameraModel.h>
#include <beam_cv/geometry/PoseRefinement.h>
#include <beam_cv/trackers/Trackers.h>

// fuse
#include <bs_common/current_submap.h>
#include <bs_common/extrinsics_lookup_online.h>
#include <bs_models/camera_to_camera/visual_map.h>
#include <bs_models/frame_to_frame/imu_preintegration.h>
#include <bs_models/trajectory_initializers/imu_initializer.h>
#include <fuse_graphs/hash_graph.h>

// ros
#include <bs_models/InitializedPathMsg.h>
#include <sensor_msgs/Imu.h>

namespace bs_models { namespace camera_to_camera {

class VIOInitializer {
public:
  /**
   * @brief Default Constructor
   */
  VIOInitializer() = default;

  /**
   * @brief Custom Constructor
   */
  VIOInitializer(std::shared_ptr<beam_calibration::CameraModel> cam_model,
                 std::shared_ptr<beam_cv::Tracker> tracker,
                 const std::string& path_topic,
                 const std::string& imu_intrinsics_path,
                 bool use_scale_estimate = false,
                 double max_optimization_time = 5.0,
                 const std::string& output_directory = "");

  /**
   * @brief Notifies initialization that image at this time is to be used for
   * init
   * @param cur_time timestamp of image
   */
  bool AddImage(ros::Time cur_time);

  /**
   * @brief Adds an imu measurement to the initializer
   * @param msg imu message to add
   */
  void AddIMU(const sensor_msgs::Imu& msg);

  /**
   * @brief Returns the current state
   * @return true or false if process is initialized
   */
  bool Initialized();

  /**
   * @brief Returns a read only the current local graph
   * @return read-only graph
   */
  const fuse_graphs::HashGraph& GetGraph();

  /**
   * @brief Returns a pointer to the imu preintegration object used
   * @return point to imu preintegration
   */
  std::shared_ptr<bs_models::frame_to_frame::ImuPreintegration>
      GetPreintegrator();

  /**
   * @brief Callback for path processing, this path is provided by LIO for
   * initialization
   * @param[in] msg - The path to process
   */
  void ProcessInitPath(const InitializedPathMsg::ConstPtr& msg);

private:
  /**
   * @brief Build a vector of frames with the current init path and the
   * frame_times_ vector, if a frame is outside of the given path it will be
   * referred to as an invalid frame and its pose will be set to 0
   * @param valid_frames[out] frames that are within the init path
   * @param invalid_frames[out] frames that are outside the init path
   */
  void BuildFrameVectors(
      std::vector<bs_models::camera_to_camera::Frame>& valid_frames,
      std::vector<bs_models::camera_to_camera::Frame>& invalid_frames);

  /**
   * @brief Estimates imu parameters given a vector of frames with some known
   * poses (can be up to scale from sfm, or in real world scale from lidar)
   * @param frames input frames  with poses to estimate imu parameters
   */
  void PerformIMUInitialization(
      const std::vector<bs_models::camera_to_camera::Frame>& frames);

  /**
   * @brief Adds all poses and inertial constraints contained within the frames
   * vector to the local graph
   * @param frames input frames
   * @param set_start when true will set the first frames pose as the prior
   */
  void AddPosesAndInertialConstraints(
      const std::vector<bs_models::camera_to_camera::Frame>& frames,
      bool set_start);

  /**
   * @brief Adds visual constraints to input frames, will triangulate landmarks
   * if they are not already triangulated
   * @param frames input frames
   * @return number of landmarks that have been added
   */
  size_t AddVisualConstraints(
      const std::vector<bs_models::camera_to_camera::Frame>& frames);

  /**
   * @brief Localizes a given frame using the current landmarks
   * @param frames input frames
   * @param T_WORLD_BASELINK[out] estimated pose of the camera
   * @return true or false if it succeeded or not
   */
  bool LocalizeFrame(const bs_models::camera_to_camera::Frame& frame,
                     Eigen::Matrix4d& T_WORLD_BASELINK);

  /**
   * @brief Outputs frame poses to standard output
   * @param frames vector of frames to output
   */
  void OutputFramePoses(
      const std::vector<bs_models::camera_to_camera::Frame>& frames);

  /**
   * @brief Optimizes the current local graph
   */
  void OptimizeGraph();

  /**
   * @brief Saves the poses and the points from the given frames to point clouds
   * @param frames input frames
   */
  void OutputResults(
      const std::vector<bs_models::camera_to_camera::Frame>& frames);

protected:
  // subscriber for initialized path
  ros::Subscriber path_subscriber_;

  // computer vision objects
  std::shared_ptr<beam_cv::PoseRefinement> pose_refiner_;
  std::shared_ptr<beam_calibration::CameraModel> cam_model_;
  std::shared_ptr<beam_cv::Tracker> tracker_;
  std::shared_ptr<bs_models::camera_to_camera::VisualMap> visual_map_;
  bs_common::CurrentSubmap& submap_ = bs_common::CurrentSubmap::GetInstance();

  // imu preintegration object
  std::shared_ptr<bs_models::frame_to_frame::ImuPreintegration> imu_preint_;
  bs_models::frame_to_frame::ImuPreintegration::Params imu_params_;

  // graph object for optimization
  std::shared_ptr<fuse_graphs::HashGraph> local_graph_;
  double max_optimization_time_{};

  // stores the added imu messages and times of keyframes to use for init
  std::queue<sensor_msgs::Imu> imu_buffer_;
  std::vector<uint64_t> frame_times_;

  // boolean flags
  bool is_initialized_{false};
  bool use_scale_estimate_{false};

  // imu intrinsics
  Eigen::Matrix3d cov_gyro_noise_;
  Eigen::Matrix3d cov_accel_noise_;
  Eigen::Matrix3d cov_gyro_bias_;
  Eigen::Matrix3d cov_accel_bias_;

  // preintegration parameters
  Eigen::Vector3d gravity_, bg_, ba_;
  double scale_;

  // initialization path
  std::shared_ptr<InitializedPathMsg> init_path_;

  // robot extrinsics
  Eigen::Matrix4d T_cam_baselink_;
  bs_common::ExtrinsicsLookupOnline& extrinsics_ =
      bs_common::ExtrinsicsLookupOnline::GetInstance();

  // directory to optionally output the initialization results
  std::string output_directory_{};
};

}} // namespace bs_models::camera_to_camera
