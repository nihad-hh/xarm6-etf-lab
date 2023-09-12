#include "base/PickAndPlaceNode.h"
#include "environment/AABB.h"

#include <sensor_msgs/msg/point_cloud2.hpp>
#include <pcl/point_cloud.h>
#include <pcl_conversions/pcl_conversions.h>

namespace real_bringup
{
    class PickAndPlaceUsingCamerasNode : public real_bringup::PickAndPlaceNode,
                                         public real_bringup::AABB
    {
    public:
        PickAndPlaceUsingCamerasNode(const std::string node_name, const std::string config_file_path);
        
        void pickAndPlaceCallback() override { pickAndPlaceUsingCamerasCallback(); };
        void pickAndPlaceUsingCamerasCallback();
        const int chooseObject() override;
        void computeObjectApproachAndPickPose();

    private:
        std::vector<float> object_approach_pose;
        std::vector<float> object_pick_pose;
        int obj_idx;
        float delta_z = 120;
        float offset_z = 10;

        enum Task
        {
            waiting_for_object,
            choosing_object,
            going_towards_object,
            picking_object,
            raising_object,
            releasing_object,
            moving_object_to_destination
        };
        Task task;

    };
}