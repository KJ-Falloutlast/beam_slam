#include <bs_models/global_mapping/global_map.h>

#include <chrono>
#include <ctime>

#include <boost/filesystem.hpp>
#include <pcl/conversions.h>

#include <beam_cv/OpenCVConversions.h>
#include <beam_cv/descriptors/Descriptor.h>
#include <beam_mapping/Poses.h>
#include <beam_utils/filesystem.h>
#include <beam_utils/log.h>
#include <beam_utils/pcl_conversions.h>
#include <beam_utils/pointclouds.h>
#include <beam_utils/time.h>

#include <bs_common/conversions.h>
#include <bs_common/graph_access.h>
#include <bs_constraints/relative_pose/pose_3d_stamped_transaction.h>
#include <bs_models/reloc/reloc_methods.h>

namespace bs_models { namespace global_mapping {

using namespace reloc;

GlobalMap::Params::Params() {
  double local_map_cov_diag = 1e-3;
  double loop_cov_diag = 1e-5;

  // clang-format off
  local_mapper_covariance << local_map_cov_diag, 0, 0, 0, 0, 0,
                             0, local_map_cov_diag, 0, 0, 0, 0,
                             0, 0, local_map_cov_diag, 0, 0, 0,
                             0, 0, 0, local_map_cov_diag, 0, 0,
                             0, 0, 0, 0, local_map_cov_diag, 0,
                             0, 0, 0, 0, 0, local_map_cov_diag;

  reloc_covariance << loop_cov_diag, 0, 0, 0, 0, 0,
                      0, loop_cov_diag, 0, 0, 0, 0,
                      0, 0, loop_cov_diag, 0, 0, 0,
                      0, 0, 0, loop_cov_diag, 0, 0,
                      0, 0, 0, 0, loop_cov_diag, 0,
                      0, 0, 0, 0, 0, loop_cov_diag;
  // clang-format on
}

void GlobalMap::Params::LoadJson(const std::string& config_path) {
  std::string read_file = config_path;
  if (read_file.empty()) {
    BEAM_INFO(
        "No config file provided to global map, using default parameters.");
    return;
  }

  if (read_file == "DEFAULT_PATH") {
    read_file =
        bs_common::GetBeamSlamConfigPath() + "global_map/global_map.json";
  }

  BEAM_INFO("Loading global map config file: {}", read_file);

  nlohmann::json J;
  if (!beam::ReadJson(read_file, J)) {
    BEAM_ERROR("Using default parameters.");
    return;
  }

  submap_size = J["submap_size_m"];
  reloc_candidate_search_type = J["reloc_candidate_search_type"];
  reloc_refinement_type = J["reloc_refinement_type"];
  reloc_candidate_search_config = J["reloc_candidate_search_config"];
  reloc_refinement_config = J["reloc_refinement_config"];

  std::vector<double> vec = J["local_mapper_covariance_diag"];
  if (vec.size() != 6) {
    BEAM_ERROR(
        "Invalid local mapper covariance diagonal (6 values required). Using "
        "default.");
  } else {
    Eigen::VectorXd vec_eig(6);
    vec_eig << vec[0], vec[1], vec[2], vec[3], vec[4], vec[5];
    local_mapper_covariance = vec_eig.asDiagonal();
  }

  std::vector<double> vec2 = J["reloc_covariance_diag"];
  if (vec2.size() != 6) {
    BEAM_ERROR("Invalid reloc covariance diagonal (6 values required). Using "
               "default.");
  } else {
    Eigen::VectorXd vec_eig = Eigen::VectorXd(6);
    vec_eig << vec2[0], vec2[1], vec2[2], vec2[3], vec2[4], vec2[5];
    reloc_covariance = vec_eig.asDiagonal();
  }

  // load filters
  nlohmann::json J_publishing = J["publishing"];
  nlohmann::json J_submap_filters = J_publishing["submap_lidar_filters"];
  nlohmann::json J_globalmap_filters = J_publishing["globalmap_lidar_filters"];

  ros_submap_filter_params =
      beam_filtering::LoadFilterParamsVector(J_submap_filters);
  ros_globalmap_filter_params =
      beam_filtering::LoadFilterParamsVector(J_globalmap_filters);
}

void GlobalMap::Params::SaveJson(const std::string& filename) {
  nlohmann::json J = {
      {"submap_size_m", submap_size},
      {"reloc_candidate_search_type", reloc_candidate_search_type},
      {"reloc_refinement_type", reloc_refinement_type},
      {"reloc_candidate_search_config", reloc_candidate_search_config},
      {"reloc_refinement_config", reloc_refinement_config},
      {"local_mapper_covariance_diag",
       {local_mapper_covariance(0, 0), local_mapper_covariance(1, 1),
        local_mapper_covariance(2, 2), local_mapper_covariance(3, 3),
        local_mapper_covariance(4, 4), local_mapper_covariance(5, 5)}},
      {"reloc_covariance_diag",
       {reloc_covariance(0, 0), reloc_covariance(1, 1), reloc_covariance(2, 2),
        reloc_covariance(3, 3), reloc_covariance(4, 4),
        reloc_covariance(5, 5)}}};

  std::ofstream file(filename);
  file << std::setw(4) << J << std::endl;
}

GlobalMap::GlobalMap(
    const std::shared_ptr<beam_calibration::CameraModel>& camera_model,
    const std::shared_ptr<bs_common::ExtrinsicsLookupBase>& extrinsics)
    : camera_model_(camera_model), extrinsics_(extrinsics) {
  Setup();
}

GlobalMap::GlobalMap(
    const std::shared_ptr<beam_calibration::CameraModel>& camera_model,
    const std::shared_ptr<bs_common::ExtrinsicsLookupBase>& extrinsics,
    const Params& params)
    : camera_model_(camera_model), params_(params), extrinsics_(extrinsics) {
  Setup();
}

GlobalMap::GlobalMap(
    const std::shared_ptr<beam_calibration::CameraModel>& camera_model,
    const std::shared_ptr<bs_common::ExtrinsicsLookupBase>& extrinsics,
    const std::string& config_path)
    : camera_model_(camera_model), extrinsics_(extrinsics) {
  params_.LoadJson(config_path);
  Setup();
}

GlobalMap::GlobalMap(const std::string& data_root_directory) {
  if (!Load(data_root_directory)) {
    BEAM_ERROR("Unable to load global map data, check data content/formats.");
    throw std::runtime_error{
        "Unable to instantiate GlobalMap with input data."};
  }
}

std::vector<SubmapPtr> GlobalMap::GetOnlineSubmaps() {
  return online_submaps_;
}

std::vector<SubmapPtr> GlobalMap::GetOfflineSubmaps() {
  return offline_submaps_;
}

void GlobalMap::SetOnlineSubmaps(std::vector<SubmapPtr>& submaps) {
  online_submaps_ = submaps;
}

void GlobalMap::SetOfflineSubmaps(std::vector<SubmapPtr>& submaps) {
  offline_submaps_ = submaps;
}

void GlobalMap::SetStoreNewSubmaps(bool store_new_submaps) {
  store_newly_completed_submaps_ = store_new_submaps;
}

void GlobalMap::SetStoreNewScans(bool store_new_scans) {
  store_new_scans_ = store_new_scans;
}

void GlobalMap::SetStoreUpdatedGlobalMap(bool store_updated_global_map) {
  store_updated_global_map_ = store_updated_global_map;
}

std::vector<std::shared_ptr<RosMap>> GlobalMap::GetRosMaps() {
  std::vector<std::shared_ptr<RosMap>> maps_vector;
  while (!ros_new_scans_.empty()) {
    maps_vector.push_back(ros_new_scans_.front());
    ros_new_scans_.pop();
  }
  while (!ros_submaps_.empty()) {
    maps_vector.push_back(ros_submaps_.front());
    ros_submaps_.pop();
  }
  if (ros_global_lidar_map_ != nullptr) {
    maps_vector.push_back(ros_global_lidar_map_);
    ros_global_lidar_map_ = nullptr;
  }
  if (ros_global_keypoints_map_ != nullptr) {
    maps_vector.push_back(ros_global_keypoints_map_);
    ros_global_keypoints_map_ = nullptr;
  }

  return maps_vector;
}

void GlobalMap::Setup() {
  // initiate reloc candidate search
  if (params_.reloc_candidate_search_type == "EUCDIST") {
    reloc_candidate_search_ = std::make_unique<RelocCandidateSearchEucDist>(
        params_.reloc_candidate_search_config);
  } else {
    BEAM_ERROR("Invalid reloc candidate search type. Using default: EUCDIST. "
               "Input: {}",
               params_.reloc_candidate_search_type);
    reloc_candidate_search_ = std::make_unique<RelocCandidateSearchEucDist>(
        params_.reloc_candidate_search_config);
  }

  // initiate reloc refinement
  if (params_.reloc_refinement_type == "ICP") {
    reloc_refinement_ = std::make_unique<RelocRefinementIcp>(
        params_.reloc_covariance, params_.reloc_refinement_config);
  } else if (params_.reloc_refinement_type == "GICP") {
    reloc_refinement_ = std::make_unique<RelocRefinementGicp>(
        params_.reloc_covariance, params_.reloc_refinement_config);
  } else if (params_.reloc_refinement_type == "NDT") {
    reloc_refinement_ = std::make_unique<RelocRefinementNdt>(
        params_.reloc_covariance, params_.reloc_refinement_config);
  } else if (params_.reloc_refinement_type == "LOAM") {
    reloc_refinement_ = std::make_unique<RelocRefinementLoam>(
        params_.reloc_covariance, params_.reloc_refinement_config);
  } else {
    BEAM_ERROR("Invalid reloc refinement type. Using default: ICP");
    reloc_refinement_ = std::make_unique<RelocRefinementIcp>(
        params_.reloc_covariance, params_.reloc_refinement_config);
  }
}

fuse_core::Transaction::SharedPtr GlobalMap::AddMeasurement(
    const bs_common::CameraMeasurementMsg& cam_measurement,
    const bs_common::LidarMeasurementMsg& lid_measurement,
    const nav_msgs::Path& traj_measurement,
    const Eigen::Matrix4d& T_WORLD_BASELINK, const ros::Time& stamp) {
  fuse_core::Transaction::SharedPtr new_transaction = nullptr;

  int submap_id = GetSubmapId(T_WORLD_BASELINK);

  // if id is equal to submap size then we need to create a new submap
  if (submap_id == online_submaps_.size()) {
    SubmapPtr new_submap = std::make_shared<Submap>(stamp, T_WORLD_BASELINK,
                                                    camera_model_, extrinsics_);
    online_submaps_.push_back(new_submap);
    new_transaction = InitiateNewSubmapPose();

    fuse_core::Transaction::SharedPtr reloc_transaction =
        RunLoopClosure(online_submaps_.size() - 2);

    if (reloc_transaction != nullptr) {
      new_transaction->merge(*reloc_transaction);
    }

    if (store_newly_completed_submaps_ && online_submaps_.size() > 1) {
      AddRosSubmap(online_submaps_.size() - 2);
    }
  }

  // add camera measurement if not empty
  if (!cam_measurement.landmarks.empty()) {
    ROS_DEBUG("Adding camera measurement to global map.");
    online_submaps_.at(submap_id)->AddCameraMeasurement(cam_measurement,
                                                        T_WORLD_BASELINK);
  }

  // if lidar measurement exists, check frame id
  if (!lid_measurement.lidar_points.empty() ||
      !lid_measurement.lidar_edges_strong.empty() ||
      !lid_measurement.lidar_surfaces_strong.empty()) {
    if (lid_measurement.frame_id != extrinsics_->GetLidarFrameId()) {
      BEAM_WARN(
          "Lidar measurement frame id not consistent with lidar frame in the "
          "extrinsics class.");
    }
  }

  // add lidar measurement if not empty
  // ROS_DEBUG("Adding lidar measurement to global map.");
  PointCloud cloud;
  if (!lid_measurement.lidar_points.empty()) {
    cloud = beam::ROSVectorToPCL(lid_measurement.lidar_points);

    // add ros msg if applicable
    if (store_new_scans_) { AddNewRosScan(cloud, T_WORLD_BASELINK, stamp); }

    online_submaps_.at(submap_id)->AddLidarMeasurement(cloud, T_WORLD_BASELINK,
                                                       stamp);
  }
  if (lid_measurement.lidar_edges_strong.size() +
          lid_measurement.lidar_edges_strong.size() +
          lid_measurement.lidar_surfaces_strong.size() +
          lid_measurement.lidar_surfaces_weak.size() >
      0) {
    beam_matching::LoamPointCloud loamCloud;
    loamCloud.edges.strong.cloud =
        beam::ROSVectorToPCLIRT(lid_measurement.lidar_edges_strong);
    loamCloud.edges.weak.cloud =
        beam::ROSVectorToPCLIRT(lid_measurement.lidar_edges_weak);
    loamCloud.surfaces.strong.cloud =
        beam::ROSVectorToPCLIRT(lid_measurement.lidar_surfaces_strong);
    loamCloud.surfaces.weak.cloud =
        beam::ROSVectorToPCLIRT(lid_measurement.lidar_surfaces_weak);
    online_submaps_.at(submap_id)->AddLidarMeasurement(loamCloud,
                                                       T_WORLD_BASELINK, stamp);
  }

  // add trajectory measurement if not empty
  if (!traj_measurement.poses.empty()) {
    ROS_DEBUG("Adding trajectory measurement to global map.");
    std::vector<Eigen::Matrix4d, beam::AlignMat4d> poses;
    std::vector<ros::Time> stamps;
    for (const auto& pose : traj_measurement.poses) {
      Eigen::Matrix4d T_KEYFRAME_FRAME;
      bs_common::PoseMsgToTransformationMatrix(pose, T_KEYFRAME_FRAME);
      poses.push_back(T_KEYFRAME_FRAME);
      stamps.push_back(pose.header.stamp);
    }
  }

  return new_transaction;
}

fuse_core::Transaction::SharedPtr GlobalMap::TriggerLoopClosure() {
  if (online_submaps_.size() < 2) { return nullptr; }

  return RunLoopClosure(online_submaps_.size() - 1);
}

int GlobalMap::GetSubmapId(const Eigen::Matrix4d& T_WORLD_BASELINK) {
  // check if current pose is within "submap_size" from previous submap, or
  // current submap. We prioritize the previous submap for the case where data
  // isn't coming in in order (e.g., lidar data may come in slower)

  // first check if submaps is empty
  if (online_submaps_.empty()) { return 0; }

  Eigen::Vector3d t_WORLD_FRAME = T_WORLD_BASELINK.block(0, 3, 3, 1);

  Eigen::Vector3d t_WORLD_SUBMAPCUR =
      online_submaps_.at(online_submaps_.size() - 1)
          ->T_WORLD_SUBMAP_INIT()
          .block(0, 3, 3, 1);

  // if only one submap exists, we only check the pose is within this first
  // submap
  if (online_submaps_.size() == 1) {
    if ((t_WORLD_FRAME - t_WORLD_SUBMAPCUR).norm() < params_.submap_size) {
      return 0;
    } else {
      return 1;
    }
  }

  // otherwise, also check the prev submap and prioritize that one
  Eigen::Vector3d t_WORLD_SUBMAPPREV =
      online_submaps_.at(online_submaps_.size() - 2)
          ->T_WORLD_SUBMAP_INIT()
          .block(0, 3, 3, 1);

  if ((t_WORLD_FRAME - t_WORLD_SUBMAPPREV).norm() < params_.submap_size) {
    return online_submaps_.size() - 2;
  } else if ((t_WORLD_FRAME - t_WORLD_SUBMAPCUR).norm() < params_.submap_size) {
    return online_submaps_.size() - 1;
  } else {
    return online_submaps_.size();
  }
}

fuse_core::Transaction::SharedPtr GlobalMap::InitiateNewSubmapPose() {
  ROS_DEBUG("Initiating new submap pose");

  const SubmapPtr& current_submap =
      online_submaps_.at(online_submaps_.size() - 1);
  bs_constraints::Pose3DStampedTransaction new_transaction(
      current_submap->Stamp());
  new_transaction.AddPoseVariables(current_submap->Position(),
                                   current_submap->Orientation(),
                                   current_submap->Stamp());

  // if first submap, add prior then return
  if (online_submaps_.size() == 1) {
    new_transaction.AddPosePrior(current_submap->Position(),
                                 current_submap->Orientation(),
                                 pose_prior_noise_, "GlobalMap");
    return new_transaction.GetTransaction();
  }

  // If not first submap add constraint to previous
  const SubmapPtr& previous_submap =
      online_submaps_.at(online_submaps_.size() - 2);

  Eigen::Matrix4d T_PREVIOUS_CURRENT =
      beam::InvertTransform(previous_submap->T_WORLD_SUBMAP()) *
      current_submap->T_WORLD_SUBMAP();
  new_transaction.AddPoseConstraint(
      previous_submap->Position(), current_submap->Position(),
      previous_submap->Orientation(), current_submap->Orientation(),
      bs_common::TransformMatrixToVectorWithQuaternion(T_PREVIOUS_CURRENT),
      params_.local_mapper_covariance, "GlobalMap");

  ROS_DEBUG("Returning submap pose prior");
  return new_transaction.GetTransaction();
}

fuse_core::Transaction::SharedPtr GlobalMap::RunLoopClosure(int query_index) {
  // if first submap, don't look for relocs
  if (online_submaps_.size() == 1) { return nullptr; }

  ROS_DEBUG("Searching for loop closure candidates");

  const Eigen::Matrix4d& T_WORLD_QUERY =
      online_submaps_.at(query_index)->T_WORLD_SUBMAP();

  std::vector<int> matched_indices;
  std::vector<Eigen::Matrix4d, beam::AlignMat4d> Ts_MATCH_QUERY;
  reloc_candidate_search_->FindRelocCandidates(online_submaps_, T_WORLD_QUERY,
                                               matched_indices, Ts_MATCH_QUERY);

  // remove candidate if it is equal to the query submap, or one before
  std::vector<int> matched_indices_filtered;
  for (int i : matched_indices) {
    if (i == query_index || i == query_index - 1) { continue; }
    matched_indices_filtered.push_back(i);
  }

  ROS_DEBUG("Found %zu loop closure candidates.",
            matched_indices_filtered.size());

  if (matched_indices_filtered.size() == 0) { return nullptr; }

  ROS_DEBUG("Matched index[0]: %d, Query Index: %d, No. or submaps: %zu. "
            "Running loop "
            "closure refinement",
            matched_indices_filtered.at(0), query_index,
            online_submaps_.size());

  fuse_core::Transaction::SharedPtr transaction =
      std::make_shared<fuse_core::Transaction>();
  for (int i = 0; i < matched_indices_filtered.size(); i++) {
    // if the matched index is adjacent to the query index, ignore it. This
    // would happen from improper candidate search implementations
    if (matched_indices_filtered[i] == query_index + 1 ||
        matched_indices_filtered[i] == query_index - 1) {
      continue;
    }
    fuse_core::Transaction::SharedPtr new_transaction =
        reloc_refinement_->GenerateTransaction(
            online_submaps_.at(matched_indices_filtered[i]),
            online_submaps_.at(query_index), Ts_MATCH_QUERY[i]);
    if (new_transaction != nullptr) { transaction->merge(*new_transaction); }
  }

  int num_constraints = bs_common::GetNumberOfConstraints(transaction);
  ROS_DEBUG("Returning %d loop closure transactions", num_constraints);

  // if we are returning loop closure constraints, we will set the active submap
  // to none to make sure next reloc takes the most updates active submap
  if (num_constraints > 0) { active_submap_type_ = SubmapType::NONE; }

  return transaction;
}

bool GlobalMap::ProcessRelocRequest(
    const bs_common::RelocRequestMsg& reloc_request_msg,
    bs_common::SubmapMsg& submap_msg) {
  // load pose
  ros::Time stamp = reloc_request_msg.T_WORLD_BASELINK.header.stamp;
  Eigen::Matrix4d T_WORLDLM_QUERY;
  bs_common::PoseMsgToTransformationMatrix(reloc_request_msg.T_WORLD_BASELINK,
                                           T_WORLDLM_QUERY);

  // load pointcloud
  PointCloud lidar_cloud_in_query_frame =
      beam::ROSVectorToPCL(reloc_request_msg.lidar_measurement.lidar_points);

  // load loam cloud
  beam_matching::LoamPointCloudPtr loam_cloud_in_query_frame =
      std::make_shared<beam_matching::LoamPointCloud>(
          beam::ROSVectorToPCLIRT(
              reloc_request_msg.lidar_measurement.lidar_edges_strong),
          beam::ROSVectorToPCLIRT(
              reloc_request_msg.lidar_measurement.lidar_surfaces_strong),
          beam::ROSVectorToPCLIRT(
              reloc_request_msg.lidar_measurement.lidar_edges_weak),
          beam::ROSVectorToPCLIRT(
              reloc_request_msg.lidar_measurement.lidar_surfaces_weak));

  // if either lidar clouds have points, check the frame id
  if (!lidar_cloud_in_query_frame.empty() ||
      !loam_cloud_in_query_frame->Empty()) {
    if (reloc_request_msg.lidar_measurement.frame_id !=
        extrinsics_->GetBaselinkFrameId()) {
      BEAM_WARN(
          "Frame id of lidar data in reloc request not consistent with the "
          "baselink frame id stored in the intrinsics class.");
    }
  }

  std::vector<int> matched_indices;
  std::vector<Eigen::Matrix4d, beam::AlignMat4d> Ts_SUBMAPCANDIDATE_QUERY;

  // first, search through offline maps
  if (!offline_submaps_.empty()) {
    // search for candidate relocs
    Eigen::Matrix4d T_WORLDOFF_QUERY =
        beam::InvertTransform(T_WORLDLM_WORLDOFF_) * T_WORLDLM_QUERY;
    ROS_DEBUG("Looking for reloc submap candidates in offline maps.");
    reloc_candidate_search_->FindRelocCandidates(
        offline_submaps_, T_WORLDOFF_QUERY, matched_indices,
        Ts_SUBMAPCANDIDATE_QUERY);
    ROS_DEBUG("Found %zu submap candidates.", Ts_SUBMAPCANDIDATE_QUERY.size());

    // go through candidates, and get first successful reloc refinement
    for (uint16_t i = 0; i < matched_indices.size(); i++) {
      int submap_index = matched_indices.at(i);
      const auto& T_SUBMAP_QUERY_initial = Ts_SUBMAPCANDIDATE_QUERY.at(i);

      if (active_submap_id_ == submap_index &&
          active_submap_type_ == SubmapType::OFFLINE) {
        ROS_DEBUG("Active submap is the same as returned submap, not updating "
                  "submap.");
        return false;
      }

      // get refined pose
      ROS_DEBUG("Looking for refined submap pose within submap index: %d",
                submap_index);
      const auto& submap = offline_submaps_.at(submap_index);
      Eigen::Matrix4d T_SUBMAP_QUERY_refined;
      if (reloc_refinement_->GetRefinedPose(
              T_SUBMAP_QUERY_refined, T_SUBMAP_QUERY_initial, submap,
              lidar_cloud_in_query_frame, loam_cloud_in_query_frame)) {
        ROS_DEBUG("Found refined reloc pose.");
        // calculate transform from offline map world frame to the local mapper
        // world frame if not already calculated
        if (!T_WORLDLM_WORLDOFF_found_) {
          ROS_DEBUG(
              "Setting transform from offline map world frame to local mapper "
              "world frame.");
          T_WORLDLM_WORLDOFF_ = T_WORLDLM_QUERY *
                                beam::InvertTransform(T_SUBMAP_QUERY_refined) *
                                beam::InvertTransform(submap->T_WORLD_SUBMAP());
          T_WORLDLM_WORLDOFF_found_ = true;
        }

        // get all required submap data
        PointCloud lidar_cloud_in_woff_frame =
            submap->GetLidarPointsInWorldFrameCombined(false);
        beam_matching::LoamPointCloud loam_cloud_in_woff_frame =
            submap->GetLidarLoamPointsInWorldFrame(false);
        PointCloud keypoints_in_woff_frame =
            submap->GetKeypointsInWorldFrame(false);

        // get descriptors
        std::vector<uint32_t> word_ids;
        // TODO: for each landmark id, get all its measurements and get the
        // associated word id for it

        // add submap data to submap msg
        FillSubmapMsg(submap_msg, lidar_cloud_in_woff_frame,
                      loam_cloud_in_woff_frame, keypoints_in_woff_frame,
                      word_ids, T_WORLDLM_WORLDOFF_);

        // set current submap
        active_submap_id_ = submap_index;
        active_submap_type_ = SubmapType::OFFLINE;

        return true;
      } else {
        ROS_DEBUG("Reloc refinement failed.");
      }
    }
  }

  // next, search through online maps
  if (!online_submaps_.empty()) {
    // search for candidate relocs
    ROS_DEBUG("Looking for reloc submap candidates in online maps.");
    reloc_candidate_search_->FindRelocCandidates(
        online_submaps_, T_WORLDLM_QUERY, matched_indices,
        Ts_SUBMAPCANDIDATE_QUERY, 2, true);
    ROS_DEBUG("Found %zu submap candidates.", Ts_SUBMAPCANDIDATE_QUERY.size());

    // go through candidates, and get first successful reloc refinement. Note:
    // based on our definition of RelocCandidateSearchBase::FindRelocCandidates,
    // this should return results in order of most to least likely. So as soon
    // as we find one, we stop.
    for (uint16_t i = 0; i < matched_indices.size(); i++) {
      int submap_index = matched_indices.at(i);
      const auto& T_SUBMAP_QUERY_initial = Ts_SUBMAPCANDIDATE_QUERY.at(i);

      if (active_submap_id_ == submap_index &&
          active_submap_type_ == SubmapType::ONLINE) {
        ROS_DEBUG("Active submap is the same as returned submap, not updating "
                  "submap.");
        return false;
      }

      const auto& submap = online_submaps_.at(submap_index);

      // get refined pose
      ROS_DEBUG("Looking for refined submap pose within submap index: %d",
                submap_index);
      Eigen::Matrix4d T_SUBMAP_QUERY_refined;
      if (reloc_refinement_->GetRefinedPose(
              T_SUBMAP_QUERY_refined, T_SUBMAP_QUERY_initial, submap,
              lidar_cloud_in_query_frame, loam_cloud_in_query_frame)) {
        ROS_DEBUG("Found refined reloc pose.");

        // get all required submap data
        PointCloud lidar_cloud_in_wlm_frame =
            submap->GetLidarPointsInWorldFrameCombined(true);
        beam_matching::LoamPointCloud loam_cloud_in_wlm_frame =
            submap->GetLidarLoamPointsInWorldFrame(true);
        PointCloud keypoints_in_wlm_frame =
            submap->GetKeypointsInWorldFrame(true);

        // get descriptors
        std::vector<uint32_t> word_ids;
        // TODO: for each landmark id, get all its measurements and get the
        // associated word id for it

        // add submap data to submap msg
        FillSubmapMsg(submap_msg, lidar_cloud_in_wlm_frame,
                      loam_cloud_in_wlm_frame, keypoints_in_wlm_frame, word_ids,
                      Eigen::Matrix4d::Identity());

        // set current submap
        active_submap_id_ = submap_index;
        active_submap_type_ = SubmapType::OFFLINE;

        return true;
      } else {
        ROS_DEBUG("Reloc refinement failed.");
      }
    }
  }

  // if we get to here, we were not successful
  return false;
}

void GlobalMap::UpdateSubmapPoses(fuse_core::Graph::ConstSharedPtr graph_msg,
                                  const ros::Time& update_time) {
  last_update_time_ = update_time;

  for (uint16_t i = 0; i < online_submaps_.size(); i++) {
    online_submaps_.at(i)->UpdatePose(graph_msg);
  }

  if (store_updated_global_map_) { AddRosGlobalMap(); }

  global_map_updates_++;
}

void GlobalMap::SaveData(const std::string& output_path) {
  if (!boost::filesystem::exists(output_path)) {
    BEAM_ERROR(
        "Global map output path does not exist, not saving map. Input: {}",
        output_path);
    return;
  }

  BEAM_INFO("Saving full global map to: {}", output_path);
  params_.SaveJson(output_path + "params.json");
  camera_model_->WriteJSON(output_path + "camera_model.json");
  extrinsics_->SaveExtrinsicsToJson(output_path + "extrinsics.json");
  extrinsics_->SaveFrameIdsToJson(output_path + "frame_ids.json");
  for (uint16_t i = 0; i < online_submaps_.size(); i++) {
    std::string submap_dir = output_path + "submap" + std::to_string(i) + "/";
    boost::filesystem::create_directory(submap_dir);
    online_submaps_.at(i)->SaveData(submap_dir);
  }
  BEAM_INFO("Done saving global map.");
}

bool GlobalMap::Load(const std::string& root_directory) {
  if (!boost::filesystem::exists(root_directory)) {
    BEAM_ERROR(
        "Global map root directory path does not exist, not loading map. "
        "Input: {}",
        root_directory);
    return false;
  }
  BEAM_INFO("Loading full global map from: {}", root_directory);

  // load params
  if (!boost::filesystem::exists(root_directory + "params.json")) {
    BEAM_ERROR(
        "params.json not foudn in root directory, not loading GlobalMap. "
        "Input root directory: {}",
        root_directory);
    return false;
  }
  params_.LoadJson(root_directory + "params.json");

  // load camera model
  if (!boost::filesystem::exists(root_directory + "camera_model.json")) {
    BEAM_ERROR(
        "camera_model.json not found in root directory, not loading GlobalMap. "
        "Input root directory: {}",
        root_directory);
    return false;
  }

  std::string camera_filename = root_directory + "camera_model.json";
  BEAM_INFO("Loading camera model from: {}", camera_filename);
  camera_model_ = beam_calibration::CameraModel::Create(camera_filename);

  // load extrinsics
  extrinsics_ = std::make_shared<bs_common::ExtrinsicsLookupBase>(
      root_directory + "frame_ids.json", root_directory + "extrinsics.json");

  // setup general stuff
  Setup();

  int submap_num = 0;
  while (true) {
    std::string submap_dir =
        root_directory + "submap" + std::to_string(submap_num) + "/";
    if (!boost::filesystem::exists(submap_dir)) { break; }

    SubmapPtr current_submap = std::make_shared<Submap>(
        ros::Time(0), Eigen::Matrix4d::Identity(), camera_model_, extrinsics_);
    BEAM_INFO("Loading submap from: {}", submap_dir);
    current_submap->LoadData(submap_dir, false);
    online_submaps_.push_back(current_submap);
    submap_num++;
  }

  if (submap_num == 0) {
    BEAM_ERROR("No submaps loaded, root directory empty.");
    return false;
  } else {
    BEAM_INFO("Done loading global map. Loaded {} submaps.", submap_num);
    return true;
  }
}

void GlobalMap::SaveLidarSubmaps(const std::string& output_path,
                                 bool save_initial) {
  if (!boost::filesystem::exists(output_path)) {
    BEAM_ERROR("Invalid output path, not saving submaps. Input: {}",
               output_path);
    return;
  }

  // save optimized submap
  std::string submaps_path = output_path + "lidar_submaps_optimized/";
  boost::filesystem::create_directory(submaps_path);
  for (int i = 0; i < online_submaps_.size(); i++) {
    std::string submap_name =
        submaps_path + "lidar_submap" + std::to_string(i) + ".pcd";
    online_submaps_.at(i)->SaveLidarMapInWorldFrame(submap_name,
                                                    max_output_map_size_);
  }

  if (!save_initial) { return; }

  // save initial submap
  std::string submaps_path_initial = output_path + "lidar_submaps_initial/";
  boost::filesystem::create_directory(submaps_path_initial);
  for (int i = 0; i < online_submaps_.size(); i++) {
    std::string submap_name =
        submaps_path_initial + "lidar_submap" + std::to_string(i) + ".pcd";
    online_submaps_.at(i)->SaveLidarMapInWorldFrame(submap_name,
                                                    max_output_map_size_, true);
  }
}

void GlobalMap::SaveKeypointSubmaps(const std::string& output_path,
                                    bool save_initial) {
  if (!boost::filesystem::exists(output_path)) {
    BEAM_ERROR("Invalid output path, not saving submaps. Input: {}",
               output_path);
    return;
  }

  // save optimized submaps
  std::string submaps_path = output_path + "keypoint_submaps_optimized/";
  boost::filesystem::create_directory(submaps_path);
  for (int i = 0; i < online_submaps_.size(); i++) {
    std::string submap_name =
        submaps_path + "keypoint_submap" + std::to_string(i) + ".pcd";
    online_submaps_.at(i)->SaveKeypointsMapInWorldFrame(submap_name);
  }

  if (!save_initial) { return; }

  // save initial submaps
  std::string submaps_path_initial = output_path + "keypoint_submaps_initial/";
  boost::filesystem::create_directory(submaps_path_initial);
  for (int i = 0; i < online_submaps_.size(); i++) {
    std::string submap_name =
        submaps_path_initial + "keypoint_submap" + std::to_string(i) + ".pcd";
    online_submaps_.at(i)->SaveKeypointsMapInWorldFrame(submap_name, true);
  }
}

void GlobalMap::SaveTrajectoryFile(const std::string& output_path,
                                   bool save_initial) {
  if (!boost::filesystem::exists(output_path)) {
    BEAM_ERROR("Invalid output path, not saving trajectory file. Input: {}",
               output_path);
    return;
  }

  std::string date = beam::ConvertTimeToDate(std::chrono::system_clock::now());

  // Get trajectory
  beam_mapping::Poses poses;
  poses.SetPoseFileDate(date);
  poses.SetFixedFrame(extrinsics_->GetWorldFrameId());
  poses.SetMovingFrame(extrinsics_->GetBaselinkFrameId());
  for (uint16_t i = 0; i < online_submaps_.size(); i++) {
    const SubmapPtr& submap = online_submaps_.at(i);
    Eigen::Matrix4d T_WORLD_SUBMAP = submap->T_WORLD_SUBMAP();
    auto poses_stamped = submap->GetTrajectory();
    for (auto& pose_stamped : poses_stamped) {
      Eigen::Matrix4d& T_SUBMAP_BASELINK = pose_stamped.pose;
      Eigen::Matrix4d T_WORLD_BASELINK = T_WORLD_SUBMAP * T_SUBMAP_BASELINK;
      poses.AddSingleTimeStamp(pose_stamped.stamp);
      poses.AddSinglePose(T_WORLD_BASELINK);
    }
  }

  // save
  std::string output_file =
      output_path + "global_map_trajectory_optimized.json";
  poses.WriteToJSON(output_file);

  if (!save_initial) { return; }

  // Get trajectory
  beam_mapping::Poses poses_initial;
  poses_initial.SetPoseFileDate(date);
  poses_initial.SetFixedFrame(extrinsics_->GetWorldFrameId());
  poses_initial.SetMovingFrame(extrinsics_->GetBaselinkFrameId());
  for (uint16_t i = 0; i < online_submaps_.size(); i++) {
    const SubmapPtr& submap = online_submaps_.at(i);
    Eigen::Matrix4d T_WORLD_SUBMAP = submap->T_WORLD_SUBMAP_INIT();
    std::vector<Submap::PoseStamped> poses_stamped = submap->GetTrajectory();
    for (auto& pose_stamped : poses_stamped) {
      Eigen::Matrix4d& T_SUBMAP_BASELINK = pose_stamped.pose;
      Eigen::Matrix4d T_WORLD_BASELINK = T_WORLD_SUBMAP * T_SUBMAP_BASELINK;
      poses_initial.AddSingleTimeStamp(pose_stamped.stamp);
      poses_initial.AddSinglePose(T_WORLD_BASELINK);
    }
  }

  // save
  std::string output_file_initial =
      output_path + "global_map_trajectory_initial.json";
  poses_initial.WriteToJSON(output_file_initial);
}

void GlobalMap::SaveTrajectoryClouds(const std::string& output_path,
                                     bool save_initial) {
  if (!boost::filesystem::exists(output_path)) {
    BEAM_ERROR("Invalid output path, not saving trajectory clouds. Input: {}",
               output_path);
    return;
  }

  // get trajectory
  pcl::PointCloud<pcl::PointXYZRGBL> cloud;
  for (uint16_t i = 0; i < online_submaps_.size(); i++) {
    const SubmapPtr& submap = online_submaps_.at(i);
    Eigen::Matrix4d T_WORLD_SUBMAP = submap->T_WORLD_SUBMAP();
    std::vector<Submap::PoseStamped> poses_stamped = submap->GetTrajectory();
    for (const Submap::PoseStamped& pose_stamped : poses_stamped) {
      const Eigen::Matrix4d& T_SUBMAP_BASELINK = pose_stamped.pose;
      Eigen::Vector4d p(0, 0, 0, 1);
      p = T_WORLD_SUBMAP * T_SUBMAP_BASELINK * p;
      pcl::PointXYZRGBL point;
      point.x = p[0];
      point.y = p[1];
      point.z = p[2];
      point.label = pose_stamped.stamp.toSec();
      cloud.push_back(point);
    }
  }

  std::string output_file = output_path + "global_map_trajectory_optimized.pcd";
  BEAM_INFO("Saving trajectory cloud to: {}", output_file);
  std::string error_message;
  if (!beam::SavePointCloud<pcl::PointXYZRGBL>(
          output_file, cloud, beam::PointCloudFileType::PCDBINARY,
          error_message)) {
    BEAM_ERROR("Unable to save trajectory cloud. Reason: {}", error_message);
  }

  if (!save_initial) { return; }

  // get trajectory initial
  pcl::PointCloud<pcl::PointXYZRGBL> cloud_initial;
  for (uint16_t i = 0; i < online_submaps_.size(); i++) {
    const SubmapPtr& submap = online_submaps_.at(i);
    Eigen::Matrix4d T_WORLD_SUBMAP = submap->T_WORLD_SUBMAP_INIT();
    auto poses_stamped = submap->GetTrajectory();
    for (const Submap::PoseStamped& pose_stamped : poses_stamped) {
      Eigen::Vector4d p(0, 0, 0, 1);
      const Eigen::Matrix4d& T_SUBMAP_BASELINK = pose_stamped.pose;
      p = T_WORLD_SUBMAP * T_SUBMAP_BASELINK * p;
      pcl::PointXYZRGBL point;
      point.x = p[0];
      point.y = p[1];
      point.z = p[2];
      point.label = pose_stamped.stamp.toSec();
      cloud_initial.push_back(point);
    }
  }

  if (!beam::SavePointCloud<pcl::PointXYZRGBL>(
          output_path + "global_map_trajectory_initial.pcd", cloud_initial,
          beam::PointCloudFileType::PCDBINARY, error_message)) {
    BEAM_ERROR("Unable to save trajectory cloud. Reason: {}", error_message);
  }
}

void GlobalMap::SaveSubmapFrames(const std::string& output_path,
                                 bool save_initial) {
  if (!boost::filesystem::exists(output_path)) {
    BEAM_ERROR("Invalid output path, not saving submap frames. Input: {}",
               output_path);
    return;
  }

  pcl::PointCloud<pcl::PointXYZRGBL> cloud;
  for (uint16_t i = 0; i < online_submaps_.size(); i++) {
    const SubmapPtr& submap = online_submaps_.at(i);
    pcl::PointCloud<pcl::PointXYZRGBL> frame =
        beam::CreateFrameCol(submap->Stamp());
    pcl::PointCloud<pcl::PointXYZRGBL> frame_transformed;
    pcl::transformPointCloud(frame, frame_transformed,
                             submap->T_WORLD_SUBMAP());
    cloud += frame_transformed;
  }
  std::string output_file =
      output_path + "global_map_submap_poses_optimized.pcd";
  BEAM_INFO("Saving submap frames to: {}", output_file);
  std::string error_message{};
  if (!beam::SavePointCloud<pcl::PointXYZRGBL>(
          output_file, cloud, beam::PointCloudFileType::PCDBINARY,
          error_message)) {
    BEAM_ERROR("Unable to save trajectory cloud. Reason: {}", error_message);
  }

  if (!save_initial) { return; }

  pcl::PointCloud<pcl::PointXYZRGBL> cloud_initial;
  for (uint16_t i = 0; i < online_submaps_.size(); i++) {
    const SubmapPtr& submap = online_submaps_.at(i);
    pcl::PointCloud<pcl::PointXYZRGBL> frame =
        beam::CreateFrameCol(submap->Stamp());
    pcl::PointCloud<pcl::PointXYZRGBL> frame_transformed;
    pcl::transformPointCloud(frame, frame_transformed,
                             submap->T_WORLD_SUBMAP_INIT());
    cloud_initial += frame_transformed;
  }
  std::string output_file_initial =
      output_path + "global_map_submap_poses_initial.pcd";
  BEAM_INFO("Saving submap frames to: {}", output_file_initial);
  if (!beam::SavePointCloud<pcl::PointXYZRGBL>(
          output_file_initial, cloud_initial,
          beam::PointCloudFileType::PCDBINARY, error_message)) {
    BEAM_ERROR("Unable to save submap frames cloud. Reason: {}", error_message);
  }
}

void GlobalMap::AddRosSubmap(int submap_id) {
  const auto& submap_ptr = online_submaps_.at(submap_id);

  // get all lidar points in pcl pointcloud
  PointCloud new_submap_pcl_cloud =
      submap_ptr->GetLidarPointsInWorldFrameCombined(true);

  sensor_msgs::PointCloud2 pointcloud2_msg;

  if (new_submap_pcl_cloud.size() > 0) {
    // filter cloud
    new_submap_pcl_cloud = beam_filtering::FilterPointCloud(
        new_submap_pcl_cloud, params_.ros_submap_filter_params);

    // convert to PointCloud2
    pointcloud2_msg = beam::PCLToROS<pcl::PointXYZ>(
        new_submap_pcl_cloud, submap_ptr->Stamp(),
        extrinsics_->GetWorldFrameId(), submap_id + 1);

    // set cloud
    RosMap new_ros_map_lidar(RosMapType::LIDARSUBMAP, pointcloud2_msg);
    ros_submaps_.push(std::make_shared<RosMap>(new_ros_map_lidar));
  }

  // get all camera keypoints in pcl pointcloud
  new_submap_pcl_cloud = submap_ptr->GetKeypointsInWorldFrame(true);

  if (new_submap_pcl_cloud.size() > 0) {
    // convert to PointCloud2
    pointcloud2_msg = beam::PCLToROS<pcl::PointXYZ>(
        new_submap_pcl_cloud, submap_ptr->Stamp(),
        extrinsics_->GetWorldFrameId(), submap_id + 1);

    // set cloud
    RosMap new_ros_map_keypoints(RosMapType::VISUALSUBMAP, pointcloud2_msg);
    ros_submaps_.push(std::make_shared<RosMap>(new_ros_map_keypoints));
  }

  // clear submaps if there are too many in the queue;
  while (ros_submaps_.size() > max_num_ros_submaps_) { ros_submaps_.pop(); }
}

void GlobalMap::AddRosGlobalMap() {
  PointCloud global_lidar_map;
  PointCloud global_keypoints_map;

  for (const auto& submap_ptr : online_submaps_) {
    // get all lidar points in pcl pointcloud
    PointCloud new_submap_pcl_cloud;
    std::vector<PointCloud> new_submap_points =
        submap_ptr->GetLidarPointsInWorldFrame(10e6, false);
    for (const PointCloud& cloud : new_submap_points) {
      new_submap_pcl_cloud += cloud;
    }

    // filter submap
    new_submap_pcl_cloud = beam_filtering::FilterPointCloud(
        new_submap_pcl_cloud, params_.ros_submap_filter_params);

    // add to global
    global_lidar_map += new_submap_pcl_cloud;

    // get all keypoints in pcl pointcloud
    new_submap_pcl_cloud = submap_ptr->GetKeypointsInWorldFrame(false);

    // add to global
    global_keypoints_map += new_submap_pcl_cloud;
  }

  if (global_lidar_map.size() > 0) {
    // filter global map
    global_lidar_map = beam_filtering::FilterPointCloud(
        global_lidar_map, params_.ros_globalmap_filter_params);

    // convert lidar map to PointCloud2
    sensor_msgs::PointCloud2 pointcloud2_msg = beam::PCLToROS<pcl::PointXYZ>(
        global_lidar_map, last_update_time_, extrinsics_->GetWorldFrameId(),
        global_map_updates_);

    // set cloud
    RosMap new_ros_map_lidar(RosMapType::LIDARGLOBALMAP, pointcloud2_msg);
    ros_global_lidar_map_ = std::make_shared<RosMap>(new_ros_map_lidar);
  }

  if (global_keypoints_map.size() > 0) {
    // convert lidar map to PointCloud2
    sensor_msgs::PointCloud2 pointcloud2_msg = beam::PCLToROS<pcl::PointXYZ>(
        global_keypoints_map, last_update_time_, extrinsics_->GetWorldFrameId(),
        global_map_updates_);

    // set cloud
    RosMap new_ros_map_keypoints(RosMapType::VISUALGLOBALMAP, pointcloud2_msg);
    ros_global_keypoints_map_ = std::make_shared<RosMap>(new_ros_map_keypoints);
  }
}

void GlobalMap::AddNewRosScan(const PointCloud& cloud,
                              const Eigen::Matrix4d& T_WORLD_BASELINK,
                              const ros::Time& stamp) {
  Eigen::Matrix4d T_BASELINK_LIDAR;
  if (!extrinsics_->GetT_BASELINK_LIDAR(T_BASELINK_LIDAR)) {
    BEAM_ERROR("Cannot get extrinsics, not publishing new lidar scans");
    store_new_scans_ = false;
    return;
  }

  PointCloud cloud_in_world_frame;
  Eigen::Matrix4d T_WORLD_LIDAR = T_WORLD_BASELINK * T_BASELINK_LIDAR;
  pcl::transformPointCloud(cloud, cloud_in_world_frame, T_WORLD_LIDAR);

  // convert to PointCloud2
  sensor_msgs::PointCloud2 pointcloud2_msg = beam::PCLToROS<pcl::PointXYZ>(
      cloud_in_world_frame, stamp, extrinsics_->GetWorldFrameId(),
      new_scans_counter_);
  new_scans_counter_++;

  // set cloud
  RosMap new_ros_map(RosMapType::LIDARNEW, pointcloud2_msg);
  ros_new_scans_.push(std::make_shared<RosMap>(new_ros_map));

  // clear maps if there are too many in the queue;
  while (ros_new_scans_.size() > max_num_new_scans_) { ros_new_scans_.pop(); }
}

void GlobalMap::FillSubmapMsg(bs_common::SubmapMsg& submap_msg,
                              const PointCloud& lidar_points,
                              const beam_matching::LoamPointCloud& loam_points,
                              const PointCloud& keypoints,
                              const std::vector<uint32_t> word_ids,
                              const Eigen::Matrix4d& T) const {
  PointCloud lidar_points_in_wlm_frame;
  pcl::transformPointCloud(lidar_points, lidar_points_in_wlm_frame, T);
  PointCloud keypoints_in_wlm_frame;
  pcl::transformPointCloud(keypoints, keypoints_in_wlm_frame, T);
  beam_matching::LoamPointCloud loam_points_in_wlm_frame(loam_points, T);

  std::vector<geometry_msgs::Vector3> points_vec;

  // add lidar points
  submap_msg.lidar_map.frame_id = extrinsics_->GetWorldFrameId();
  for (const auto& p : lidar_points_in_wlm_frame) {
    geometry_msgs::Vector3 point;
    point.x = p.x;
    point.y = p.y;
    point.z = p.z;
    points_vec.push_back(point);
  }
  submap_msg.lidar_map.lidar_points = points_vec;

  // add loam points
  points_vec.clear();
  for (const auto& p : loam_points_in_wlm_frame.edges.strong.cloud) {
    geometry_msgs::Vector3 point;
    point.x = p.x;
    point.y = p.y;
    point.z = p.z;
    points_vec.push_back(point);
  }

  submap_msg.lidar_map.lidar_edges_strong = points_vec;
  points_vec.clear();
  for (const auto& p : loam_points_in_wlm_frame.edges.weak.cloud) {
    geometry_msgs::Vector3 point;
    point.x = p.x;
    point.y = p.y;
    point.z = p.z;
    points_vec.push_back(point);
  }

  submap_msg.lidar_map.lidar_edges_weak = points_vec;
  points_vec.clear();
  for (const auto& p : loam_points_in_wlm_frame.surfaces.strong.cloud) {
    geometry_msgs::Vector3 point;
    point.x = p.x;
    point.y = p.y;
    point.z = p.z;
    points_vec.push_back(point);
  }

  submap_msg.lidar_map.lidar_surfaces_strong = points_vec;
  points_vec.clear();
  for (const auto& p : loam_points_in_wlm_frame.surfaces.weak.cloud) {
    geometry_msgs::Vector3 point;
    point.x = p.x;
    point.y = p.y;
    point.z = p.z;
    points_vec.push_back(point);
  }
  submap_msg.lidar_map.lidar_surfaces_weak = points_vec;

  // add keypoints
  points_vec.clear();
  for (const auto& p : keypoints_in_wlm_frame) {
    geometry_msgs::Vector3 point;
    point.x = p.x;
    point.y = p.y;
    point.z = p.z;
    points_vec.push_back(point);
  }
  submap_msg.visual_map_points = points_vec;

  // add descriptors
  submap_msg.visual_map_word_ids = word_ids;
}

}} // namespace bs_models::global_mapping
