#include "pick_and_place_using_cameras.h"

PickAndPlaceUsingCamerasNode::PickAndPlaceUsingCamerasNode(const std::string node_name, const int period, const std::string time_unit) 
    : PickAndPlaceNode(node_name, period, time_unit)
{
    bounding_boxes_subscription = this->create_subscription<sensor_msgs::msg::PointCloud2>
        ("/bounding_boxes", 10, std::bind(&PickAndPlaceUsingCamerasNode::boundingBoxesCallback, this, std::placeholders::_1));
}

void PickAndPlaceUsingCamerasNode::pickAndPlaceUsingCamerasCallback()
{
    // testRobotOrientation();
    
    if (state == 0)
    {
        chooseObject();
        if (obj_idx != -1)
        {
            RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Object %d is recognized at the position (%f, %f, %f).", 
                obj_idx, objects_pos[obj_idx].x(), objects_pos[obj_idx].y(), objects_pos[obj_idx].z());
            computeObjectApproachAndPickPose();
            state = 1;
        }
    }
    
    switch (state)
    {
    case 1:
        RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Going towards the object...");
        xarm_client.get_servo_angle(current_angles);
        current_angles[0] = std::atan2(objects_pos[obj_idx].y(), objects_pos[obj_idx].x());            
        xarm_client.set_servo_angle(current_angles, max_ang_vel, max_ang_acc, 0, true, 1, -1);
        xarm_client.set_position(object_approach_pose, -1, max_lin_vel, max_lin_acc, 0, true, 1);
        xarm_client.set_gripper_position(850, true, 1); 
        state++;
        break;

    // case 2:
    //     RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Preparing to pick the object...");
    //     xarm_client.get_servo_angle(current_angles);
    //     if (std::abs(current_angles[3]) > 2*M_PI - 0.1)     // If theta4 goes out of range
    //     {
    //         current_angles[3] = 0;
    //         xarm_client.set_servo_angle(current_angles, max_ang_vel, max_ang_acc, 0, true, 1, -1);
    //     }
    //     else    // Just for fun in case the previous action does not occur
    //     {
    //         current_angles[5] += M_PI;
    //         xarm_client.set_servo_angle(current_angles, 2*max_ang_vel, 2*max_ang_acc, 0, false, -1, -1);
    //         xarm_client.set_gripper_speed(3000);
    //         xarm_client.set_gripper_position(0);
    //         xarm_client.set_gripper_position(850);
    //         current_angles[5] -= M_PI;
    //         xarm_client.set_servo_angle(current_angles, 2*max_ang_vel, 2*max_ang_acc, 0, false, -1, -1);
    //     }
    //     state++;
    //     break;

    case 2:
        RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Picking the object...");
        current_pose = object_approach_pose; 
        current_pose[2] -= delta_z;
        xarm_client.set_position(current_pose, -1, 0.5*max_lin_vel, 0.5*max_lin_acc, 0, true, 1);
        xarm_client.set_position(object_pick_pose, -1, 0.5*max_lin_vel, 0.5*max_lin_acc, 0, true, 1);
        xarm_client.set_gripper_position(0, true, 1);

        current_pose[2] += 2*delta_z;
        xarm_client.set_position(current_pose, -1, max_lin_vel, max_lin_acc, 0, true, 1);
        state++;
        break;
    
    case 3:
        float gripper_pos;
        xarm_client.get_gripper_position(&gripper_pos);
        RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Gripper position: %f", gripper_pos);
        if (gripper_pos < 10)  // Nothing is caught
        {
            RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Trying to pick the object again...");
            xarm_client.set_gripper_position(850, true, 1);     
            offset_z += -5;     // Correct object pick pose z for -5 mm
            objects_pos.clear();
            objects_dim.clear();
            num_captures.clear();
            state = 0;      // New measurements can be obtained
        }
        else
        {
            RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Moving the object to destination...");
            xarm_client.get_servo_angle(current_angles);
            if (std::atan2(objects_pos[obj_idx].y(), objects_pos[obj_idx].x()) > 0)
                current_angles[0] = M_PI - 0.05;
            else
                current_angles[0] = -M_PI + 0.05;
            
            xarm_client.set_servo_angle(current_angles, max_ang_vel, max_ang_acc, 0, true, 1, -1);
            state++;
            offset_z = 0;
        }
        break;

    case 4:
        RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Moving the object to destination...");            
        xarm_client.get_position(current_pose);
        current_pose[0] = -550;
        xarm_client.set_position(current_pose, -1, 0.5*max_lin_vel, 0.5*max_lin_acc, 0, true, 1);
        xarm_client.set_gripper_position(850, true, 1);

        objects_pos.clear();
        objects_dim.clear();
        num_captures.clear();
        state = 0;      // New measurements can be obtained
        break;
    
    default:
        RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Waiting for the object...");
        xarm_client.set_gripper_position(850, true, 1);
        objects_pos.clear();
        objects_dim.clear();
        num_captures.clear();
        state = 0;      // New measurements can be obtained
        break;
    }

    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "---------------------------------------------"); 
}

void PickAndPlaceUsingCamerasNode::boundingBoxesCallback(const sensor_msgs::msg::PointCloud2::SharedPtr msg)
{
    if (state != 0)
        return;
    
    pcl::PointCloud<pcl::PointXYZ>::Ptr pcl(new pcl::PointCloud<pcl::PointXYZ>);	
    pcl::moveFromROSMsg(*msg, *pcl);
    Eigen::Vector3f new_object_pos;
    Eigen::Vector3f new_object_dim;
    bool found = false;

    for (int i = 0; i < pcl->size(); i += 2)
    {
        new_object_dim << pcl->points[i].x, pcl->points[i].y, pcl->points[i].z;
        new_object_pos << pcl->points[i+1].x, pcl->points[i+1].y, pcl->points[i+1].z;

        // Filter the destination box
        if (new_object_pos.x() < -0.4 && std::abs(new_object_pos.y()) < 0.2 && new_object_pos.z() < 0.25)
            continue;

        RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Bounding-box %d: dim = (%f, %f, %f), pos = (%f, %f, %f)",  // (x, y, z) in [m]
            i/2, new_object_dim.x(), new_object_dim.y(), new_object_dim.z(), new_object_pos.x(), new_object_pos.y(), new_object_pos.z());
    
        // Measurements are averaged online
        for (int j = 0; j < objects_dim.size(); j++)
        {
            if ((new_object_dim - objects_dim[j]).norm() < 0.02 && (new_object_pos - objects_pos[j]).norm() < 0.02)
            {
                objects_dim[j] = (num_captures[j] * objects_dim[j] + new_object_dim) / (num_captures[j] + 1);
                objects_pos[j] = (num_captures[j] * objects_pos[j] + new_object_pos) / (num_captures[j] + 1);
                num_captures[j]++;
                found = true;
                break;
            }
        }
        
        if (!found)
        {
            objects_dim.emplace_back(new_object_dim);
            objects_pos.emplace_back(new_object_pos);
            num_captures.emplace_back(1);
        }                
    }    
}

void PickAndPlaceUsingCamerasNode::testRobotOrientation()
{
    xarm_client.get_position(current_pose);
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Robot end-effector pose: (%f, %f, %f)", current_pose[0], current_pose[1], current_pose[2]);     // XYZ in [m]
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Robot end-effector RPY:  (%f, %f, %f)", current_pose[3], current_pose[4], current_pose[5]);     // RPY angles in [rad]
    
    Eigen::Matrix3f R;
    Eigen::Vector3f RPY, YPR;

    // For approaching from above
    // R.col(0) << objects_pos[obj_idx](0), objects_pos[obj_idx](1), 0;
    // R.col(0) = R.col(0) / R.col(0).norm();
    // R.col(1) << objects_pos[obj_idx](1), -objects_pos[obj_idx](0), 0;
    // R.col(1) = R.col(1) / R.col(1).norm();
    // R.col(2) << 0, 0, -1;

    // R.col(0) << 0, 0, -1;
    // R.col(1) << -current_pose[1], current_pose[0], 0;
    // R.col(1) = R.col(1) / R.col(1).norm();
    // R.col(2) << current_pose[0], current_pose[1], 0;
    // R.col(2) = R.col(2) / R.col(2).norm();

    R = Eigen::AngleAxisf(current_pose[5], Eigen::Vector3f::UnitZ()) 
        * Eigen::AngleAxisf(current_pose[4], Eigen::Vector3f::UnitY())
        * Eigen::AngleAxisf(current_pose[3], Eigen::Vector3f::UnitX());
    
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Rotation matrix: ");
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Vector n: (%f, %f, %f)", R(0,0), R(1,0), R(2,0));
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Vector s: (%f, %f, %f)", R(0,1), R(1,1), R(2,1));
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Vector a: (%f, %f, %f)", R(0,2), R(1,2), R(2,2));

    YPR = R.eulerAngles(2, 1, 0);   // R = Rz(yaw) * Ry(pich) * Rx(roll)
    RPY << YPR(2), YPR(1), YPR(0);
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "RPY: (%f, %f, %f)", RPY(0), RPY(1), RPY(2));

    R = Eigen::AngleAxisf(RPY(2), Eigen::Vector3f::UnitZ()) 
        * Eigen::AngleAxisf(RPY(1), Eigen::Vector3f::UnitY())
        * Eigen::AngleAxisf(RPY(0), Eigen::Vector3f::UnitX());
    
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Rotation matrix: ");
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Vector n: (%f, %f, %f)", R(0,0), R(1,0), R(2,0));
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Vector s: (%f, %f, %f)", R(0,1), R(1,1), R(2,1));
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Vector a: (%f, %f, %f)", R(0,2), R(1,2), R(2,2));
}

void PickAndPlaceUsingCamerasNode::chooseObject()
{
    float z_max = -INFINITY;
    obj_idx = -1;
    for (int i = 0; i < objects_pos.size(); i++)
    {
        RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Object %d. dim = (%f, %f, %f), pos = (%f, %f, %f). Num. captures %d.",
            i, objects_dim[i].x(), objects_dim[i].y(), objects_dim[i].z(), 
               objects_pos[i].x(), objects_pos[i].y(), objects_pos[i].z(), num_captures[i]);
        if (num_captures[i] >= min_num_captures && objects_pos[i].z() > z_max)
        {
            z_max = objects_pos[i].z();
            obj_idx = i;
        }
    }
}

void PickAndPlaceUsingCamerasNode::computeObjectApproachAndPickPose()
{
    Eigen::Matrix3f R;
    Eigen::Vector3f RPY, YPR;

    // For approaching from above
    R.col(0) << objects_pos[obj_idx].x(), objects_pos[obj_idx].y(), 0; R.col(0).normalize();
    R.col(1) << objects_pos[obj_idx].y(), -objects_pos[obj_idx].x(), 0; R.col(1).normalize();
    R.col(2) << 0, 0, -1;

    // For approaching by side
    // R.col(0) << 0, 0, -1;
    // R.col(1) << -objects_pos[obj_idx].y(), objects_pos[obj_idx].x(), 0; R.col(1).normalize();
    // R.col(2) << objects_pos[obj_idx].x(), objects_pos[obj_idx].y(), 0; R.col(2).normalize();

    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Rotation matrix: ");
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Vector n: (%f, %f, %f)", R(0,0), R(1,0), R(2,0));
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Vector s: (%f, %f, %f)", R(0,1), R(1,1), R(2,1));
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "Vector a: (%f, %f, %f)", R(0,2), R(1,2), R(2,2));

    YPR = R.eulerAngles(2, 1, 0);   // R = Rz(yaw) * Ry(pich) * Rx(roll)
    RPY << YPR(2), YPR(1), YPR(0);
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "RPY: (%f, %f, %f)", RPY(0), RPY(1), RPY(2));
    
    float fi = std::atan2(objects_pos[obj_idx].y(), objects_pos[obj_idx].x());
    float r = objects_pos[obj_idx].head(2).norm();
    if (r < 0.3)
        r += 1.5 * objects_dim[obj_idx].head(2).norm();
    else
        r -= 1.5 * objects_dim[obj_idx].head(2).norm();

    object_approach_pose = {1000 * r * float(cos(fi)), 
                            1000 * r * float(sin(fi)), 
                            1000 * objects_pos[obj_idx].z() + delta_z, 
                            RPY(0), 
                            RPY(1), 
                            RPY(2)};
    object_pick_pose = {1000 * objects_pos[obj_idx].x(), 
                        1000 * objects_pos[obj_idx].y(), 
                        1000 * objects_pos[obj_idx].z() + offset_z, 
                        RPY(0), 
                        RPY(1), 
                        RPY(2)};
}