#pragma once

// std
#include <queue>

// messages
#include <beam_models/InitializedPathMsg.h>
#include <beam_slam_common/CameraMeasurementMsg.h>
#include <beam_slam_common/LandmarkMeasurementMsg.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/Imu.h>

// fuse
#include <fuse_core/async_sensor_model.h>
#include <fuse_models/common/throttled_callback.h>

// beam_slam
#include <beam_common/extrinsics_lookup.h>
#include <beam_models/camera_to_camera/visual_map.h>
#include <beam_models/frame_to_frame/imu_preintegration.h>
#include <beam_models/trajectory_initializers/vio_initializer.h>
#include <beam_parameters/models/camera_params.h>
#include <beam_parameters/models/global_params.h>

// libbeam
#include <beam_calibration/CameraModel.h>
#include <beam_cv/geometry/PoseRefinement.h>
#include <beam_cv/trackers/Trackers.h>

namespace beam_models { namespace camera_to_camera {

class VisualInertialOdom : public fuse_core::AsyncSensorModel {
public:
  SMART_PTR_DEFINITIONS(VisualInertialOdom);

  /**
   * @brief Default Constructor
   */
  VisualInertialOdom();

  /**
   * @brief Default Destructor
   */
  ~VisualInertialOdom() override = default;

  /**
   * @brief Callback for image processing, this callback will add visual
   * constraints and triangulate new landmarks when required
   * @param[in] msg - The image to process
   */
  void processImage(const sensor_msgs::Image::ConstPtr& msg);

  /**
   * @brief Callback for image processing, this callback will add visual
   * constraints and triangulate new landmarks when required
   * @param[in] msg - The image to process
   */
  void processIMU(const sensor_msgs::Imu::ConstPtr& msg);

  /**
   * @brief Callback for path processing, this path is provided by LIO for
   * initialization
   * @param[in] msg - The path to process
   */
  void processInitPath(const InitializedPathMsg::ConstPtr& msg);

protected:
  fuse_core::UUID device_id_; //!< The UUID of this device

  /**
   * @brief Perform any required initialization for the sensor model
   *
   * This could include things like reading from the parameter server or
   * subscribing to topics. The class's node handles will be properly
   * initialized before SensorModel::onInit() is called. Spinning of the
   * callback queue will not begin until after the call to SensorModel::onInit()
   * completes.
   */
  void onInit() override;

  /**
   * @brief Subscribe to the input topic to start sending transactions to the
   * optimizer
   */
  void onStart() override;

  /**
   * @brief Unsubscribe to the input topic
   */
  void onStop() override;

  /**
   * @brief Callback for when a newly optimized graph is available
   */
  void onGraphUpdate(fuse_core::Graph::ConstSharedPtr graph_msg) override;

private:
  /**
   * @brief Converts ros image message to opencv image
   * @param msg image message to convert to cv mat
   */
  cv::Mat ExtractImage(const sensor_msgs::Image& msg);

  /**
   * @brief Copies all variables and constraints in the init graph and sends to
   * fuse
   * @param init_graph the graph obtained from the initializer
   */
  void SendInitializationGraph(const fuse_graphs::HashGraph& init_graph);

  /**
   * @brief Localizes a given frame using the tracker and the current visual map
   * @param img_time time of image to localize
   * @param[out] triangulated_ids id's of landmarks that have already been
   * triangulated
   * @param[out] untriangulated_ids id's of landmarks that have not been
   * triangulated
   * @param[out] T_WORLD_CAMERA estimated pose
   * @return failure or success
   */
  bool LocalizeFrame(const ros::Time& img_time,
                     std::vector<uint64_t>& triangulated_ids,
                     std::vector<uint64_t>& untriangulated_ids,
                     Eigen::Matrix4d& T_WORLD_CAMERA);

  /**
   * @brief Determines if a frame is a keyframe
   * @param img_time time of image to determine if its a keyframe
   * @param triangulated_ids id's of landmarks that have already been
   * triangulated
   * @param untriangulated_ids id's of landmarks that have not been
   * triangulated
   * @param T_WORLD_CAMERA pose of the frame in question
   * @return true or false decision
   */
  bool IsKeyframe(const ros::Time& img_time,
                  const std::vector<uint64_t>& triangulated_ids,
                  const std::vector<uint64_t>& untriangulated_ids,
                  const Eigen::Matrix4d& T_WORLD_CAMERA);

  /**
   * @brief Computes the mean parallax between images at two times
   * @param t1 time of first image
   * @param t2 time of second image
   * @param t2_landmarks id's of landmarks that have been seen in tw
   * @return mean parallax
   */
  double ComputeAvgParallax(const ros::Time& t1, const ros::Time& t2,
                            const std::vector<uint64_t>& t2_landmarks);

  /**
   * @brief Extends the map at an image time and adds the visual constraints
   * @param img_time time of keyframe to extend map at
   * @param triangulated_ids id's of landmarks that have already been
   * triangulated
   * @param untriangulated_ids id's of landmarks that have not been
   * triangulated
   */
  void ExtendMap(const ros::Time& img_time,
                 const std::vector<uint64_t>& triangulated_ids,
                 const std::vector<uint64_t>& untriangulated_ids);

  /**
   * @brief Adds a pose to the graph
   * @param img_time time of keyframe to extend map at
   * @param T_WORLD_CAMERA pose of the keyframe to add
   */
  void SendNewKeyframePose(const ros::Time& img_time,
                           const Eigen::Matrix4d& T_WORLD_CAMERA);

  /**
   * @brief Send the generated inertial constraint for the current image
   * @param img_time time of current keyframe
   */
  void AddInertialConstraint(const ros::Time& img_time,
                              fuse_core::Transaction::SharedPtr transaction);

  /**
   * @brief Publishes the oldest keyframe that is stored
   */
  void PublishCameraMeasurement();

  /**
   * @brief Publishes to the keyframe header topic to notify any lsiteners that
   * a new keyframe is detected
   */
  void NotifyNewKeyframe(const ros::Time& img_time);

protected:
  // loadable camera parameters
  beam_parameters::models::CameraParams camera_params_;

  // global parameters
  beam_parameters::models::GlobalParams global_params_;

  // topic publishers, subscribers and buffers
  ros::Subscriber image_subscriber_;
  ros::Subscriber imu_subscriber_;
  ros::Subscriber path_subscriber_;
  ros::Publisher init_odom_publisher_;
  ros::Publisher new_keyframe_publisher_;
  ros::Publisher cam_measurement_publisher_;
  std::queue<sensor_msgs::Image> image_buffer_;
  std::queue<sensor_msgs::Imu> imu_buffer_;

  // callbacks for messages
  using ThrottledImageCallback =
      fuse_models::common::ThrottledCallback<sensor_msgs::Image>;
  using ThrottledIMUCallback =
      fuse_models::common::ThrottledCallback<sensor_msgs::Imu>;
  ThrottledImageCallback throttled_image_callback_;
  ThrottledIMUCallback throttled_imu_callback_;

  // computer vision objects
  std::shared_ptr<beam_cv::PoseRefinement> pose_refiner_;
  std::shared_ptr<beam_calibration::CameraModel> cam_model_;
  std::shared_ptr<beam_cv::Tracker> tracker_;
  std::shared_ptr<beam_models::camera_to_camera::VisualMap> visual_map_;
  bool init_graph_optimized_{false};

  // initialization object
  std::shared_ptr<beam_models::camera_to_camera::VIOInitializer> initializer_;

  // imu preintegration object
  std::shared_ptr<beam_models::frame_to_frame::ImuPreintegration> imu_preint_;

  // keyframe information
  ros::Time cur_kf_time_ = ros::Time(0);
  std::deque<ros::Time> keyframes_;
  uint32_t added_since_kf_{0};

  // robot extrinsics
  Eigen::Matrix4d T_cam_baselink_;
  beam_common::ExtrinsicsLookup& extrinsics_ =
      beam_common::ExtrinsicsLookup::GetInstance();
};

}} // namespace beam_models::camera_to_camera
