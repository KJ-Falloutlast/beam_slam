#include <gtest/gtest.h>

#include <ctime>
#include <random>

#include <fuse_constraints/absolute_pose_3d_stamped_constraint.h>
#include <fuse_core/constraint.h>
#include <fuse_core/uuid.h>
#include <fuse_core/variable.h>
#include <fuse_graphs/hash_graph.h>
#include <pcl/io/pcd_io.h>

#include <beam_filtering/VoxelDownsample.h>
#include <beam_matching/Matchers.h>
#include <beam_utils/math.h>
#include <beam_utils/pointclouds.h>

#include <beam_common/sensor_proc.h>
#include <beam_common/utils.h>
#include <beam_models/frame_to_frame/scan_matcher_3d.h>

using namespace beam_models;
using namespace frame_to_frame;
using namespace beam_common;

class Data {
 public:
  Data() {
    // read input cloud
    std::string current_file = "scan_matcher_3d_tests.cpp";
    std::string test_path = __FILE__;
    test_path.erase(test_path.end() - current_file.size(), test_path.end());
    std::string scan_path = test_path + "data/testscan.pcd";
    PointCloud test_cloud_tmp;
    PointCloud test_cloud;
    pcl::io::loadPCDFile(scan_path, test_cloud_tmp);

    // downsample input cloud
    Eigen::Vector3f scan_voxel_size(0.05, 0.05, 0.05);
    beam_filtering::VoxelDownsample downsampler(scan_voxel_size);
    downsampler.Filter(test_cloud_tmp, test_cloud);

    // create poses
    // srand(time(NULL));
    double max_pose_rot{20};
    double max_pose_trans{1};
    double max_pert_rot{10};
    double max_pert_trans{0.05};

    T_WORLD_S1 = Eigen::Matrix4d::Identity();
    Eigen::VectorXd perturb(6);
    perturb << beam::randf(max_pose_rot, -max_pose_rot),
        beam::randf(max_pose_rot, -max_pose_rot),
        beam::randf(max_pose_rot, -max_pose_rot),
        beam::randf(max_pose_trans, -max_pose_trans),
        beam::randf(max_pose_trans, -max_pose_trans),
        beam::randf(max_pose_trans, -max_pose_trans);
    T_WORLD_S2 = beam::PerturbTransformDegM(T_WORLD_S1, perturb);
    Eigen::VectorXd perturb2(6);
    perturb2 << beam::randf(max_pert_rot, -max_pert_rot),
        beam::randf(max_pert_rot, -max_pert_rot),
        beam::randf(max_pert_rot, -max_pert_rot),
        beam::randf(max_pert_trans, -max_pert_trans),
        beam::randf(max_pert_trans, -max_pert_trans),
        beam::randf(max_pert_trans, -max_pert_trans);
    T_WORLD_S2_pert = beam::PerturbTransformDegM(T_WORLD_S2, perturb2);
    T_S1_S2 = beam::InvertTransform(T_WORLD_S1) * T_WORLD_S2;

    Eigen::VectorXd perturb3(6);
    perturb3 << beam::randf(max_pose_rot, -max_pose_rot),
        beam::randf(max_pose_rot, -max_pose_rot),
        beam::randf(max_pose_rot, -max_pose_rot),
        beam::randf(max_pose_trans, -max_pose_trans),
        beam::randf(max_pose_trans, -max_pose_trans),
        beam::randf(max_pose_trans, -max_pose_trans);
    T_WORLD_S3 = beam::PerturbTransformDegM(T_WORLD_S2, perturb3);
    Eigen::VectorXd perturb4(6);
    perturb4 << beam::randf(max_pert_rot, -max_pert_rot),
        beam::randf(max_pert_rot, -max_pert_rot),
        beam::randf(max_pert_rot, -max_pert_rot),
        beam::randf(max_pert_trans, -max_pert_trans),
        beam::randf(max_pert_trans, -max_pert_trans),
        beam::randf(max_pert_trans, -max_pert_trans);
    T_WORLD_S3_pert = beam::PerturbTransformDegM(T_WORLD_S3, perturb4);
    T_S1_S3 = beam::InvertTransform(T_WORLD_S1) * T_WORLD_S3;
    T_S2_S3 = beam::InvertTransform(T_WORLD_S2) * T_WORLD_S3;

    // create scans
    S1 = test_cloud;
    pcl::transformPointCloud(S1, S2, beam::InvertTransform(T_S1_S2));
    pcl::transformPointCloud(S1, S3, beam::InvertTransform(T_S1_S3));

    // create scan matcher params
    matcher_params.max_corr = 1;
    matcher_params.max_iter = 50;
    matcher_params.t_eps = 1e-8;
    matcher_params.fit_eps = 1e-2;
    matcher_params.lidar_ang_covar = 7.78e-9;
    matcher_params.lidar_lin_covar = 2.5e-4;
    matcher_params.multiscale_steps = 0;
    matcher_params.res = 0;

    // create prior that will be used in optimization
    ScanPose SP_TMP(ros::Time(0), T_WORLD_S1, S1);

    fuse_core::Vector7d mean;
    mean << SP_TMP.Position().x(), SP_TMP.Position().y(), SP_TMP.Position().z(),
        SP_TMP.Orientation().w(), SP_TMP.Orientation().x(),
        SP_TMP.Orientation().y(), SP_TMP.Orientation().z();
    fuse_core::Matrix6d prior_covariance;
    prior_covariance.setIdentity();
    prior_covariance = prior_covariance * 0.0000000001;

    prior = std::make_shared<fuse_constraints::AbsolutePose3DStampedConstraint>(
        "PRIOR", SP_TMP.Position(), SP_TMP.Orientation(), mean,
        prior_covariance);
  }

  Eigen::Matrix4d T_WORLD_S1;
  Eigen::Matrix4d T_WORLD_S2;
  Eigen::Matrix4d T_WORLD_S2_pert;
  Eigen::Matrix4d T_WORLD_S3;
  Eigen::Matrix4d T_WORLD_S3_pert;
  Eigen::Matrix4d T_S1_S2;
  Eigen::Matrix4d T_S1_S3;
  Eigen::Matrix4d T_S2_S3;
  PointCloud S1;
  PointCloud S2;
  PointCloud S3;
  beam_matching::IcpMatcherParams matcher_params;
  fuse_constraints::AbsolutePose3DStampedConstraint::SharedPtr prior;
};

Data data_;

void OutputTransformInformation(const Eigen::Matrix4d& T,
                                const std::string& transform_name) {
  Eigen::Matrix3d R = T.block(0, 0, 3, 3);
  Eigen::Vector3d rpy = R.eulerAngles(0, 1, 2);
  std::cout << transform_name << ":\n"
            << "R: \n"
            << R << "\n"
            << "t: [" << T(0, 3) << ", " << T(1, 3) << ", " << T(2, 3) << "]\n"
            << "rpy (deg): [" << beam::Rad2Deg(beam::WrapToPi(rpy[0])) << ", "
            << beam::Rad2Deg(beam::WrapToPi(rpy[1])) << ", "
            << beam::Rad2Deg(beam::WrapToPi(rpy[2])) << "]\n";
}

fuse_constraints::RelativePose3DStampedConstraint::SharedPtr CreateConstraint(
    const fuse_variables::Position3DStamped& position1,
    const fuse_variables::Orientation3DStamped& orientation1,
    const fuse_variables::Position3DStamped& position2,
    const fuse_variables::Orientation3DStamped& orientation2,
    const Eigen::Matrix4d& T_CLOUD1_CLOUD2) {
  // convert rotation matrix to quaternion
  Eigen::Matrix3d R = T_CLOUD1_CLOUD2.block(0, 0, 3, 3);
  Eigen::Quaterniond q(R);

  // Convert measurement to fuse variable
  fuse_core::Vector7d pose_relative_mean;
  pose_relative_mean << T_CLOUD1_CLOUD2(0, 3), T_CLOUD1_CLOUD2(1, 3),
      T_CLOUD1_CLOUD2(2, 3), q.w(), q.x(), q.y(), q.z();

  // create covariance
  Eigen::Matrix<double, 6, 6> covariance;
  covariance.setIdentity();
  covariance = covariance * 0.1;

  // Create a relative pose constraint. We assume the pose measurements are
  // independent.
  auto constraint =
      fuse_constraints::RelativePose3DStampedConstraint::make_shared(
          "SOURCE", position1, orientation1, position2, orientation2,
          pose_relative_mean, covariance);
  return constraint;
}

int AddConstraints(const fuse_core::Transaction::SharedPtr& transaction,
                   fuse_graphs::HashGraph& graph) {
  int counter = 0;
  auto added_constraints = transaction->addedConstraints();
  for (auto iter = added_constraints.begin(); iter != added_constraints.end();
       iter++) {
    auto constraint =
        dynamic_cast<const fuse_constraints::RelativePose3DStampedConstraint&>(
            *iter);
    fuse_core::Constraint::SharedPtr constraint_ptr =
        fuse_constraints::RelativePose3DStampedConstraint::make_shared(
            constraint);
    graph.addConstraint(constraint_ptr);
    counter++;
  }
  return counter;
}

std::vector<fuse_core::UUID> AddVariables(
    const fuse_core::Transaction::SharedPtr& transaction,
    fuse_graphs::HashGraph& graph) {
  fuse_variables::Position3DStamped dummy_position;
  fuse_variables::Orientation3DStamped dummy_orientation;
  std::vector<fuse_core::UUID> uuids;
  auto added_variables = transaction->addedVariables();
  for (auto iter = added_variables.begin(); iter != added_variables.end();
       iter++) {
    if (iter->type() == dummy_position.type()) {
      auto var = dynamic_cast<const fuse_variables::Position3DStamped&>(*iter);
      fuse_core::Variable::SharedPtr var_ptr =
          fuse_variables::Position3DStamped::make_shared(var);
      graph.addVariable(var_ptr);
    } else if (iter->type() == dummy_orientation.type()) {
      auto var =
          dynamic_cast<const fuse_variables::Orientation3DStamped&>(*iter);
      fuse_core::Variable::SharedPtr var_ptr =
          fuse_variables::Orientation3DStamped::make_shared(var);
      graph.addVariable(var_ptr);
    } else {
      return uuids;
    }
    uuids.push_back(iter->uuid());
  }
  return uuids;
}

bool VectorsEqual(double* v1, double* v2, int vsize) {
  double precision = 0.001;
  for (int i = 0; i < vsize; i++) {
    if (std::abs(v1[i] - v2[i]) > precision) {
      return false;
    }
  }
  return true;
}

TEST(ScanPose, IO) {
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2);

  Eigen::Matrix4d T_WORLD_S2_ = SP2.T_REFFRAME_CLOUD();
  Eigen::Matrix4d T_WORLD_S1_ = SP1.T_REFFRAME_CLOUD();
  EXPECT_TRUE(beam::ArePosesEqual(data_.T_WORLD_S1, T_WORLD_S1_, 0.1, 0.001));
  EXPECT_EQ(SP1.Cloud().size(), data_.S1.size());
  EXPECT_EQ(SP2.Cloud().size(), data_.S2.size());
  EXPECT_EQ(SP1.Stamp(), ros::Time(0));
  EXPECT_EQ(SP2.Stamp(), ros::Time(1));
}

TEST(ScanPose, 2NodeFG) {
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2);

  // Create the graph
  fuse_graphs::HashGraph graph;

  // Add variables
  fuse_variables::Position3DStamped::SharedPtr p1 =
      fuse_variables::Position3DStamped::make_shared(SP1.Position());
  fuse_variables::Orientation3DStamped::SharedPtr o1 =
      fuse_variables::Orientation3DStamped::make_shared(SP1.Orientation());
  fuse_variables::Position3DStamped::SharedPtr p2 =
      fuse_variables::Position3DStamped::make_shared(SP2.Position());
  fuse_variables::Orientation3DStamped::SharedPtr o2 =
      fuse_variables::Orientation3DStamped::make_shared(SP2.Orientation());

  graph.addVariable(p1);
  graph.addVariable(o1);
  graph.addVariable(p2);
  graph.addVariable(o2);

  // Add constraint
  auto constraint1 = CreateConstraint(*p1, *o1, *p2, *o2, data_.T_S1_S2);
  graph.addConstraint(constraint1);

  // Optimize the constraints and variables.
  graph.optimize();

  for (int i = 0; i < 3; i++) {
    EXPECT_TRUE(p1->data()[i] == SP1.Position().data()[i]);
    EXPECT_TRUE(p2->data()[i] == SP2.Position().data()[i]);
  }
  for (int i = 0; i < 4; i++) {
    EXPECT_TRUE(o1->data()[i] == SP1.Orientation().data()[i]);
    EXPECT_TRUE(o2->data()[i] == SP2.Orientation().data()[i]);
  }
}

TEST(ScanPose, 2NodeFGPert) {
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2);
  ScanPose SP2_pert(ros::Time(1), data_.T_WORLD_S2_pert, data_.S2);

  // Create the graph
  fuse_graphs::HashGraph graph;

  // Add variables
  fuse_variables::Position3DStamped::SharedPtr p1 =
      fuse_variables::Position3DStamped::make_shared(SP1.Position());
  fuse_variables::Orientation3DStamped::SharedPtr o1 =
      fuse_variables::Orientation3DStamped::make_shared(SP1.Orientation());
  fuse_variables::Position3DStamped::SharedPtr p2 =
      fuse_variables::Position3DStamped::make_shared(SP2_pert.Position());
  fuse_variables::Orientation3DStamped::SharedPtr o2 =
      fuse_variables::Orientation3DStamped::make_shared(SP2_pert.Orientation());
  graph.addVariable(p1);
  graph.addVariable(o1);
  graph.addVariable(p2);
  graph.addVariable(o2);

  // Add constraint between poses
  auto constraint1 = CreateConstraint(*p1, *o1, *p2, *o2, data_.T_S1_S2);
  graph.addConstraint(constraint1);

  // Add prior on first pose
  graph.addConstraint(data_.prior);

  // Optimize the constraints and variables.
  graph.optimize();

  for (int i = 0; i < 3; i++) {
    EXPECT_TRUE(std::abs(SP1.Position().data()[i] - p1->data()[i]) < 0.001);
    EXPECT_TRUE(std::abs(SP2.Position().data()[i] - p2->data()[i]) < 0.001);
  }
  for (int i = 0; i < 4; i++) {
    EXPECT_TRUE(std::abs(SP1.Orientation().data()[i] - o1->data()[i]) < 0.001);
    EXPECT_TRUE(std::abs(SP2.Orientation().data()[i] - o2->data()[i]) < 0.001);
  }
}

TEST(ScanPose, ScanRegistrationICP) {
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2);
  ScanPose SP2_pert(ros::Time(1), data_.T_WORLD_S2_pert, data_.S2);

  Eigen::Matrix4d T_S1_S2_initial =
      beam::InvertTransform(data_.T_WORLD_S1) * data_.T_WORLD_S2_pert;
  PointCloudPtr S2_RefFEst = std::make_shared<PointCloud>();
  pcl::transformPointCloud(SP2_pert.Cloud(), *S2_RefFEst,
                           Eigen::Affine3d(T_S1_S2_initial));

  pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> matcher;
  matcher.setInputSource(S2_RefFEst);
  matcher.setInputTarget(std::make_shared<PointCloud>(SP1.Cloud()));
  matcher.setMaximumIterations(50);
  matcher.setTransformationEpsilon(1e-8);
  matcher.setMaxCorrespondenceDistance(1);
  matcher.setEuclideanFitnessEpsilon(1e-2);

  PointCloud S2_RefFOpt1;
  matcher.align(S2_RefFOpt1);
  Eigen::Matrix4d T_S1Opt_S1Ini =
      matcher.getFinalTransformation().cast<double>();
  Eigen::Matrix4d T_S1_S2_opt = T_S1Opt_S1Ini * T_S1_S2_initial;

  PointCloud S2_RefFOpt2;
  pcl::transformPointCloud(SP2_pert.Cloud(), S2_RefFOpt2, T_S1_S2_opt);

  // check final transform is close to original
  EXPECT_NEAR(data_.T_S1_S2.norm(), T_S1_S2_opt.norm(), 0.001);
}

TEST(ScanPose, ScanRegistrationPGPert) {
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2);
  ScanPose SP2_pert(ros::Time(1), data_.T_WORLD_S2_pert, data_.S2);

  // run scan registration
  // * (1) transform second scan into estimated scan 1 frame
  // * (2) run scan macher
  // * (3) get estimated transform between two frames
  //
  Eigen::Matrix4d T_S1_S2_init =
      beam::InvertTransform(data_.T_WORLD_S1) * data_.T_WORLD_S2_pert;
  PointCloudPtr S2_RefFEst = std::make_shared<PointCloud>();
  pcl::transformPointCloud(SP2_pert.Cloud(), *S2_RefFEst,
                           Eigen::Affine3d(T_S1_S2_init));

  std::unique_ptr<beam_matching::IcpMatcher> matcher =
      std::make_unique<beam_matching::IcpMatcher>(data_.matcher_params);
  matcher->SetRef(S2_RefFEst);
  matcher->SetTarget(std::make_shared<PointCloud>(SP1.Cloud()));
  matcher->Match();
  matcher->EstimateInfo();

  Eigen::Matrix4d T_S1Opt_S1Ini = matcher->GetResult().matrix();
  Eigen::Matrix4d T_S1_S2_opt = T_S1Opt_S1Ini * T_S1_S2_init;
  Eigen::Matrix<double, 6, 6> covariance = matcher->GetInfo();

  Eigen::Matrix4d T_W_S2_opt = data_.T_WORLD_S1 * T_S1_S2_opt;

  // check final transform is close to original
  EXPECT_NEAR(data_.T_S1_S2.norm(), T_S1_S2_opt.norm(), 0.001);
  EXPECT_NEAR(data_.T_WORLD_S2.norm(), T_W_S2_opt.norm(), 0.001);

  // Create the graph
  fuse_graphs::HashGraph graph;

  // Add variables
  fuse_variables::Position3DStamped::SharedPtr p1 =
      fuse_variables::Position3DStamped::make_shared(SP1.Position());
  fuse_variables::Orientation3DStamped::SharedPtr o1 =
      fuse_variables::Orientation3DStamped::make_shared(SP1.Orientation());
  fuse_variables::Position3DStamped::SharedPtr p2 =
      fuse_variables::Position3DStamped::make_shared(SP2_pert.Position());
  fuse_variables::Orientation3DStamped::SharedPtr o2 =
      fuse_variables::Orientation3DStamped::make_shared(SP2_pert.Orientation());

  graph.addVariable(p1);
  graph.addVariable(o1);
  graph.addVariable(p2);
  graph.addVariable(o2);

  // Add constraint between poses
  auto constraint1 = CreateConstraint(*p1, *o1, *p2, *o2, T_S1_S2_opt);
  graph.addConstraint(constraint1);

  // Add prior on first pose
  graph.addConstraint(data_.prior);

  // Optimize the constraints and variables.
  graph.optimize();

  for (int i = 0; i < 3; i++) {
    EXPECT_TRUE(std::abs(SP1.Position().data()[i] - p1->data()[i]) < 0.001);
    EXPECT_TRUE(std::abs(SP2.Position().data()[i] - p2->data()[i]) < 0.001);
  }
  for (int i = 0; i < 4; i++) {
    EXPECT_TRUE(std::abs(SP1.Orientation().data()[i] - o1->data()[i]) < 0.001);
    EXPECT_TRUE(std::abs(SP2.Orientation().data()[i] - o2->data()[i]) < 0.001);
  }
}

TEST(MultiScanRegistration, 2Scans) {
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2);
  ScanPose SP2_pert(ros::Time(1), data_.T_WORLD_S2_pert, data_.S2);

  // init scan registration
  std::unique_ptr<beam_matching::IcpMatcher> matcher =
      std::make_unique<beam_matching::IcpMatcher>(data_.matcher_params);

  MultiScanRegistration::Params scan_reg_params{.num_neighbors = 1,
                                                .outlier_threshold_t = 1,
                                                .outlier_threshold_r = 30,
                                                .min_motion_trans_m = 0,
                                                .min_motion_rot_rad = 0,
                                                .source = "TEST",
                                                .lag_duration = 100,
                                                .fix_first_scan = false};

  std::unique_ptr<MultiScanRegistration> multi_scan_registration =
      std::make_unique<MultiScanRegistration>(std::move(matcher),
                                              scan_reg_params);

  Eigen::Matrix<double, 6, 6> covariance;
  covariance.setIdentity();
  covariance = covariance * 0.1;
  multi_scan_registration->SetFixedCovariance(covariance);

  auto transaction1 =
      multi_scan_registration->RegisterNewScan(SP1).GetTransaction();
  auto transaction2 =
      multi_scan_registration->RegisterNewScan(SP2_pert).GetTransaction();

  // validate stamps
  EXPECT_EQ(transaction1->stamp(), SP1.Stamp());
  EXPECT_EQ(transaction2->stamp(), SP2.Stamp());

  // Create the graph
  fuse_graphs::HashGraph graph;

  // add variables and validate uuids for each transaction
  std::vector<fuse_core::UUID> uuids;
  uuids = AddVariables(transaction1, graph);
  EXPECT_EQ(uuids.size(), 2);
  for (auto uuid : uuids) {
    bool should_be_true =
        uuid == SP1.Position().uuid() || uuid == SP1.Orientation().uuid();
    EXPECT_TRUE(should_be_true);
  }
  uuids = AddVariables(transaction2, graph);
  EXPECT_EQ(uuids.size(), 2);
  for (auto uuid : uuids) {
    bool should_be_true =
        uuid == SP2.Position().uuid() || uuid == SP2.Orientation().uuid();
    EXPECT_TRUE(should_be_true);
  }

  // add constraints and validate for each transaction
  int counter{0};
  counter += AddConstraints(transaction1, graph);
  counter += AddConstraints(transaction2, graph);
  EXPECT_EQ(counter, 1);

  // Add prior on first pose
  graph.addConstraint(data_.prior);

  // Optimize the constraints and variables.
  graph.optimize();

  auto p1 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph.getVariable(SP1.Position().uuid()));
  auto p2 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph.getVariable(SP2.Position().uuid()));

  auto o1 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph.getVariable(SP1.Orientation().uuid()));
  auto o2 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph.getVariable(SP2.Orientation().uuid()));

  for (int i = 0; i < 3; i++) {
    EXPECT_TRUE(std::abs(SP1.Position().data()[i] - p1.data()[i]) < 0.001);
    EXPECT_TRUE(std::abs(SP2.Position().data()[i] - p2.data()[i]) < 0.001);
  }
  for (int i = 0; i < 4; i++) {
    EXPECT_TRUE(std::abs(SP1.Orientation().data()[i] - o1.data()[i]) < 0.001);
    EXPECT_TRUE(std::abs(SP2.Orientation().data()[i] - o2.data()[i]) < 0.001);
  }
}

TEST(MultiScanRegistration, 3Scans) {
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2);
  ScanPose SP3(ros::Time(2), data_.T_WORLD_S3, data_.S3);
  ScanPose SP2_pert(ros::Time(1), data_.T_WORLD_S2_pert, data_.S2);
  ScanPose SP3_pert(ros::Time(2), data_.T_WORLD_S3_pert, data_.S3);

  // init scan registration
  std::unique_ptr<beam_matching::IcpMatcher> matcher =
      std::make_unique<beam_matching::IcpMatcher>(data_.matcher_params);

  MultiScanRegistration::Params scan_reg_params{.num_neighbors = 3,
                                                .outlier_threshold_t = 1,
                                                .outlier_threshold_r = 30,
                                                .min_motion_trans_m = 0,
                                                .min_motion_rot_rad = 0,
                                                .source = "TEST",
                                                .lag_duration = 100,
                                                .fix_first_scan = false};

  std::unique_ptr<MultiScanRegistration> multi_scan_registration =
      std::make_unique<MultiScanRegistration>(std::move(matcher),
                                              scan_reg_params);

  Eigen::Matrix<double, 6, 6> covariance;
  covariance.setIdentity();
  covariance = covariance * 0.1;
  multi_scan_registration->SetFixedCovariance(covariance);

  auto transaction1 =
      multi_scan_registration->RegisterNewScan(SP1).GetTransaction();
  auto transaction2 =
      multi_scan_registration->RegisterNewScan(SP2_pert).GetTransaction();
  auto transaction3 =
      multi_scan_registration->RegisterNewScan(SP3_pert).GetTransaction();

  // validate stamps
  EXPECT_EQ(transaction1->stamp(), SP1.Stamp());
  EXPECT_EQ(transaction2->stamp(), SP2.Stamp());
  EXPECT_EQ(transaction3->stamp(), SP3.Stamp());

  // Create the graph
  fuse_graphs::HashGraph graph;

  // add variables and validate uuids for each transaction
  std::vector<fuse_core::UUID> uuids;
  uuids = AddVariables(transaction1, graph);
  EXPECT_EQ(uuids.size(), 2);
  for (auto uuid : uuids) {
    bool should_be_true =
        uuid == SP1.Position().uuid() || uuid == SP1.Orientation().uuid();
    EXPECT_TRUE(should_be_true);
  }
  uuids = AddVariables(transaction2, graph);
  EXPECT_EQ(uuids.size(), 2);
  for (auto uuid : uuids) {
    bool should_be_true =
        uuid == SP2.Position().uuid() || uuid == SP2.Orientation().uuid();
    EXPECT_TRUE(should_be_true);
  }
  uuids = AddVariables(transaction3, graph);
  EXPECT_EQ(uuids.size(), 2);
  for (auto uuid : uuids) {
    bool should_be_true =
        uuid == SP3.Position().uuid() || uuid == SP3.Orientation().uuid();
    EXPECT_TRUE(should_be_true);
  }

  // add constraints and validate for each transaction
  int counter{0};
  counter += AddConstraints(transaction1, graph);
  counter += AddConstraints(transaction2, graph);
  counter += AddConstraints(transaction3, graph);
  EXPECT_EQ(counter, 3);

  // Add prior on first pose
  graph.addConstraint(data_.prior);

  // Optimize the constraints and variables.
  graph.optimize();

  auto p1 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph.getVariable(SP1.Position().uuid()));
  auto p2 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph.getVariable(SP2.Position().uuid()));
  auto p3 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph.getVariable(SP3.Position().uuid()));

  auto o1 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph.getVariable(SP1.Orientation().uuid()));
  auto o2 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph.getVariable(SP2.Orientation().uuid()));
  auto o3 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph.getVariable(SP3.Orientation().uuid()));

  EXPECT_TRUE(VectorsEqual(SP1.Position().data(), p1.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP1.Orientation().data(), o1.data(), 4));
  EXPECT_TRUE(VectorsEqual(SP2.Position().data(), p2.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP2.Orientation().data(), o2.data(), 4));
  EXPECT_TRUE(VectorsEqual(SP3.Position().data(), p3.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP3.Orientation().data(), o3.data(), 4));
}

TEST(MultiScanRegistration, TransactionsAndUpdates) {
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2);
  ScanPose SP3(ros::Time(2), data_.T_WORLD_S3, data_.S3);
  ScanPose SP2_pert(ros::Time(1), data_.T_WORLD_S2_pert, data_.S2);
  ScanPose SP3_pert(ros::Time(2), data_.T_WORLD_S3_pert, data_.S3);

  // init scan registration
  std::unique_ptr<beam_matching::IcpMatcher> matcher =
      std::make_unique<beam_matching::IcpMatcher>(data_.matcher_params);

  MultiScanRegistration::Params scan_reg_params{
      .num_neighbors = 3,
      .outlier_threshold_t = 1,
      .outlier_threshold_r = 30,
      .min_motion_trans_m = 0,
      .min_motion_rot_rad = 0,
      .source = "TEST",
      .lag_duration = 0,  // should still work with 0
      .fix_first_scan = false};

  std::unique_ptr<MultiScanRegistration> multi_scan_registration =
      std::make_unique<MultiScanRegistration>(std::move(matcher),
                                              scan_reg_params);

  Eigen::Matrix<double, 6, 6> covariance;
  covariance.setIdentity();
  covariance = covariance * 0.1;
  multi_scan_registration->SetFixedCovariance(covariance);

  // Create the graph
  fuse_core::Graph::SharedPtr graph = fuse_graphs::HashGraph::make_shared();

  // add transactions
  auto transaction1 =
      multi_scan_registration->RegisterNewScan(SP1).GetTransaction();
  graph->update(*transaction1);
  graph->addConstraint(data_.prior);
  graph->optimize();
  EXPECT_TRUE(graph->variableExists(SP1.Position().uuid()));
  EXPECT_TRUE(!graph->variableExists(SP2_pert.Position().uuid()));
  EXPECT_TRUE(!graph->variableExists(SP3_pert.Position().uuid()));
  auto transaction2 =
      multi_scan_registration->RegisterNewScan(SP2_pert).GetTransaction();
  graph->update(*transaction2);
  graph->optimize();
  EXPECT_TRUE(graph->variableExists(SP1.Position().uuid()));
  EXPECT_TRUE(graph->variableExists(SP2_pert.Position().uuid()));
  EXPECT_TRUE(!graph->variableExists(SP3_pert.Position().uuid()));
  auto transaction3 =
      multi_scan_registration->RegisterNewScan(SP3_pert).GetTransaction();
  graph->update(*transaction3);
  graph->optimize();
  EXPECT_TRUE(graph->variableExists(SP1.Position().uuid()));
  EXPECT_TRUE(graph->variableExists(SP2_pert.Position().uuid()));
  EXPECT_TRUE(graph->variableExists(SP3_pert.Position().uuid()));

  // Check results
  auto p1 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph->getVariable(SP1.Position().uuid()));
  auto p2 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph->getVariable(SP2.Position().uuid()));
  auto p3 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph->getVariable(SP3.Position().uuid()));

  auto o1 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph->getVariable(SP1.Orientation().uuid()));
  auto o2 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph->getVariable(SP2.Orientation().uuid()));
  auto o3 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph->getVariable(SP3.Orientation().uuid()));

  EXPECT_TRUE(VectorsEqual(SP1.Position().data(), p1.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP1.Orientation().data(), o1.data(), 4));
  EXPECT_TRUE(VectorsEqual(SP2.Position().data(), p2.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP2.Orientation().data(), o2.data(), 4));
  EXPECT_TRUE(VectorsEqual(SP3.Position().data(), p3.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP3.Orientation().data(), o3.data(), 4));

  // test updating scan poses from graph msg

  auto SP1_ = multi_scan_registration->GetScan(SP1.Stamp());
  auto SP2_ = multi_scan_registration->GetScan(SP2.Stamp());
  auto SP3_ = multi_scan_registration->GetScan(SP3.Stamp());
  EXPECT_TRUE(SP1_.Updates() == 0);
  EXPECT_TRUE(!VectorsEqual(SP2.Position().data(), SP2_.Position().data(), 3));
  EXPECT_TRUE(
      !VectorsEqual(SP2.Orientation().data(), SP2_.Orientation().data(), 4));
  EXPECT_TRUE(SP2_.Updates() == 0);
  EXPECT_TRUE(!VectorsEqual(SP3.Position().data(), SP3_.Position().data(), 3));
  EXPECT_TRUE(
      !VectorsEqual(SP3.Orientation().data(), SP3_.Orientation().data(), 4));
  EXPECT_TRUE(SP3_.Updates() == 0);
  multi_scan_registration->UpdateScanPoses(graph);
  auto SP1__ = multi_scan_registration->GetScan(SP1.Stamp());
  auto SP2__ = multi_scan_registration->GetScan(SP2.Stamp());
  auto SP3__ = multi_scan_registration->GetScan(SP3.Stamp());
  EXPECT_TRUE(VectorsEqual(SP1.Position().data(), SP1__.Position().data(), 3));
  EXPECT_TRUE(
      VectorsEqual(SP1.Orientation().data(), SP1__.Orientation().data(), 4));
  EXPECT_TRUE(SP1__.Updates() == 1);
  EXPECT_TRUE(VectorsEqual(SP2.Position().data(), SP2__.Position().data(), 3));
  EXPECT_TRUE(
      VectorsEqual(SP2.Orientation().data(), SP2__.Orientation().data(), 4));
  EXPECT_TRUE(SP2__.Updates() == 1);
  EXPECT_TRUE(VectorsEqual(SP3.Position().data(), SP3__.Position().data(), 3));
  EXPECT_TRUE(
      VectorsEqual(SP3.Orientation().data(), SP3__.Orientation().data(), 4));
  EXPECT_TRUE(SP3__.Updates() == 1);
}

TEST(MultiScanRegistration, NumNeighbours) {
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2);
  ScanPose SP3(ros::Time(2), data_.T_WORLD_S3, data_.S3);
  ScanPose SP2_pert(ros::Time(1), data_.T_WORLD_S2_pert, data_.S2);
  ScanPose SP3_pert(ros::Time(2), data_.T_WORLD_S3_pert, data_.S3);

  // init scan registration
  std::unique_ptr<beam_matching::IcpMatcher> matcher1 =
      std::make_unique<beam_matching::IcpMatcher>(data_.matcher_params);
  std::unique_ptr<beam_matching::IcpMatcher> matcher2 =
      std::make_unique<beam_matching::IcpMatcher>(data_.matcher_params);

  MultiScanRegistration::Params scan_reg_params1{.num_neighbors = 1,
                                                 .outlier_threshold_t = 1,
                                                 .outlier_threshold_r = 30,
                                                 .min_motion_trans_m = 0,
                                                 .min_motion_rot_rad = 0,
                                                 .source = "TEST",
                                                 .lag_duration = 100,
                                                 .fix_first_scan = false};

  std::unique_ptr<MultiScanRegistration> multi_scan_registration1 =
      std::make_unique<MultiScanRegistration>(std::move(matcher1),
                                              scan_reg_params1);

  MultiScanRegistration::Params scan_reg_params2{.num_neighbors = 2,
                                                 .outlier_threshold_t = 1,
                                                 .outlier_threshold_r = 30,
                                                 .min_motion_trans_m = 0,
                                                 .min_motion_rot_rad = 0,
                                                 .source = "TEST",
                                                 .lag_duration = 100,
                                                 .fix_first_scan = true};

  std::unique_ptr<MultiScanRegistration> multi_scan_registration2 =
      std::make_unique<MultiScanRegistration>(std::move(matcher2),
                                              scan_reg_params2);

  Eigen::Matrix<double, 6, 6> covariance;
  covariance.setIdentity();
  covariance = covariance * 0.1;
  multi_scan_registration1->SetFixedCovariance(covariance);
  multi_scan_registration2->SetFixedCovariance(covariance);

  // get transactions for each new scan
  auto transaction11 =
      multi_scan_registration1->RegisterNewScan(SP1).GetTransaction();
  auto transaction12 =
      multi_scan_registration1->RegisterNewScan(SP2_pert).GetTransaction();
  auto transaction13 =
      multi_scan_registration1->RegisterNewScan(SP3_pert).GetTransaction();

  auto transaction21 =
      multi_scan_registration2->RegisterNewScan(SP1).GetTransaction();
  auto transaction22 =
      multi_scan_registration2->RegisterNewScan(SP2_pert).GetTransaction();
  auto transaction23 =
      multi_scan_registration2->RegisterNewScan(SP3_pert).GetTransaction();

  // Create the graph and add transactions
  fuse_core::Graph::SharedPtr graph1 = fuse_graphs::HashGraph::make_shared();
  graph1->update(*transaction11);
  graph1->update(*transaction12);
  graph1->update(*transaction13);

  fuse_core::Graph::SharedPtr graph2 = fuse_graphs::HashGraph::make_shared();
  graph2->update(*transaction21);
  graph2->update(*transaction22);
  graph2->update(*transaction23);

  // check correct number of constraints are added
  auto constraints_added1 = graph1->getConstraints();
  int counter1 = 0;
  for (auto iter = constraints_added1.begin(); iter != constraints_added1.end();
       iter++) {
    counter1++;
  }
  EXPECT_TRUE(counter1 == 2);

  auto constraints_added2 = graph2->getConstraints();
  int counter2 = 0;
  for (auto iter = constraints_added2.begin(); iter != constraints_added2.end();
       iter++) {
    counter2++;
  }
  EXPECT_TRUE(counter1 == 2);

  // Add prior on first pose for just the first case
  graph1->addConstraint(data_.prior);

  // Optimize the constraints and variables.
  graph1->optimize();
  graph2->optimize();

  auto p11 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph1->getVariable(SP1.Position().uuid()));
  auto p12 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph1->getVariable(SP2.Position().uuid()));
  auto p13 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph1->getVariable(SP3.Position().uuid()));

  auto o11 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph1->getVariable(SP1.Orientation().uuid()));
  auto o12 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph1->getVariable(SP2.Orientation().uuid()));
  auto o13 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph1->getVariable(SP3.Orientation().uuid()));

  EXPECT_TRUE(VectorsEqual(SP1.Position().data(), p11.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP1.Orientation().data(), o11.data(), 4));
  EXPECT_TRUE(VectorsEqual(SP2.Position().data(), p12.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP2.Orientation().data(), o12.data(), 4));
  EXPECT_TRUE(VectorsEqual(SP3.Position().data(), p13.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP3.Orientation().data(), o13.data(), 4));

  auto p21 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph2->getVariable(SP1.Position().uuid()));
  auto p22 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph2->getVariable(SP2.Position().uuid()));
  auto p23 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph2->getVariable(SP3.Position().uuid()));

  auto o21 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph2->getVariable(SP1.Orientation().uuid()));
  auto o22 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph2->getVariable(SP2.Orientation().uuid()));
  auto o23 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph2->getVariable(SP3.Orientation().uuid()));

  EXPECT_TRUE(VectorsEqual(SP1.Position().data(), p21.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP1.Orientation().data(), o21.data(), 4));
  EXPECT_TRUE(VectorsEqual(SP2.Position().data(), p22.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP2.Orientation().data(), o22.data(), 4));
  EXPECT_TRUE(VectorsEqual(SP3.Position().data(), p23.data(), 3));
  EXPECT_TRUE(VectorsEqual(SP3.Orientation().data(), o23.data(), 4));
}

TEST(MultiScanRegistration, RegistrationCases) {
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2);
  ScanPose SP3(ros::Time(2), data_.T_WORLD_S3, data_.S3);

  // init scan registration
  std::unique_ptr<beam_matching::IcpMatcher> matcher =
      std::make_unique<beam_matching::IcpMatcher>(data_.matcher_params);

  MultiScanRegistration::Params scan_reg_params{.num_neighbors = 5,
                                                .outlier_threshold_t = 1,
                                                .outlier_threshold_r = 30,
                                                .min_motion_trans_m = 0,
                                                .min_motion_rot_rad = 0,
                                                .source = "TEST",
                                                .lag_duration = 100,
                                                .fix_first_scan = true};
  std::unique_ptr<MultiScanRegistration> multi_scan_registration =
      std::make_unique<MultiScanRegistration>(std::move(matcher),
                                              scan_reg_params);

  Eigen::Matrix<double, 6, 6> covariance;
  covariance.setIdentity();
  covariance = covariance * 0.1;
  multi_scan_registration->SetFixedCovariance(covariance);

  // get transactions for each new scan
  auto transaction1 =
      multi_scan_registration->RegisterNewScan(SP1).GetTransaction();
  auto transaction2 =
      multi_scan_registration->RegisterNewScan(SP2).GetTransaction();
  auto transaction3 =
      multi_scan_registration->RegisterNewScan(SP3).GetTransaction();

  // Create the graph and add transactions
  fuse_core::Graph::SharedPtr graph = fuse_graphs::HashGraph::make_shared();
  graph->update(*transaction1);
  graph->update(*transaction2);
  graph->update(*transaction3);

  // create two bad ScanPose objects and make sure that no transactions are
  // added
  Eigen::VectorXd perturb(6);
  perturb << -45, 30, 90, 10, -10, 8;
  Eigen::Matrix4d T_WORLD_S4_pert =
      beam::PerturbTransformDegM(data_.T_WORLD_S3, perturb);
  ScanPose SP4_BADINIT(ros::Time(3), T_WORLD_S4_pert, data_.S3);
  ScanPose SP4_EMPTY(ros::Time(3), T_WORLD_S4_pert, PointCloud());
  auto transactionNULL1 =
      multi_scan_registration->RegisterNewScan(SP4_BADINIT).GetTransaction();
  auto transactionNULL2 =
      multi_scan_registration->RegisterNewScan(SP4_EMPTY).GetTransaction();
  EXPECT_TRUE(transactionNULL1 == nullptr);
  EXPECT_TRUE(transactionNULL2 == nullptr);
}

TEST(MultiScanLoamRegistration, 2Scans) {
  data_ = Data();

  // init scan registration
  std::shared_ptr<LoamParams> matcher_params = std::make_shared<LoamParams>();
  std::unique_ptr<Matcher<LoamPointCloudPtr>> matcher;
  matcher = std::make_unique<LoamMatcher>(*matcher_params);
  std::shared_ptr<LoamFeatureExtractor> feature_extractor =
      std::make_shared<LoamFeatureExtractor>(matcher_params);
  
  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1, feature_extractor);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2, feature_extractor);
  ScanPose SP2_pert(ros::Time(1), data_.T_WORLD_S2_pert, data_.S2, feature_extractor);

  MultiScanRegistration::Params scan_reg_params{
      .num_neighbors = 1,
      .outlier_threshold_t = 1,
      .outlier_threshold_r = 30,
      .min_motion_trans_m = 0,
      .min_motion_rot_rad = 0,
      .source = "TEST",
      .lag_duration = 0,  // should still work with 0
      .fix_first_scan = false};

  std::unique_ptr<MultiScanLoamRegistration> multi_scan_registration =
      std::make_unique<MultiScanLoamRegistration>(std::move(matcher),
                                              scan_reg_params);

  Eigen::Matrix<double, 6, 6> covariance;
  covariance.setIdentity();
  covariance = covariance * 0.1;
  multi_scan_registration->SetFixedCovariance(covariance);

  auto transaction1 =
      multi_scan_registration->RegisterNewScan(SP1).GetTransaction();
  auto transaction2 =
      multi_scan_registration->RegisterNewScan(SP2_pert).GetTransaction();

  // Create the graph
  fuse_graphs::HashGraph graph;

  // add transactions
  graph.update(*transaction1);
  graph.update(*transaction2);

  // Add prior on first pose
  graph.addConstraint(data_.prior);

  // Optimize the constraints and variables.
  graph.optimize();

  auto p1 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph.getVariable(SP1.Position().uuid()));
  auto p2 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph.getVariable(SP2.Position().uuid()));

  auto o1 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph.getVariable(SP1.Orientation().uuid()));
  auto o2 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph.getVariable(SP2.Orientation().uuid()));

  Eigen::Matrix4d T_WORLD_S1_mea = beam_common::FusePoseToEigenTransform(p1, o1);
  Eigen::Matrix4d T_WORLD_S2_mea = beam_common::FusePoseToEigenTransform(p2, o2);
  
  EXPECT_TRUE(beam::ArePosesEqual(T_WORLD_S1_mea, data_.T_WORLD_S1, 1, 0.03));
  EXPECT_TRUE(beam::ArePosesEqual(T_WORLD_S2_mea, data_.T_WORLD_S2, 1, 0.03));
}

TEST(MultiScanLoamRegistration, 3Scans) {
  // init scan registration
  std::shared_ptr<LoamParams> matcher_params = std::make_shared<LoamParams>();
  std::unique_ptr<Matcher<LoamPointCloudPtr>> matcher;
  matcher = std::make_unique<LoamMatcher>(*matcher_params);
  std::shared_ptr<LoamFeatureExtractor> feature_extractor =
      std::make_shared<LoamFeatureExtractor>(matcher_params);

  // create scan poses
  ScanPose SP1(ros::Time(0), data_.T_WORLD_S1, data_.S1, feature_extractor);
  ScanPose SP2(ros::Time(1), data_.T_WORLD_S2, data_.S2, feature_extractor);
  ScanPose SP3(ros::Time(2), data_.T_WORLD_S3, data_.S3, feature_extractor);
  ScanPose SP2_pert(ros::Time(1), data_.T_WORLD_S2_pert, data_.S2,
                    feature_extractor);
  ScanPose SP3_pert(ros::Time(2), data_.T_WORLD_S3_pert, data_.S3,
                    feature_extractor);
  //   SP1.Save("/home/nick/tmp/loam_scan_registration/sp1/");
  //   SP2.Save("/home/nick/tmp/loam_scan_registration/sp2/");
  //   SP3.Save("/home/nick/tmp/loam_scan_registration/sp3/");

  // init scan registration
  MultiScanRegistration::Params scan_reg_params{
      .num_neighbors = 3,
      .outlier_threshold_t = 1,
      .outlier_threshold_r = 30,
      .min_motion_trans_m = 0,
      .min_motion_rot_rad = 0,
      .source = "TEST",
      .lag_duration = 0,  // should still work with 0
      .fix_first_scan = false};

  std::unique_ptr<MultiScanLoamRegistration> multi_scan_registration =
      std::make_unique<MultiScanLoamRegistration>(std::move(matcher),
                                                  scan_reg_params);

  Eigen::Matrix<double, 6, 6> covariance;
  covariance.setIdentity();
  covariance = covariance * 0.1;
  multi_scan_registration->SetFixedCovariance(covariance);

  // Create the graph
  fuse_core::Graph::SharedPtr graph = fuse_graphs::HashGraph::make_shared();

  // add transactions
  auto transaction1 =
      multi_scan_registration->RegisterNewScan(SP1).GetTransaction();
  auto transaction2 =
      multi_scan_registration->RegisterNewScan(SP2_pert).GetTransaction();
  auto transaction3 =
      multi_scan_registration->RegisterNewScan(SP3_pert).GetTransaction();
  graph->update(*transaction1);
  graph->update(*transaction2);
  graph->update(*transaction3);
  graph->addConstraint(data_.prior);
  graph->optimize();

  // Check results
  auto p1 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph->getVariable(SP1.Position().uuid()));
  auto p2 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph->getVariable(SP2.Position().uuid()));
  auto p3 = dynamic_cast<const fuse_variables::Position3DStamped&>(
      graph->getVariable(SP3.Position().uuid()));

  auto o1 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph->getVariable(SP1.Orientation().uuid()));
  auto o2 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph->getVariable(SP2.Orientation().uuid()));
  auto o3 = dynamic_cast<const fuse_variables::Orientation3DStamped&>(
      graph->getVariable(SP3.Orientation().uuid()));

  Eigen::Matrix4d T_WORLD_S1_mea = beam_common::FusePoseToEigenTransform(p1, o1);
  Eigen::Matrix4d T_WORLD_S2_mea = beam_common::FusePoseToEigenTransform(p2, o2);
  Eigen::Matrix4d T_WORLD_S3_mea = beam_common::FusePoseToEigenTransform(p3, o3);
  
  EXPECT_TRUE(beam::ArePosesEqual(T_WORLD_S1_mea, data_.T_WORLD_S1, 1, 0.03));
  EXPECT_TRUE(beam::ArePosesEqual(T_WORLD_S2_mea, data_.T_WORLD_S2, 1, 0.03));
  EXPECT_TRUE(beam::ArePosesEqual(T_WORLD_S3_mea, data_.T_WORLD_S3, 1, 0.03));
}