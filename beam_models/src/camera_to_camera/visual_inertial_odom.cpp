#include <beam_models/camera_to_camera/visual_inertial_odom.h>

#include <fuse_core/transaction.h>
#include <pluginlib/class_list_macros.h>

#include <cv_bridge/cv_bridge.h>
#include <nlohmann/json.hpp>

#include <beam_common/utils.h>
#include <beam_cv/descriptors/Descriptors.h>
#include <beam_cv/detectors/Detectors.h>
#include <beam_cv/geometry/AbsolutePoseEstimator.h>
#include <beam_cv/geometry/Triangulation.h>

// Register this sensor model with ROS as a plugin.
PLUGINLIB_EXPORT_CLASS(beam_models::camera_to_camera::VisualInertialOdom,
                       fuse_core::SensorModel)

namespace beam_models { namespace camera_to_camera {

VisualInertialOdom::VisualInertialOdom() : fuse_core::AsyncSensorModel(1) {}

void VisualInertialOdom::onInit() {
  // Read settings from the parameter sever
  device_id_ = fuse_variables::loadDeviceId(private_node_handle_);
  camera_params_.loadFromROS(private_node_handle_);
  global_params_.loadFromROS(private_node_handle_);
  /***********************************************************
   *       Initialize pose refiner object with params        *
   ***********************************************************/
  ceres::Solver::Options pose_refinement_options;
  pose_refinement_options.minimizer_progress_to_stdout = false;
  pose_refinement_options.max_num_iterations = 10;
  pose_refinement_options.max_solver_time_in_seconds = 1e-2;
  pose_refinement_options.function_tolerance = 1e-4;
  pose_refinement_options.gradient_tolerance = 1e-6;
  pose_refinement_options.parameter_tolerance = 1e-4;
  pose_refinement_options.linear_solver_type = ceres::SPARSE_SCHUR;
  pose_refinement_options.preconditioner_type = ceres::SCHUR_JACOBI;
  pose_refiner_ =
      std::make_shared<beam_cv::PoseRefinement>(pose_refinement_options);
  /***********************************************************
   *        Load camera model and Create Map object          *
   ***********************************************************/
  cam_model_ =
      beam_calibration::CameraModel::Create(global_params_.cam_intrinsics_path);
  visual_map_ = std::make_shared<VisualMap>(cam_model_, camera_params_.source);
  /***********************************************************
   *              Initialize tracker variables               *
   ***********************************************************/
  beam_cv::DescriptorType descriptor_type =
      beam_cv::DescriptorTypeStringMap[camera_params_.descriptor];
  std::shared_ptr<beam_cv::Descriptor> descriptor =
      beam_cv::Descriptor::Create(descriptor_type);
  std::shared_ptr<beam_cv::Detector> detector =
      std::make_shared<beam_cv::GFTTDetector>(
          camera_params_.num_features_to_track);
  tracker_ = std::make_shared<beam_cv::KLTracker>(detector, descriptor,
                                                  camera_params_.window_size);
  /***********************************************************
   *                  Subscribe to topics                    *
   ***********************************************************/
  image_subscriber_ =
      private_node_handle_.subscribe(camera_params_.image_topic, 1000,
                                     &VisualInertialOdom::processImage, this);
  imu_subscriber_ = private_node_handle_.subscribe(
      camera_params_.imu_topic, 10000, &VisualInertialOdom::processIMU, this);
  path_subscriber_ = private_node_handle_.subscribe(
      private_node_handle_.getNamespace() + camera_params_.init_path_topic, 1,
      &VisualInertialOdom::processInitPath, this);
  init_odom_publisher_ =
      private_node_handle_.advertise<geometry_msgs::PoseStamped>(
          camera_params_.frame_odometry_output_topic, 100);
  /***********************************************************
   *               Create initializer object                 *
   ***********************************************************/
  nlohmann::json J;
  std::ifstream file(global_params_.imu_intrinsics_path);
  file >> J;
  initializer_ =
      std::make_shared<beam_models::camera_to_camera::VIOInitializer>(
          cam_model_, tracker_, J["cov_gyro_noise"], J["cov_accel_noise"],
          J["cov_gyro_bias"], J["cov_accel_bias"]);
}

void VisualInertialOdom::processImage(const sensor_msgs::Image::ConstPtr& msg) {
  // push image onto buffer
  image_buffer_.push(*msg);
  // get current imu and image timestamps
  ros::Time imu_time = imu_buffer_.front().header.stamp;
  ros::Time img_time = image_buffer_.front().header.stamp;
  /**************************************************************************
   *                    Add Image to map or initializer                     *
   **************************************************************************/
  if (imu_time > img_time && !imu_buffer_.empty()) {
    tracker_->AddImage(ExtractImage(image_buffer_.front()), img_time);
    if (!initializer_->Initialized()) {
      if ((img_time - cur_kf_time_).toSec() >= 1.0) {
        keyframes_.push_back(img_time);
        cur_kf_time_ = img_time;
        if (initializer_->AddImage(img_time)) {
          ROS_INFO("Initialization Success: %f", cur_kf_time_.toSec());
          // get the preintegration object
          imu_preint_ = initializer_->GetPreintegrator();
          // copy init graph and send to fuse optimizer
          SendInitializationGraph(initializer_->GetGraph());
        } else {
          ROS_INFO("Initialization Failure: %f", cur_kf_time_.toSec());
        }
      }
    } else {
      std::vector<uint64_t> triangulated_ids;
      std::vector<uint64_t> untriangulated_ids;
      Eigen::Matrix4d T_WORLD_CAMERA =
          LocalizeFrame(img_time, triangulated_ids, untriangulated_ids);
      // transform to imu frame
      extrinsics_.GetT_CAMERA_BASELINK(T_cam_baselink_);
      Eigen::Matrix4d T_WORLD_BASELINK = T_WORLD_CAMERA * T_cam_baselink_;
      // publish pose to odom topic
      geometry_msgs::PoseStamped pose;
      beam_common::TransformationMatrixToPoseMsg(T_WORLD_BASELINK, img_time,
                                                 pose);
      init_odom_publisher_.publish(pose);
      // process if keyframe
      if (IsKeyframe(img_time, triangulated_ids, untriangulated_ids)) {
        cur_kf_time_ = img_time;
        keyframes_.push_back(img_time);
        added_since_kf_ = 0;
        // [1] Add constraints to triangulated ids
        // [2] Try to triangulate untriangulated ids and add constraints
      } else {
        added_since_kf_++;
      }
    }
    image_buffer_.pop();
  }
}

void VisualInertialOdom::processIMU(const sensor_msgs::Imu::ConstPtr& msg) {
  // push imu message onto buffer
  imu_buffer_.push(*msg);
  // get current image timestamp
  ros::Time img_time = image_buffer_.front().header.stamp;
  /**************************************************************************
   *          Add IMU messages to preintegrator or initializer              *
   **************************************************************************/
  while (imu_buffer_.front().header.stamp <= img_time && !imu_buffer_.empty()) {
    if (!initializer_->Initialized()) {
      initializer_->AddIMU(imu_buffer_.front());
    } else {
      imu_preint_->AddToBuffer(imu_buffer_.front());
    }
    imu_buffer_.pop();
  }
}

void VisualInertialOdom::processInitPath(
    const InitializedPathMsg::ConstPtr& msg) {
  initializer_->SetPath(*msg);
}

void VisualInertialOdom::onGraphUpdate(fuse_core::Graph::ConstSharedPtr graph) {
  visual_map_->UpdateGraph(graph);
}

void VisualInertialOdom::onStop() {}

cv::Mat VisualInertialOdom::ExtractImage(const sensor_msgs::Image& msg) {
  cv_bridge::CvImagePtr cv_ptr;
  try {
    cv_ptr = cv_bridge::toCvCopy(msg, msg.encoding);
  } catch (cv_bridge::Exception& e) {
    ROS_ERROR("cv_bridge exception: %s", e.what());
  }
  return cv_ptr->image;
}

void VisualInertialOdom::SendInitializationGraph(
    const fuse_graphs::HashGraph& init_graph) {
  auto transaction = fuse_core::Transaction::make_shared();
  for (auto& var : init_graph.getVariables()) {
    fuse_variables::Position3D::SharedPtr landmark =
        fuse_variables::Position3D::make_shared();
    fuse_variables::Position3DStamped::SharedPtr position =
        fuse_variables::Position3DStamped::make_shared();
    fuse_variables::Orientation3DStamped::SharedPtr orientation =
        fuse_variables::Orientation3DStamped::make_shared();

    if (var.type() == landmark->type()) {
      *landmark = dynamic_cast<const fuse_variables::Position3D&>(var);
      visual_map_->AddLandmark(landmark, transaction);
    }
    if (var.type() == orientation->type()) {
      *orientation =
          dynamic_cast<const fuse_variables::Orientation3DStamped&>(var);
      visual_map_->AddOrientation(orientation, transaction);
    }
    if (var.type() == position->type()) {
      *position = dynamic_cast<const fuse_variables::Position3DStamped&>(var);
      visual_map_->AddPosition(position, transaction);
    }
  }
  for (auto& constraint : init_graph.getConstraints()) {
    transaction->addConstraint(std::move(constraint.clone()));
  }
  sendTransaction(transaction);
}

Eigen::Matrix4d VisualInertialOdom::LocalizeFrame(
    const ros::Time& img_time, std::vector<uint64_t>& triangulated_ids,
    std::vector<uint64_t>& untriangulated_ids) {
  std::vector<Eigen::Vector2i, beam_cv::AlignVec2i> pixels;
  std::vector<Eigen::Vector3d, beam_cv::AlignVec3d> points;
  std::vector<uint64_t> landmarks = tracker_->GetLandmarkIDsInImage(img_time);
  // get 2d-3d correspondences
  for (auto& id : landmarks) {
    fuse_variables::Position3D::SharedPtr lm = visual_map_->GetLandmark(id);
    if (lm) {
      triangulated_ids.push_back(id);
      Eigen::Vector2i pixeli = tracker_->Get(img_time, id).cast<int>();
      pixels.push_back(pixeli);
      Eigen::Vector3d point(lm->x(), lm->y(), lm->z());
      points.push_back(point);
    } else {
      untriangulated_ids.push_back(id);
    }
  }
  // perform ransac pnp for initial estimate
  if (points.size() < 3) { return Eigen::Matrix4d::Zero(); }
  Eigen::Matrix4d T_CAMERA_WORLD_est =
      beam_cv::AbsolutePoseEstimator::RANSACEstimator(cam_model_, pixels,
                                                      points);
  // refine pose using motion only BA
  ceres::Solver::Options ceres_solver_options;
  ceres_solver_options.minimizer_progress_to_stdout = false;
  ceres_solver_options.max_solver_time_in_seconds = 1e-2;
  ceres_solver_options.linear_solver_type = ceres::SPARSE_SCHUR;
  ceres_solver_options.preconditioner_type = ceres::SCHUR_JACOBI;
  beam_cv::PoseRefinement refiner(ceres_solver_options);
  std::string report;
  Eigen::Matrix4d T_CAMERA_WORLD_ref = refiner.RefinePose(
      T_CAMERA_WORLD_est, cam_model_, pixels, points, report);
  return T_CAMERA_WORLD_ref.inverse();
}

bool VisualInertialOdom::IsKeyframe(
    const ros::Time& img_time, const std::vector<uint64_t>& triangulated_ids,
    const std::vector<uint64_t>& untriangulated_ids) {
  bool is_keyframe = false;
  if ((img_time - cur_kf_time_).toSec() >=
      camera_params_.keyframe_min_time_in_seconds) {
    std::vector<uint64_t> all_ids;
    all_ids.insert(all_ids.end(), triangulated_ids.begin(),
                   triangulated_ids.end());
    all_ids.insert(all_ids.end(), untriangulated_ids.begin(),
                   untriangulated_ids.end());
    double avg_parallax = ComputeAvgParallax(cur_kf_time_, img_time, all_ids);

    if (avg_parallax > camera_params_.keyframe_parallax ||
        triangulated_ids.size() < camera_params_.keyframe_tracks_drop) {
      is_keyframe = true;
    } else if (added_since_kf_ == (camera_params_.window_size - 1)) {
      is_keyframe = true;
    }
  } else {
    is_keyframe = false;
  }
  return is_keyframe;
}

double VisualInertialOdom::ComputeAvgParallax(
    const ros::Time& t1, const ros::Time& t2,
    const std::vector<uint64_t>& t2_landmarks) {
  double total_parallax = 0.0;
  int num_matches = 0;
  for (auto& id : t2_landmarks) {
    try {
      Eigen::Vector2d p1 = tracker_->Get(t1, id);
      Eigen::Vector2d p2 = tracker_->Get(t2, id);
      double dist = beam::distance(p1, p2);
      total_parallax += dist;
      num_matches++;
    } catch (const std::out_of_range& oor) {}
  }
  return total_parallax / (double)num_matches;
}

}} // namespace beam_models::camera_to_camera
