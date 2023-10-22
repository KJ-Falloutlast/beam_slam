#pragma once

#include <map>

#include <fuse_core/transaction.h>
#include <ros/time.h>

#include <beam_utils/pointclouds.h>
#include <bs_models/global_mapping/submap.h>

namespace bs_models::reloc {

struct RelocRefinementResults {
  Eigen::Matrix4d T_MATCH_QUERY{Eigen::Matrix4d::Identity()};
  std::optional<Eigen::Matrix<double, 6, 6>> covariance;
  bool successful{false};
};

/**
 * @brief A reloc refinement step that takes an estimated pose
 * from the candidate search and refines the relative pose between the two
 * candidate locations.
 */
class RelocRefinementBase {
public:
  /**
   * @brief constructor with a required path to a json config
   * @param config path to json config file. If empty, it will use default
   * parameters
   */
  RelocRefinementBase(const std::string& config) : config_path_(config){};

  /**
   * @brief default destructor
   */
  ~RelocRefinementBase() = default;

  /**
   * @brief Pure virtual function that runs the refinement between two
   * candidate reloc submaps.
   * @param matched_submap
   * @param query_submap
   */
  virtual RelocRefinementResults
      RunRefinement(const global_mapping::SubmapPtr& matched_submap,
                    const global_mapping::SubmapPtr& query_submap,
                    const Eigen::Matrix4d& T_MATCH_QUERY_EST) = 0;

  /**
   * @brief Factory method to create a object at runtime given a config file
   */
  static std::shared_ptr<RelocRefinementBase>
      Create(const std::string& config_path);

protected:
  std::string config_path_;

  /* Debugging tools that can only be set here */
  bool output_results_{false};
  std::string debug_output_path_{"/userhome/debug/reloc/"};
  std::string output_path_stamped_; // to be created in implementation
};

} // namespace bs_models::reloc
