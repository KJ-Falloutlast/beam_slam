#pragma once

#include <map>

#include <ros/time.h>

#include <beam_utils/pointclouds.h>
#include <global_mapping/loop_closure/loop_closure_candidate_search_base.h>

namespace global_mapping {

/**
 * @brief This class implements a loop closure candidate search class. To
 * look for candidate loop closures, this class simply looks through all
 * previous submaps and calculates the norm between the query submap and the
 * candidate loop closure submap. If the norm is below some threshold, then the
 * candidate is return along with the relative pose (from current submap pose
 * estimates) between the two.
 */
class LoopClosureCandidateSearchEucDist
    : public LoopClosureCandidateSearchBase {
 public:
  // Inherit base class constructors
  using LoopClosureCandidateSearchBase::LoopClosureCandidateSearchBase;

  /**
   * @brief another constructor that takes in parameters
   * @param distance_threshold_m submaps closer than this will be considered as
   * loop closure candidates
   */
  LoopClosureCandidateSearchEucDist(double distance_threshold_m);

  /**
   * @brief Overrides the virtual function that takes in a vector of submaps, a
   * query index and finds candidate loop closures with an estimated relative
   * pose
   * @param submaps vector of submaps
   * @param query_index index of submap to check for loops
   * @param matched_indices reference to vector of indices which represent the
   * candidate loop closure submap indices
   * @param estimated_poses reference to vector of transforms from query submap
   * to matched submap (T_MATCH_QUERY)
   */
  void FindLoopClosureCandidates(
      const std::vector<Submap>& submaps, int query_index,
      std::vector<int>& matched_indices,
      std::vector<Eigen::Matrix4d, pose_allocator>& estimated_poses) override;

 private:
  /**
   * @brief Method for loading a config json file.
   */
  void LoadConfig() override;

  double distance_threshold_m_{5};
};

}  // namespace global_mapping