#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <fuse_loss/cauchy_loss.h>
#include <ros/node_handle.h>
#include <ros/param.h>

#include <bs_common/utils.h>
#include <bs_parameters/parameter_base.h>

namespace bs_parameters { namespace models {

/**
 * @brief Defines the set of parameters required by the ScanMatcher class
 */
struct SLAMInitializationParams : public ParameterBase {
public:
  /**
   * @brief Method for loading parameter values from ROS.
   *
   * @param[in] nh - The ROS node handle with which to load parameters
   */
  void loadFromROS(const ros::NodeHandle& nh) final {
    // subscribing topics
    getParam<std::string>(nh, "imu_topic", imu_topic, "");
    getParam<std::string>(nh, "lidar_topic", lidar_topic, "");

    // config for an optional frame initializer
    std::string frame_initializer_config_rel;
    getParam<std::string>(nh, "frame_initializer_config",
                          frame_initializer_config_rel,
                          frame_initializer_config_rel);
    if (!frame_initializer_config_rel.empty()) {
      frame_initializer_config = beam::CombinePaths(
          bs_common::GetBeamSlamConfigPath(), frame_initializer_config_rel);
    }

    // path to optional output folder
    getParam<std::string>(nh, "output_folder", output_folder, output_folder);

    // mode for initializing, options: VISUAL, LIDAR
    getParam<std::string>(nh, "init_mode", init_mode, init_mode);
    if (init_mode != "VISUAL" && init_mode != "LIDAR") {
      ROS_ERROR("Invalid init mode type, options: 'VISUAL', 'LIDAR'.");
    }
    // maximum optimizaiton time in seconds
    getParam<double>(nh, "max_optimization_s", max_optimization_s, 1.0);

    // minimum acceptable trajectory length to intialize (if using frame init is
    // given or using LIDAR)
    getParam<double>(nh, "min_trajectory_length_m", min_trajectory_length_m,
                     min_trajectory_length_m);

    // minimum acceptable parallax to intialize (if using frame init is
    // given or using VISUAL)
    getParam<double>(nh, "min_visual_parallax", min_visual_parallax,
                     min_visual_parallax);

    // weighting factor on inertial measurements
    // This gets applied to the sqrt inv cov such that: E = (w sqrt(cov^-1)) *
    // Residuals
    getParam<double>(nh, "inertial_info_weight", inertial_info_weight,
                     inertial_info_weight);

    // size of init window in seconds. This controls the data buffers, this
    // should be larger than the amount of time it takes to produce the min.
    // traj.
    getParam<double>(nh, "initialization_window_s", initialization_window_s,
                     initialization_window_s);

    // weighting factor on visual measurements
    // This gets applied to the sqrt inv cov such that: E = (w sqrt(cov^-1)) *
    // Residuals
    getParam<double>(nh, "reprojection_information_weight",
                     reprojection_information_weight,
                     reprojection_information_weight);

    // weighting factor on lidar scan registration measurements
    // This gets applied to the sqrt inv cov such that: E = (w sqrt(cov^-1)) *
    // Residuals
    getParam<double>(nh, "lidar_information_weight", lidar_information_weight,
                     lidar_information_weight);

    std::string matcher_config_rel;
    getParam<std::string>(nh, "matcher_config", matcher_config_rel,
                          matcher_config_rel);
    if (!matcher_config_rel.empty()) {
      matcher_config = beam::CombinePaths(bs_common::GetBeamSlamConfigPath(),
                                          matcher_config_rel);
    }

    double reprojection_loss_a = 5.0 * reprojection_information_weight;
    // reprojection loss
    reprojection_loss =
        std::make_shared<fuse_loss::CauchyLoss>(reprojection_loss_a);

    // read vo params
    std::string vo_params = beam::CombinePaths(
        bs_common::GetBeamSlamConfigPath(), "vo/vo_params.json");
    nlohmann::json J;
    if (!beam::ReadJson(vo_params, J)) {
      ROS_ERROR("Cannot read input VO Params, using default.");
    } else {
      max_triangulation_distance = J["max_triangulation_distance"];
    }
  }

  std::string visual_measurement_topic{
      "/local_mapper/visual_feature_tracker/visual_measurements"};
  std::string imu_topic{""};
  std::string lidar_topic{""};
  std::string frame_initializer_config{""};
  std::string init_mode{"FRAMEINIT"};
  std::string output_folder{""};

  std::string matcher_config;
  double max_optimization_s{1.0};
  double inertial_info_weight{0.001};
  double reprojection_information_weight{1.0};
  double lidar_information_weight{1.0};
  double min_trajectory_length_m{2.0};
  double min_visual_parallax{40.0};
  double frame_init_frequency{0.1};
  double max_triangulation_distance{30.0};
  double initialization_window_s{10.0};
  fuse_core::Loss::SharedPtr reprojection_loss;
};
}} // namespace bs_parameters::models
