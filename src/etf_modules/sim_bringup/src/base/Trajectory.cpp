#include "base/Trajectory.h"

sim_bringup::Trajectory::Trajectory(const std::string &config_file_path) : 
    Robot(config_file_path)
{
    if (Robot::getNumDOFs() == 6)
        msg.joint_names = {"joint1", "joint2", "joint3", "joint4", "joint5", "joint6"};
    else
        RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "Such number of robot DOFs is not supported!");

    std::string project_abs_path(__FILE__);
    for (size_t i = 0; i < 4; i++)
        project_abs_path = project_abs_path.substr(0, project_abs_path.find_last_of("/\\"));
    
    YAML::Node node { YAML::LoadFile(project_abs_path + config_file_path) };
    YAML::Node planner_node { node["planner"] };

    if (planner_node["max_edge_length"])
        max_edge_length = planner_node["max_edge_length"].as<float>();
    else
    {
        max_edge_length = 0.1;
        RCLCPP_WARN(rclcpp::get_logger("rclcpp"), "Maximal edge length is not defined! Using default value of %f", max_edge_length);
    }

    if (planner_node["trajectory_max_time_step"])
        trajectory_max_time_step = planner_node["trajectory_max_time_step"].as<float>();
    else
    {
        trajectory_max_time_step = 0.1;
        RCLCPP_WARN(rclcpp::get_logger("rclcpp"), "Maximal edge length is not defined! Using default value of %f", trajectory_max_time_step);
    }
}

void sim_bringup::Trajectory::addPoint(float time_instance, const Eigen::VectorXf &position)
{
    trajectory_msgs::msg::JointTrajectoryPoint point {};
    for (long int i = 0; i < position.size(); i++)
    {
        point.positions.emplace_back(position(i));
        point.velocities.emplace_back(0);
        point.accelerations.emplace_back(0);
    }

    point.time_from_start.sec = int32_t(time_instance);
    point.time_from_start.nanosec = (time_instance - point.time_from_start.sec) * 1e9;
    msg.points.emplace_back(point);
}

void sim_bringup::Trajectory::addPoint(float time_instance, const Eigen::VectorXf &position, const Eigen::VectorXf &velocity)
{
    trajectory_msgs::msg::JointTrajectoryPoint point {};
    for (long int i = 0; i < position.size(); i++)
    {
        point.positions.emplace_back(position(i));
        point.velocities.emplace_back(velocity(i));
        point.accelerations.emplace_back(0);
    }

    point.time_from_start.sec = int32_t(time_instance);
    point.time_from_start.nanosec = (time_instance - point.time_from_start.sec) * 1e9;
    msg.points.emplace_back(point);
}

void sim_bringup::Trajectory::addPoint(float time_instance, const Eigen::VectorXf &position, const Eigen::VectorXf &velocity, 
                                       const Eigen::VectorXf &acceleration)
{
    trajectory_msgs::msg::JointTrajectoryPoint point {};
    for (long int i = 0; i < position.size(); i++)
    {
        point.positions.emplace_back(position(i));
        point.velocities.emplace_back(velocity(i));
        point.accelerations.emplace_back(acceleration(i));
    }

    point.time_from_start.sec = int32_t(time_instance);
    point.time_from_start.nanosec = (time_instance - point.time_from_start.sec) * 1e9;
    msg.points.emplace_back(point);
}

/// @brief Add points from 'spline' to 'msg.points' using the time discretization step 'trajectory_max_time_step'.
/// @param spline Spline which points are used.
/// @param t_offset Time offset for which all points are time shifted.
/// @param t_final Final time from the spline which limits a final point that will be added.
void sim_bringup::Trajectory::addPoints(std::shared_ptr<planning::trajectory::Spline> spline, float t_offset, float t_final)
{
    Eigen::VectorXf q_current {};
    Eigen::VectorXf q_current_dot {};
    Eigen::VectorXf q_current_ddot {};
    float t { 0 };

    do
    {
        t += trajectory_max_time_step;
        if (t > t_final)
            t = t_final;

        q_current = spline->getPosition(t);
        q_current_dot = spline->getVelocity(t);
        q_current_ddot = spline->getAcceleration(t);
        addPoint(t_offset + t, q_current, q_current_dot, q_current_ddot);
        
        // std::cout << "Adding point at time: " << t_offset + t << " [s] \n";
        // std::cout << "Position:     " << q_current.transpose() << "\n";
        // std::cout << "Velocity:     " << q_current_dot.transpose() << "\n";
        // std::cout << "Acceleration: " << q_current_ddot.transpose() << "\n\n";
    } 
    while (t < t_final);
}

void sim_bringup::Trajectory::addPath(const std::vector<std::shared_ptr<base::State>> &path, const std::vector<float> &time_instances)
{
    for (size_t i = 0; i < path.size(); i++)
        addPoint(time_instances[i], path[i]->getCoord());
}

void sim_bringup::Trajectory::addPath(const std::vector<Eigen::VectorXf> &path, const std::vector<float> &time_instances)
{
    for (size_t i = 0; i < path.size(); i++)
        addPoint(time_instances[i], path[i]);
}

void sim_bringup::Trajectory::addPath(const std::vector<std::shared_ptr<base::State>> &path)
{
    std::vector<Eigen::VectorXf> new_path {};
    preprocessPath(path, new_path);

    std::shared_ptr<planning::trajectory::Spline> spline_current { nullptr };
    std::shared_ptr<planning::trajectory::Spline> spline_next { nullptr };
    Eigen::VectorXf q_current { new_path.front() };
    Eigen::VectorXf q_current_dot { Eigen::VectorXf::Zero(Robot::getNumDOFs()) };
    Eigen::VectorXf q_current_ddot { Eigen::VectorXf::Zero(Robot::getNumDOFs()) };

    addPoint(0, q_current);
    spline_current = std::make_shared<planning::trajectory::Spline5>(Robot::getRobot(), q_current, q_current_dot, q_current_ddot);
    if (new_path.size() == 2)
        spline_current->compute(new_path[1]);
    else
        spline_current->compute(new_path[2]);
    
    float t_current {};
    float t {}, t_min {}, t_max {};
    bool found { false };
    size_t num { 0 };
    const size_t max_num_iter { 5 };
    
    for (int i = 0; i < int(new_path.size()) - 3; i++)
    {
        // std::cout << "i: " << i << " ---------------------------\n";
        found = false;
        num = 0;
        t_min = 0;
        t_max = spline_current->getTimeFinal();

        while (true)
        {
            t = (t_min + t_max) / 2;
            // std::cout << "t: " << t << " [s] \n";
            q_current = spline_current->getPosition(t);
            q_current_dot = spline_current->getVelocity(t);
            q_current_ddot = spline_current->getAcceleration(t);

            spline_next = std::make_shared<planning::trajectory::Spline5>(Robot::getRobot(), q_current, q_current_dot, q_current_ddot);
            found = spline_next->compute(new_path[i+3]);

            if (found)
                break;
            else if (++num == max_num_iter)
                t_min = t_max;  // Solution surely exists, and 'found' will become true.
            else
                t_min = t;
        }

        addPoints(spline_current, t_current, t);        
        t_current += t;
        spline_current = spline_next;
        // std::cout << "t_current: " << t_current << " [s] \n";
    }

    addPoints(spline_current, t_current, spline_current->getTimeFinal());
}

void sim_bringup::Trajectory::preprocessPath(const std::vector<std::shared_ptr<base::State>> &path, std::vector<Eigen::VectorXf> &new_path)
{
    new_path.clear();
    new_path.emplace_back(path.front()->getCoord());
    base::State::Status status { base::State::Status::None };
    Eigen::VectorXf q_new {};
    float dist {};

    for (size_t i = 1; i < path.size(); i++)
    {
        status = base::State::Status::Advanced;
        q_new = path[i-1]->getCoord();
        while (status == base::State::Status::Advanced)
        {
            dist = (q_new - path[i]->getCoord()).norm();
            if (max_edge_length < dist)
            {
                q_new += (path[i]->getCoord() - q_new) * (max_edge_length / dist);
                status = base::State::Status::Advanced;
            }
            else
            {
                q_new = path[i]->getCoord();
                status = base::State::Status::Reached;
            }

            new_path.emplace_back(q_new);
        }
    }

    // std::cout << "Preprocessed path is: \n";
    // for (size_t i = 0; i < new_path.size(); i++)
    //     std::cout << new_path.at(i).transpose() << "\n";
    // std::cout << std::endl;
}

/// @brief Publish a trajectory stored in 'msg.points'.
/// @param time_delay Time delay in [s] after which the trajectory will be pubslihed. Default: 0.
/// @param print Whether to print a published trajectory. Default: false.
void sim_bringup::Trajectory::publish(float time_delay, bool print)
{
    if (msg.points.empty())
    {
        RCLCPP_WARN(rclcpp::get_logger("rclcpp"), "There are no trajectory points to be published!");
        return;
    }

    msg.header.stamp.sec = int32_t(time_delay);
    msg.header.stamp.nanosec = (time_delay - msg.header.stamp.sec) * 1e9;
    publisher->publish(msg);

    if (!print)
        return;

    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Trajectory points: ");
    switch (msg.points.front().positions.size())
    {
    case 6:
        for (size_t i = 0; i < msg.points.size(); i++)
        {
            RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Num. %ld.\t Time: %f [s]", 
                        i, (msg.points[i].time_from_start.sec + msg.points[i].time_from_start.nanosec * 1e-9) + time_delay);
            RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "\t Position:     (%f, %f, %f, %f, %f, %f)", 
                        msg.points[i].positions[0],
                        msg.points[i].positions[1],
                        msg.points[i].positions[2],
                        msg.points[i].positions[3],
                        msg.points[i].positions[4],
                        msg.points[i].positions[5]);
            RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "\t Velocity:     (%f, %f, %f, %f, %f, %f)", 
                        msg.points[i].velocities[0],
                        msg.points[i].velocities[1],
                        msg.points[i].velocities[2],
                        msg.points[i].velocities[3],
                        msg.points[i].velocities[4],
                        msg.points[i].velocities[5]);
            RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "\t Acceleration: (%f, %f, %f, %f, %f, %f)", 
                        msg.points[i].accelerations[0],
                        msg.points[i].accelerations[1],
                        msg.points[i].accelerations[2],
                        msg.points[i].accelerations[3],
                        msg.points[i].accelerations[4],
                        msg.points[i].accelerations[5]);
        }
        break;
    
    default:
        RCLCPP_WARN(rclcpp::get_logger("rclcpp"), "Cannot print trajectory!");
        break;
    }
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Publishing trajectory ...");
}

void sim_bringup::Trajectory::clear()
{
    msg.points.clear();
}
