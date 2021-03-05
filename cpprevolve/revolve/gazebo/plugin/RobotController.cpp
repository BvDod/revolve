/*
* Copyright (C) 2017 Vrije Universiteit Amsterdam
*
* Licensed under the Apache License, Version 2.0 (the "License");
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* Author: Elte Hupkes
* Date: May 3, 2015
*
*/

#include <memory>
#include <stdexcept>
#include <cstdio>
#include <gazebo/sensors/sensors.hh>

#include <revolve/gazebo/motors/MotorFactory.h>
#include <revolve/gazebo/sensors/SensorFactory.h>
#include <revolve/gazebo/brains/Brains.h>
#include <revolve/gazebo/brains/GazeboReporter.h>
#include <revolve/brains/learner/NoLearner.h>
#include <revolve/brains/learner/BayesianOptimizer.h>
#include <revolve/brains/learner/HyperNEAT.h>
#include <multineat/Genome.h>
#include <multineat/Population.h>
#include <revolve/brains/controller/IMC/IMC.h>
#include <torch/torch.h>

#include "RobotController.h"

namespace gz = gazebo;

using namespace revolve::gazebo;

/////////////////////////////////////////////////
/// Default actuation time is given and this will be overwritten by the plugin
/// config in Load.
RobotController::RobotController()
    : actuationTime_(0)
{
}

/////////////////////////////////////////////////
RobotController::~RobotController()
{
  this->node_.reset();
  this->world_.reset();
  this->motorFactory_.reset();
  this->sensorFactory_.reset();
}

/////////////////////////////////////////////////
void RobotController::Load(
    ::gazebo::physics::ModelPtr _parent,
    sdf::ElementPtr _sdf)
{
    try {
        // Store the pointer to the model / world
        this->model_ = _parent;
        this->world_ = _parent->GetWorld();
        this->initTime_ = this->world_->SimTime().Double();

        // Create transport node
        this->node_.reset(new gz::transport::Node());
        this->node_->Init();

        // Subscribe to robot battery state updater
        this->batterySetSub_ = this->node_->Subscribe(
                "~/battery_level/request",
                &RobotController::UpdateBattery,
                this);
        this->batterySetPub_ = this->node_->Advertise<gz::msgs::Response>(
                "~/battery_level/response");

        if (not _sdf->HasElement("rv:robot_config")) {
            std::cerr
                    << "No `rv:robot_config` element found, controller not initialized."
                    << std::endl;
            return;
        }

        auto robotConfiguration = _sdf->GetElement("rv:robot_config");

        if (robotConfiguration->HasElement("rv:update_rate")) {
            auto updateRate = robotConfiguration->GetElement("rv:update_rate")->Get<double>();
            this->actuationTime_ = 1.0 / updateRate;
        }

        // Load motors
        this->motorFactory_ = this->MotorFactory(_parent);
        this->LoadActuators(robotConfiguration);

        // Load sensors
        this->sensorFactory_ = this->SensorFactory(_parent);
        this->LoadSensors(robotConfiguration);

        // Load brain, this needs to be done after the motors and sensors so they
        // can potentially be reordered.
        this->LoadBrain(robotConfiguration);

        // Call the battery loader
        this->LoadBattery(robotConfiguration);

        // Call startup function which decides on actuation
        this->Startup(_parent, _sdf);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error Loading the Robot Controller, expcetion: " << std::endl
                  << e.what() << std::endl;
        throw;
    }
}

/////////////////////////////////////////////////
void RobotController::UpdateBattery(ConstRequestPtr &_request)
{
  if (_request->data() not_eq this->model_->GetName() and
      _request->data() not_eq this->model_->GetScopedName())
  {
    return;
  }

  gz::msgs::Response resp;
  resp.set_id(_request->id());
  resp.set_request(_request->request());

  if (_request->request() == "set_battery_level")
  {
    resp.set_response("success");
    this->SetBatteryLevel(_request->dbl_data());
  }
  else
  {
    std::stringstream ss;
    ss << this->BatteryLevel();
    resp.set_response(ss.str());
  }

  batterySetPub_->Publish(resp);
}

/////////////////////////////////////////////////
void RobotController::LoadActuators(const sdf::ElementPtr _sdf)
{
  if (not _sdf->HasElement("rv:brain")
      or not _sdf->GetElement("rv:brain")->HasElement("rv:actuators"))
  {
    return;
  }
  auto actuators = _sdf->GetElement("rv:brain")->GetElement("rv:actuators");

  // Load actuators of type servomotor
  if (actuators->HasElement("rv:servomotor"))
  {
    auto servomotor = actuators->GetElement("rv:servomotor");
    while (servomotor)
    {
      auto servomotorObj = this->motorFactory_->Create(servomotor);
      motors_.push_back(servomotorObj);
      servomotor = servomotor->GetNextElement("rv:servomotor");
    }
  }
}

/////////////////////////////////////////////////
void RobotController::LoadSensors(const sdf::ElementPtr _sdf)
{
  if (not _sdf->HasElement("rv:brain")
      or not _sdf->GetElement("rv:brain")->HasElement("rv:sensors"))
  {
    return;
  }
  auto sensors = _sdf->GetElement("rv:brain")->GetElement("rv:sensors");

  // Load sensors
  auto sensor = sensors->GetElement("rv:sensor");
  while (sensor)
  {
    auto sensorObj = this->sensorFactory_->Create(sensor);
    sensors_.push_back(sensorObj);
    sensor = sensor->GetNextElement("rv:sensor");
  }
}

/////////////////////////////////////////////////
MotorFactoryPtr RobotController::MotorFactory(
    ::gazebo::physics::ModelPtr _model)
{
  return MotorFactoryPtr(new class MotorFactory(_model));
}

/////////////////////////////////////////////////
SensorFactoryPtr RobotController::SensorFactory(
    ::gazebo::physics::ModelPtr _model)
{
  return SensorFactoryPtr(new class SensorFactory(_model));
}

/////////////////////////////////////////////////
void RobotController::LoadBrain(const sdf::ElementPtr _sdf)
{
    if (not _sdf->HasElement("rv:brain")) {
        std::cerr << "No robot brain detected, this is probably an error."
                  << std::endl;
        return;
    }

    auto brain_sdf = _sdf->GetElement("rv:brain");
    auto controller_type = brain_sdf->GetElement("rv:controller")->GetAttribute("type")->GetAsString();
//    auto IMC_params = brain_sdf->GetElement("rv:IMC")->GetElement("rv:params");
    auto learner_type = brain_sdf->GetElement("rv:learner")->GetAttribute("type")->GetAsString();
    std::cout << "Loading controller " << controller_type << " and learner " << learner_type << std::endl;


    //TODO parameters from SDF
    const double evaluation_rate = 60.0;
    const unsigned int n_learning_evaluations = 300;

    this->evaluator = std::make_unique<::revolve::gazebo::Evaluator>(evaluation_rate, true, this->model_);

    // aggregated reporter
    std::unique_ptr<AggregatedReporter> aggregated_reporter(new AggregatedReporter(this->model_->GetName()));

    aggregated_reporter->create<::revolve::PrintReporter>();
    // gazebo network publisher reporter
    this->gazebo_reporter.reset(new GazeboReporter(aggregated_reporter->robot_id, this->node_));
    aggregated_reporter->append(this->gazebo_reporter);

    this->reporter = std::move(aggregated_reporter);

    // SELECT CONTROLLER ------------------------------------------------------
    std::unique_ptr<::revolve::Controller> controller;

    if ("ann" == controller_type) {
        controller = std::make_unique<NeuralNetwork>(this->model_, brain_sdf, motors_, sensors_);
    } else if ("spline" == controller_type) {
        if (not motors_.empty()) {
            controller = std::make_unique<RLPower>(this->model_, brain_sdf, motors_, sensors_);
        }
    } else if ("cpg" == controller_type) {
        controller = std::make_unique<DifferentialCPG>(brain_sdf, motors_);
    } else if ("cppn-cpg" == controller_type) {
        controller = std::make_unique<DifferentialCPPNCPG>(brain_sdf, motors_);
    } else {
        throw std::runtime_error("Robot brain: Controller \"" + controller_type + "\" is not supported.");
    }

    sdf::ElementPtr IMC_sdf = brain_sdf->GetElement("rv:IMC");
    if( IMC_sdf->GetAttribute("active")->GetAsString() == "1"){
        std::cout << "Initializing IMC" << std::endl;
        // ================= INITIALIZE IMC ====================
        IMC::IMCParams imc_params = IMC::IMCParams();
        imc_params.restore_checkpoint = (IMC_sdf->GetAttribute("restore_checkpoint")->GetAsString() == "1");
        imc_params.save_checkpoint = (IMC_sdf->GetAttribute("save_checkpoint")->GetAsString() == "1");
        imc_params.learning_rate = stod(IMC_sdf->GetAttribute("learning_rate")->GetAsString());
        imc_params.beta1 = stod(IMC_sdf->GetAttribute("beta1")->GetAsString());
        imc_params.beta2 = stod(IMC_sdf->GetAttribute("beta2")->GetAsString());
        imc_params.weight_decay = stod(IMC_sdf->GetAttribute("weight_decay")->GetAsString());
        imc_params.model_name = this->model_->GetName();

        std::cout<<"IMC Parameters: lr:"<< imc_params.learning_rate<<", beta1:"  << imc_params.beta1<<", beta2:"  << imc_params.beta2<<", wd:" << imc_params.weight_decay << std::endl;
        controller = std::make_unique<IMC>(std::move(controller), motors_, imc_params);
        std::cout<<"IMC has been Loaded"<<std::endl;
    }

    // SELECT LEARNER ---------------------------------------------------------
    if ("offline" == learner_type) {
        learner = std::make_unique<NoLearner<Controller>>(std::move(controller));
    } else if ("rlpower" == learner_type) {
        //TODO make RLPower generic
        if ("spline" != controller_type) {
            throw std::runtime_error("Robot brain: Learner RLPower not supported for \"" + controller_type + "\" controller.");
        }
        learner = std::make_unique<NoLearner<Controller>>(std::move(controller));
    } else if ("bo" == learner_type) {
        learner = std::make_unique<BayesianOptimizer>(
                std::move(controller),
                this->evaluator.get(),
                this->reporter.get(),
                evaluation_rate,
                n_learning_evaluations,
                this->model_->GetName());
    } else if ("nipes" == learner_type) {
        NIPES::NIPES_Parameters params = NIPES::NIPES_Parameters();

        EA::Parameters EA_params = EA::Parameters();
        params.EA_params = EA_params;
        params.EA_params.verbose = (brain_sdf->GetElement("rv:learner")->GetAttribute("verbose")->GetAsString() == "1");
        params.EA_params.population_size = stoi(brain_sdf->GetElement("rv:learner")->GetAttribute("population_size")->GetAsString());
        params.EA_params.max_eval = std::min(int(n_learning_evaluations), stoi(brain_sdf->GetElement("rv:learner")->GetAttribute("max_eval")->GetAsString()));

        auto dist = std::bind(std::uniform_int_distribution<int>(),
                              std::mt19937(std::random_device{}()));

        learner = std::make_unique<NIPES>(
                std::move(controller),
                this->evaluator.get(),
                this->reporter.get(),
                params,
                dist(),
                evaluation_rate,
                params.EA_params.max_eval,
                this->model_->GetName());
    } else if ("de"==learner_type) {
        DifferentialEvo::DE_Parameters params = DifferentialEvo::DE_Parameters();
        params.type = brain_sdf->GetElement("rv:learner")->GetAttribute("subtype")->GetAsString();
        params.CR = stod(brain_sdf->GetElement("rv:learner")->GetAttribute("CR")->GetAsString());
        params.F =  stod(brain_sdf->GetElement("rv:learner")->GetAttribute("F")->GetAsString());
        params.n_parents =  stoi(brain_sdf->GetElement("rv:learner")->GetAttribute("n_parents")->GetAsString());;
        if (params.type == "dex3"){
            params.n_parents = 7;
        }

        EA::Parameters EA_params = EA::Parameters();
        params.EA_params = EA_params;
        params.EA_params.verbose = (brain_sdf->GetElement("rv:learner")->GetAttribute("verbose")->GetAsString() == "1");
        params.EA_params.population_size = stoi(brain_sdf->GetElement("rv:learner")->GetAttribute("population_size")->GetAsString());
        params.EA_params.max_eval = std::min(int(n_learning_evaluations), stoi(brain_sdf->GetElement("rv:learner")->GetAttribute("max_eval")->GetAsString()));

        auto dist = std::bind(std::uniform_int_distribution<int>(),
                              std::mt19937(std::random_device{}()));

        learner = std::make_unique<DifferentialEvo>(
                std::move(controller),
                this->evaluator.get(),
                this->reporter.get(),
                params,
                dist(),
                evaluation_rate,
                n_learning_evaluations,
                this->model_->GetName());
//    } else if ("hyperneat" == learner_type) {
//        NEAT::Parameters neat_params = NEAT::Parameters();
//
//        const sdf::ElementPtr learner_sdf = brain_sdf->GetElement("rv:learner")->GetElement("rv:params");
//
//#define WRITE_DOUBLE_PARAM(x)   std::cout << #x << " is set to: " << learner_sdf->GetAttribute(#x)->GetAsString() << std::endl; neat_params.x = stod(learner_sdf->GetAttribute(#x)->GetAsString());
//#define CHECK_PARAM(x)   {stod(std::to_string(neat_params.x))==stod(learner_sdf->GetAttribute(#x)->GetAsString()) ? std::cout << std::left <<#x << " is set to: Default" << std::endl : WRITE_DOUBLE_PARAM(x)}
//        CHECK_PARAM(PopulationSize)
//        CHECK_PARAM(WeightDiffCoeff)
//        CHECK_PARAM(CompatTreshold)
//        CHECK_PARAM(YoungAgeTreshold)
//        CHECK_PARAM(OldAgeTreshold)
//        CHECK_PARAM(MinSpecies)
//        CHECK_PARAM(MaxSpecies)
//        CHECK_PARAM(RouletteWheelSelection)
//        CHECK_PARAM(RecurrentProb)
//        CHECK_PARAM(OverallMutationRate)
//        CHECK_PARAM(ArchiveEnforcement)
//        CHECK_PARAM(MutateWeightsProb)
//        CHECK_PARAM(WeightMutationMaxPower)
//        CHECK_PARAM(WeightReplacementMaxPower)
//        CHECK_PARAM(MutateWeightsSevereProb)
//        CHECK_PARAM(WeightMutationRate)
//        CHECK_PARAM(WeightReplacementRate)
//        CHECK_PARAM(MaxWeight)
//        CHECK_PARAM(MutateAddNeuronProb)
//        CHECK_PARAM(MutateAddLinkProb)
//        CHECK_PARAM(MutateRemLinkProb)
//        CHECK_PARAM(MinActivationA)
//        CHECK_PARAM(MaxActivationA)
//        CHECK_PARAM(ActivationFunction_SignedSigmoid_Prob)
//        CHECK_PARAM(ActivationFunction_UnsignedSigmoid_Prob)
//        CHECK_PARAM(ActivationFunction_Tanh_Prob)
//        CHECK_PARAM(ActivationFunction_SignedStep_Prob)
//        CHECK_PARAM(CrossoverRate)
//        CHECK_PARAM(MultipointCrossoverRate)
//        CHECK_PARAM(SurvivalRate)
//        CHECK_PARAM(MutateNeuronTraitsProb)
//        CHECK_PARAM(MutateLinkTraitsProb)
//#undef CHECK_PARAM
//#undef WRITE_DOUBLE_PARAM
//
//        neat_params.DynamicCompatibility = (learner_sdf->GetAttribute("DynamicCompatibility")->GetAsString() == "true");
//        neat_params.NormalizeGenomeSize = (learner_sdf->GetAttribute("NormalizeGenomeSize")->GetAsString() == "true");
//        neat_params.AllowLoops = (learner_sdf->GetAttribute("AllowLoops")->GetAsString() == "true");
//        neat_params.AllowClones = (learner_sdf->GetAttribute("AllowClones")->GetAsString() == "true");
//
//        int seed = 0;
//
//        learner = std::make_unique<HyperNEAT>(
//                std::move(controller),
//                this->evaluator.get(),
//                this->reporter.get(),
//                neat_params,
//                seed,
//                evaluation_rate,
//                n_learning_evaluations);
    } else {
        throw std::runtime_error("Robot brain: Learner \"" + learner_type + "\" is not supported.");
    }
}

/////////////////////////////////////////////////
/// Default startup, bind to CheckUpdate
void RobotController::Startup(
    ::gazebo::physics::ModelPtr /*_parent*/,
    sdf::ElementPtr /*_sdf*/)
{
  this->updateConnection_ = gz::event::Events::ConnectWorldUpdateBegin(
      boost::bind(&RobotController::CheckUpdate, this, _1));
}

/////////////////////////////////////////////////
void RobotController::CheckUpdate(const ::gazebo::common::UpdateInfo _info)
{
  auto diff = _info.simTime - lastActuationTime_;

  if (diff.Double() >= actuationTime_)
  {
    this->DoUpdate(_info);
    lastActuationTime_ = _info.simTime;
  }
}

/////////////////////////////////////////////////
/// Default update function simply tells the brain to perform an update
void RobotController::DoUpdate(const ::gazebo::common::UpdateInfo _info)
{
    const gz::common::Time current_time = _info.simTime - initTime_;
    const double current_time_d = current_time.Double();
    const double delta_time = (_info.simTime - lastActuationTime_).Double();

    //const ::ignition::math::Pose3d &relative_pose = model_->RelativePose();
    const ::ignition::math::Pose3d &world_pose = model_->WorldPose();

    if (evaluator) {
        evaluator->simulation_update(world_pose, current_time_d, delta_time);
    }

    if (gazebo_reporter) {
        gazebo_reporter->simulation_update(world_pose, current_time, delta_time);
    }

    if (learner) {
        learner->optimize(current_time_d, delta_time);
        revolve::Controller *controller = learner->controller();
        if (controller) {
            controller->update(motors_, sensors_, current_time_d, delta_time);
        }
    }
}

/////////////////////////////////////////////////
void RobotController::LoadBattery(const sdf::ElementPtr _sdf)
{
  if (_sdf->HasElement("rv:battery"))
  {
    this->batteryElem_ = _sdf->GetElement("rv:battery");
  }
}

/////////////////////////////////////////////////
double RobotController::BatteryLevel()
{
  if (not batteryElem_ or not batteryElem_->HasElement("rv:level"))
  {
    return 0.0;
  }

  return batteryElem_->GetElement("rv:level")->Get< double >();
}

/////////////////////////////////////////////////
void RobotController::SetBatteryLevel(double _level)
{
  if (batteryElem_ and batteryElem_->HasElement("rv:level"))
  {
    batteryElem_->GetElement("rv:level")->Set(_level);
  }
}
