#include <bs_models/global_mapping/global_map_refinement.h>

#include <fuse_core/transaction.h>
#include <fuse_graphs/hash_graph.h>

#include <bs_common/utils.h>
#include <bs_models/lidar/scan_pose.h>
#include <bs_models/reloc/reloc_methods.h>

namespace bs_models { namespace global_mapping {

using namespace reloc;

void GlobalMapRefinement::Params::LoadJson(const std::string& config_path) {
  if (config_path.empty()) {
    BEAM_INFO("No config file provided to global map refinement, using default "
              "parameters.");
    return;
  }

  BEAM_INFO("Loading global map refinement config file: {}", config_path);

  nlohmann::json J;
  if (!beam::ReadJson(config_path, J)) {
    BEAM_ERROR("Unable to read global map refinement config");
    throw std::runtime_error{"Unable to read global map refinement config"};
  }

  bs_common::ValidateJsonKeysOrThrow(
      std::vector<std::string>{"loop_closure", "submap_refinement"}, J);

  // load loop closure params
  nlohmann::json J_loop_closure = J["loop_closure"];
  bs_common::ValidateJsonKeysOrThrow(
      std::vector<std::string>{"candidate_search_config", "refinement_config"},
      J_loop_closure);

  std::string candidate_search_config_rel =
      J_loop_closure["candidate_search_config"];
  if (!candidate_search_config_rel.empty()) {
    loop_closure.candidate_search_config = beam::CombinePaths(
        bs_common::GetBeamSlamConfigPath(), candidate_search_config_rel);
  }

  std::string refinement_config_rel = J_loop_closure["refinement_config"];
  if (!refinement_config_rel.empty()) {
    loop_closure.refinement_config = beam::CombinePaths(
        bs_common::GetBeamSlamConfigPath(), refinement_config_rel);
  }

  // load submap refinement params
  nlohmann::json J_submap_refinement = J["submap_refinement"];
  bs_common::ValidateJsonKeysOrThrow(
      std::vector<std::string>{"scan_registration_config", "matcher_config",
                               "registration_results_output_path"},
      J_submap_refinement);

  std::string scan_registration_config_rel =
      J_submap_refinement["scan_registration_config"];
  if (!scan_registration_config_rel.empty()) {
    submap_refinement.scan_registration_config = beam::CombinePaths(
        bs_common::GetBeamSlamConfigPath(), scan_registration_config_rel);
  }

  std::string matcher_config_rel = J_submap_refinement["matcher_config"];
  if (!scan_registration_config_rel.empty()) {
    submap_refinement.matcher_config = beam::CombinePaths(
        bs_common::GetBeamSlamConfigPath(), matcher_config_rel);
  }

  submap_refinement.registration_results_output_path =
      J_submap_refinement["registration_results_output_path"];
}

GlobalMapRefinement::GlobalMapRefinement(const std::string& global_map_data_dir,
                                         const Params& params)
    : params_(params) {
  Setup();

  // load global map to get submaps
  BEAM_INFO("Loading global map data from: {}", global_map_data_dir);
  global_map_ = std::make_shared<GlobalMap>(global_map_data_dir);
  submaps_ = global_map_->GetSubmaps();
}

GlobalMapRefinement::GlobalMapRefinement(const std::string& global_map_data_dir,
                                         const std::string& config_path) {
  // load params & setup
  params_.LoadJson(config_path);
  Setup();

  // load global map to get submaps
  BEAM_INFO("Loading global map data from: {}", global_map_data_dir);
  global_map_ = std::make_shared<GlobalMap>(global_map_data_dir);
  submaps_ = global_map_->GetSubmaps();
}

GlobalMapRefinement::GlobalMapRefinement(std::shared_ptr<GlobalMap>& global_map,
                                         const Params& params)
    : global_map_(global_map), params_(params) {
  submaps_ = global_map_->GetSubmaps();
  Setup();
}

GlobalMapRefinement::GlobalMapRefinement(std::shared_ptr<GlobalMap>& global_map,
                                         const std::string& config_path)
    : global_map_(global_map) {
  params_.LoadJson(config_path);
  submaps_ = global_map_->GetSubmaps();
  Setup();
}

void GlobalMapRefinement::Setup() {
  // initiate reloc candidate search
  loop_closure_candidate_search_ = reloc::RelocCandidateSearchBase::Create(
      params_.loop_closure.candidate_search_config);

  // initiate reloc refinement
  loop_closure_refinement_ = reloc::RelocRefinementBase::Create(
      params_.loop_closure.refinement_config);
}

bool GlobalMapRefinement::RunSubmapRefinement() {
  for (uint16_t i = 0; i < submaps_.size(); i++) {
    BEAM_INFO("Refining submap No. {}", static_cast<int>(i));
    if (!RefineSubmap(submaps_.at(i))) {
      BEAM_ERROR("Submap refinement failed, exiting.");
      return false;
    }
  }
  return true;
}

bool GlobalMapRefinement::RefineSubmap(SubmapPtr& submap) {
  // Create optimization graph
  std::shared_ptr<fuse_graphs::HashGraph> graph =
      fuse_graphs::HashGraph::make_shared();

  std::unique_ptr<sr::ScanRegistrationBase> scan_registration =
      sr::ScanRegistrationBase::Create(
          params_.submap_refinement.scan_registration_config,
          params_.submap_refinement.matcher_config,
          params_.submap_refinement.registration_results_output_path, true);

  // clear lidar map
  scan_registration->GetMapMutable().Clear();

  // iterate through stored scan poses and add scan registration factors to the
  // graph
  BEAM_INFO("Registering scans");
  for (auto scan_iter = submap->LidarKeyframesBegin();
       scan_iter != submap->LidarKeyframesEnd(); scan_iter++) {
    const bs_models::ScanPose& scan_pose = scan_iter->second;
    auto transaction =
        scan_registration->RegisterNewScan(scan_pose).GetTransaction();
    if (transaction != nullptr) {
      // std::cout << "\nADDING TRANSACTION: \n";
      // transaction->print();
      graph->update(*transaction);
    }
  }

  // TODO: Add visual BA constraints

  // Optimize graph and update data
  BEAM_INFO("Optimizing graph");
  graph->optimize();

  BEAM_INFO("updating scan poses");
  for (auto scan_iter = submap->LidarKeyframesBegin();
       scan_iter != submap->LidarKeyframesEnd(); scan_iter++) {
    scan_iter->second.UpdatePose(graph);
  }

  // TODO: update visual data (just frame poses?)

  return true;
}

bool GlobalMapRefinement::RunPoseGraphOptimization() {
  // TODO
  BEAM_ERROR("PGO NOT YET IMPLEMENTED");
  return true;
}

void GlobalMapRefinement::SaveResults(const std::string& output_path,
                                      bool save_initial) {
  // create results directory
  if (!boost::filesystem::exists(output_path)) {
    BEAM_ERROR(
        "Output directory does not exist, not outputting global map refinement "
        "results. Input: {}",
        output_path);
    return;
  }

  // save
  global_map_->SaveTrajectoryFile(output_path, save_initial);
  global_map_->SaveTrajectoryClouds(output_path, save_initial);
  global_map_->SaveSubmapFrames(output_path, save_initial);
  global_map_->SaveLidarSubmaps(output_path, save_initial);
  global_map_->SaveKeypointSubmaps(output_path, save_initial);
}

void GlobalMapRefinement::SaveGlobalMapData(const std::string& output_path) {
  // create results directory
  if (!boost::filesystem::exists(output_path)) {
    BEAM_ERROR(
        "Output directory does not exist, not outputting global map refinement "
        "results. Input: {}",
        output_path);
    return;
  }

  std::string save_dir;
  if (output_path.back() != '/') {
    save_dir = output_path + "/global_map_data_refined/";
  } else {
    save_dir = output_path + "global_map_data_refined/";
  }
  boost::filesystem::create_directory(save_dir);

  // save
  global_map_->SaveData(save_dir);
}

}} // namespace bs_models::global_mapping