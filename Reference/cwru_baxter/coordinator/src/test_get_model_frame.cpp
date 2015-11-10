// test_get_model_frame.cpp
// wsn, Aug 2015;
// cut from coordinator just to test comm w/ get_model_frame
//responds to GUI button for "move marker to grasp pose"

#include<ros/ros.h>
#include<std_msgs/Float32.h>
#include<geometry_msgs/PoseStamped.h>
#include<std_msgs/Bool.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <Eigen/Eigen>  
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Eigen/Dense>
#include <cwru_srv/IM_node_service_message.h>
#include <cwru_srv/simple_float_service_message.h>
#include <cwru_srv/simple_int_service_message.h>
#include <cwru_srv/arm_nav_service_message.h>
#include <tf/transform_listener.h>
#include <tf/LinearMath/Vector3.h>
#include <tf/LinearMath/QuadWord.h>

//const double H_CYLINDER = 0.24; // estimated height of cylinder
const double H_CYLINDER = 0.12; // estimated height of Coke can

using namespace std;

//#include <cwru_srv/>
//command codes for interactive marker: get/set pose
cwru_srv::IM_node_service_message pcl_getframe_msg;
cwru_srv::IM_node_service_message IM_6dof_srv_msg;
const int IM_GET_CURRENT_MARKER_POSE=0;
const int IM_SET_NEW_MARKER_POSE= 1;

//command codes for reachability node--only 1 action at present, so only need to provide z-height value
cwru_srv::simple_float_service_message reachability_msg;



//incoming command codes for this coordinator service:
const int RQST_DO_NOTHING = 0;
const int RQST_DISPLAY_REACHABILITY_AT_MARKER_HEIGHT = 1;
const int RQST_COMPUTE_MOVE_ARM_TO_PRE_POSE = 2;
const int RQST_COMPUTE_MOVE_ARM_TO_APPROACH_POSE = 3;
const int RQST_COMPUTE_MOVE_ARM_TO_GRASP_POSE = 4;
const int RQST_COMPUTE_MOVE_ARM_DEPART = 5;
const int RQST_COMPUTE_MOVE_ARM_TO_MARKER = 6;
const int RQST_MOVE_MARKER_TO_GRASP_POSE = 7;
const int RQST_EXECUTE_PLANNED_PATH=8;
const int RQST_DESCEND_20CM=9;
const int RQST_ASCEND_20CM=10;

const int RQST_COMPUTE_MOVE_ARM_JSPACE_CURRENT_TO_PRE_POSE = 11;
const int RQST_PREVIEW_TRAJECTORY=12;

//service codes to send to arm interface: these are in cartesian_moves/arm_motion_interface_defs.h
cwru_srv::arm_nav_service_message arm_nav_msg;
const int ARM_TEST_MODE =0;
//queries: 
const int ARM_IS_SERVER_BUSY_QUERY = 1;
const int ARM_QUERY_IS_PATH_VALID = 2;
const int ARM_GET_Q_DATA = 3;




//requests for motion plans:
const int ARM_PLAN_PATH_CURRENT_TO_GOAL_POSE=20; //plan paths from current arm pose
const int ARM_PLAN_PATH_CURRENT_TO_PRE_POSE=21;


const int ARM_PLAN_JSPACE_PATH_CURRENT_TO_PRE_POSE=22;
const int ARM_PLAN_JSPACE_PATH_CURRENT_TO_QGOAL=23;

const int ARM_PLAN_PATH_QSTART_TO_QGOAL = 25;
const int ARM_PLAN_PATH_QSTART_TO_ADES = 24; //specify start and end, j-space or hand pose

const int ARM_PLAN_PATH_ASTART_TO_QGOAL = 26;

// request to preview plan:
const int ARM_DISPLAY_TRAJECTORY = 50;

//MOVE command!
const int ARM_EXECUTE_PLANNED_PATH = 100;
const int ARM_DESCEND_20CM=101;
const int ARM_DEPART_20CM=102;

//response codes...
const int ARM_STATUS_UNDEFINED=0;
const int ARM_RECEIVED_AND_INITIATED_RQST=1;
const int ARM_REQUEST_REJECTED_ALREADY_BUSY=2;
const int ARM_SERVER_NOT_BUSY=3;
const int ARM_SERVER_IS_BUSY=4;
const int ARM_RECEIVED_AND_COMPLETED_RQST=5;
const int ARM_PATH_IS_VALID=6;
const int ARM_PATH_NOT_VALID=7;


bool g_trigger = false;
int g_coordinator_mode = RQST_DO_NOTHING;

tf::TransformListener *g_tfListener_ptr; //pointer to a global transform listener
ros::ServiceClient g_pcl_getframe_svc_client; //global service to talk to pcl
ros::ServiceClient g_arm_interface_svc_client;

//use this service to set processing modes interactively
// this is a very simple interface--expects an integer code in, and nothing gets returned;
// interact w/ dumb GUI

bool coordinatorService(cwru_srv::simple_int_service_messageRequest& request, cwru_srv::simple_int_service_messageResponse& response) {
    ROS_INFO("coordinator service callback activated");
    response.resp = true; // boring, but valid response info
    g_coordinator_mode = request.req;
    g_trigger = true; //signal that we received a request; trigger a response
    cout << "Mode set to: " << g_coordinator_mode << endl;
    return true;
}

geometry_msgs::PoseStamped get_model_pose_wrt_torso() {
geometry_msgs::PoseStamped poseStamped;
    
                    ROS_INFO("requesting model pose from pcl_perception: ");
                    pcl_getframe_msg.request.cmd_mode = 1; // not really used yet...service always assumes you want the model frame, in kinect_pc_frame coords
                    bool status = g_pcl_getframe_svc_client.call(pcl_getframe_msg);
                    if (status) {
                        ROS_INFO("service call successful");
                       
                    }
                    else {
                        ROS_INFO("service call was not successful");
                    }
                    geometry_msgs::PoseStamped pose_from_pcl, pose_wrt_torso;
                    pose_from_pcl = pcl_getframe_msg.response.poseStamped_IM_current;
                    ROS_INFO("got current model pose: x,y,z = %f, %f, %f",
                            pose_from_pcl.pose.position.x,
                            pose_from_pcl.pose.position.y,
                            pose_from_pcl.pose.position.z);
                    ROS_INFO("quaternion is: %f, %f, %f, %f",
                            pose_from_pcl.pose.orientation.x,
                            pose_from_pcl.pose.orientation.y,
                            pose_from_pcl.pose.orientation.z,
                            pose_from_pcl.pose.orientation.w);

                    // need to transform this into torso coords:
                    /*
                    tf::Vector3 pos = tf_kinect_wrt_torso.getOrigin();
                    tf::Quaternion tf_quaternion = tf_kinect_wrt_torso.getRotation();

                    quaternion.x = tf_quaternion.x();
                    quaternion.y = tf_quaternion.y();
                    quaternion.z = tf_quaternion.z();
                    quaternion.w = tf_quaternion.w();
                    ROS_INFO("transform components: ");
                    ROS_INFO("x,y, z = %f, %f, %f", pos[0], pos[1], pos[2]);
                    ROS_INFO("q x,y,z,w: %f %f %f %f", quaternion.x, quaternion.y, quaternion.z, quaternion.w);
                     * */
                    g_tfListener_ptr->transformPose("torso", pose_from_pcl, pose_wrt_torso);
                    ROS_INFO("model pose w/rt torso: ");
                    ROS_INFO("origin: %f, %f, %f", pose_wrt_torso.pose.position.x, pose_wrt_torso.pose.position.y, pose_wrt_torso.pose.position.z);
                    ROS_INFO("orientation: %f, %f, %f, %f", pose_wrt_torso.pose.orientation.x, pose_wrt_torso.pose.orientation.y, pose_wrt_torso.pose.orientation.z, pose_wrt_torso.pose.orientation.w);
                    return pose_wrt_torso;
    
}

bool arm_server_busy_wait_done() {
    int status= ARM_SERVER_IS_BUSY;
    int nwaits=0;
    cout<<"waiting on arm server: ";
    while (status != ARM_SERVER_NOT_BUSY) {
        arm_nav_msg.request.cmd_mode = ARM_IS_SERVER_BUSY_QUERY;
        g_arm_interface_svc_client.call(arm_nav_msg); 
        status = arm_nav_msg.response.rtn_code;
        cout<<".";
        nwaits++;
        //cout<<"status code: "<<status<<endl;
        ros::spinOnce();
        ros::Duration(0.1).sleep();
        if (nwaits>50) {
            ROS_WARN("coord: giving up waiting on arm interface");
            return false;
        }
    }
    cout<<"done"<<endl;
    return true;
}

//utility fnc to convert from a rotation matrix to a geometry_msgs::Quaternion
geometry_msgs::Quaternion quaternion_from_R(Eigen::Matrix3d R) {
    geometry_msgs::Quaternion quat_msg;
    Eigen::Quaterniond  eigen_quat(R);
    quat_msg.x = eigen_quat.x();
    quat_msg.y = eigen_quat.y();
    quat_msg.z = eigen_quat.z();
    quat_msg.w = eigen_quat.w();   
    return quat_msg;
}

int main(int argc, char **argv) {
    ros::init(argc, argv, "coordinator");
    ros::NodeHandle nh; //standard ros node handle   
    ros::Rate rate(2);
    bool status;

    geometry_msgs::Pose pose;
    geometry_msgs::PoseStamped poseStamped;
    geometry_msgs::PoseStamped pose_from_pcl, model_pose_wrt_torso;
    Eigen::Vector3d n_des, t_des, b_des;
    b_des << 0, 0, -1;
    n_des << 1, 0, 0;
    t_des = b_des.cross(n_des);

    Eigen::Matrix3d R_flange_down;
    R_flange_down.col(0) = n_des;
    R_flange_down.col(1) = t_des;
    R_flange_down.col(2) = b_des;
    
    geometry_msgs::Quaternion quat_gripper_down = quaternion_from_R(R_flange_down);
    
    //define a pre-pose in this node; note--may not be same as pre-pose in arm interface node
    Eigen::Matrix<double, 7, 1> coord_pre_pose;
    coord_pre_pose<< -0.907528, -0.111813, 2.06622, 1.8737, -1.295, 2.00164, -2.87179;   
    
    tf::TransformListener tfListener;
    g_tfListener_ptr = &tfListener;
    
    tf::StampedTransform tf_kinect_wrt_torso;
    // wait to start receiving valid tf transforms 
    /**/
    bool tferr = true;
    ROS_INFO("waiting for tf between kinect: camera_depth_optical_frame and torso...");
    while (tferr) {
        tferr = false;
        try {

            //The direction of the transform returned will be from the target_frame to the source_frame. 
            //Which if applied to data, will transform data in the source_frame into the target_frame. See tf/CoordinateFrameConventions#Transform_Direction
            tfListener.lookupTransform("camera_depth_optical_frame", "torso", ros::Time(0), tf_kinect_wrt_torso);
        } catch (tf::TransformException &exception) {
            ROS_ERROR("%s", exception.what());
            tferr = true;
            ros::Duration(0.5).sleep(); // sleep for half a second
            ros::spinOnce();
        }
    }
    ROS_INFO("tf is good");

    //communicate with node interactive_marker_node interactive_marker_node
    ROS_INFO("setting up a service client of rt_hand_marker");
    ros::ServiceClient IM_6dof_svc_client = nh.serviceClient<cwru_srv::IM_node_service_message>("IM6DofSvc");
    //ros::ServiceClient pcl_getframe_svc_client = nh.serviceClient<cwru_srv::IM_node_service_message>("pcl_getframe_svc");
    g_pcl_getframe_svc_client= nh.serviceClient<cwru_srv::IM_node_service_message>("pcl_getframe_svc");
    //talk to the reachability node:
    ros::ServiceClient reachability_svc_client = nh.serviceClient<cwru_srv::simple_float_service_message>("compute_reachability_svc");
    // talk to arm interface:
    g_arm_interface_svc_client= nh.serviceClient<cwru_srv::arm_nav_service_message>("cartMoveSvc");
    
    ros::ServiceServer service = nh.advertiseService("coordinator_svc", coordinatorService);
    double des_z_height=0.0;

    bool wait_done=true;
    geometry_msgs::Quaternion quaternion;
    while (ros::ok()) {
        if (g_trigger) {
            g_trigger = false; // reset the trigger

            switch (g_coordinator_mode) { // what we do here depends on our mode; mode is settable via a service
                case RQST_DO_NOTHING:
                    ROS_INFO("case DO_NOTHING; doing nothing!");
                    break;
                case RQST_MOVE_MARKER_TO_GRASP_POSE:
                    ROS_INFO("case MOVE_MARKER_TO_GRASP_POSE");                    
                    model_pose_wrt_torso = get_model_pose_wrt_torso();

                    // put the marker origin at the top of the can:
                    /*
                    pose = model_pose_wrt_torso.pose;
                    pose.position.z += H_CYLINDER;
                    pose.orientation = quat_gripper_down; // coerce orientation to point down
                    
                    poseStamped.pose = pose;
                    poseStamped.header.stamp = ros::Time::now();
                    poseStamped.header.frame_id= "torso";
                    IM_6dof_srv_msg.request.cmd_mode = IM_SET_NEW_MARKER_POSE;
                    IM_6dof_srv_msg.request.poseStamped_IM_desired = poseStamped;
                    ROS_INFO("placing IM at top of model");
                    status = IM_6dof_svc_client.call(IM_6dof_srv_msg);
                     * */
                    /*
                    ROS_INFO("return status: %d", status);
                    ROS_INFO("got current marker pose: x,y,z = %f, %f, %f",
                            IM_6dof_srv_msg.response.poseStamped_IM_current.pose.position.x,
                            IM_6dof_srv_msg.response.poseStamped_IM_current.pose.position.y,
                            IM_6dof_srv_msg.response.poseStamped_IM_current.pose.position.z);*/
                    break;
                case RQST_DISPLAY_REACHABILITY_AT_MARKER_HEIGHT:
                    IM_6dof_srv_msg.request.cmd_mode = IM_GET_CURRENT_MARKER_POSE;
                    status = IM_6dof_svc_client.call(IM_6dof_srv_msg);
                    des_z_height = IM_6dof_srv_msg.response.poseStamped_IM_current.pose.position.z;
                    
                    ROS_INFO("case DISPLAY_REACHABILITY_AT_MARKER_HEIGHT: z_des = %f",des_z_height);
                    // compute/display reachability at marker height:
                    reachability_msg.request.request_float32 = des_z_height;
                    reachability_svc_client.call(reachability_msg);  
                    //arm_server_busy_wait_done(); // no...don't wait on arm server!  wait on reachability
                    
                    break;
                case RQST_COMPUTE_MOVE_ARM_TO_PRE_POSE:
                    ROS_INFO("case COMPUTE_MOVE_ARM_TO_PRE_POSE");
                    arm_nav_msg.request.cmd_mode= ARM_PLAN_PATH_CURRENT_TO_PRE_POSE;
                    g_arm_interface_svc_client.call(arm_nav_msg); //need error checking here
                    arm_server_busy_wait_done();
                    //let's see if resulting plan was successful:
                    arm_nav_msg.request.cmd_mode= ARM_QUERY_IS_PATH_VALID; 
                    g_arm_interface_svc_client.call(arm_nav_msg);
                    if (arm_nav_msg.response.rtn_code==ARM_PATH_IS_VALID) {
                        ROS_INFO("computed a valid path");
                    }
                    break;
                case RQST_COMPUTE_MOVE_ARM_JSPACE_CURRENT_TO_PRE_POSE:
                     ROS_INFO("case RQST_COMPUTE_MOVE_ARM_JSPACE_CURRENT_TO_PRE_POSE");
                    arm_nav_msg.request.cmd_mode= ARM_PLAN_JSPACE_PATH_CURRENT_TO_QGOAL; //ARM_PLAN_JSPACE_PATH_CURRENT_TO_PRE_POSE;
                    //need to fill in goal:
                    arm_nav_msg.request.q_vec_end.resize(7);
                    for (int i=0;i<7;i++ )  {
                        arm_nav_msg.request.q_vec_end[i] = coord_pre_pose[i];
                    }
                    g_arm_interface_svc_client.call(arm_nav_msg); //need error checking here
                    arm_server_busy_wait_done();
                    //let's see if resulting plan was successful:
                    arm_nav_msg.request.cmd_mode= ARM_QUERY_IS_PATH_VALID; 
                    g_arm_interface_svc_client.call(arm_nav_msg);
                    if (arm_nav_msg.response.rtn_code==ARM_PATH_IS_VALID) {
                        ROS_INFO("computed a valid path");
                    }                   
                    break;
                case  RQST_PREVIEW_TRAJECTORY:
                     ROS_INFO("case RQST_PREVIEW_TRAJECTORY");
                    arm_nav_msg.request.cmd_mode= ARM_DISPLAY_TRAJECTORY; //request a trajectory preview
                    g_arm_interface_svc_client.call(arm_nav_msg); //need error checking here
                    arm_server_busy_wait_done();
                    break;
                    
                case RQST_EXECUTE_PLANNED_PATH:
                    ROS_INFO("case RQST_EXECUTE_PLANNED_PATH");
                    arm_nav_msg.request.cmd_mode= ARM_EXECUTE_PLANNED_PATH;     
                    g_arm_interface_svc_client.call(arm_nav_msg);
                    arm_server_busy_wait_done();
                    break;
                case RQST_DESCEND_20CM:
                    ROS_INFO("case RQST_DESCEND_20CM");
                    arm_nav_msg.request.cmd_mode= ARM_DESCEND_20CM;     
                    g_arm_interface_svc_client.call(arm_nav_msg);  
                    arm_server_busy_wait_done();
                    break;
                 case RQST_ASCEND_20CM:
                    ROS_INFO("case RQST_ASCEND_20CM");
                    arm_nav_msg.request.cmd_mode= ARM_DEPART_20CM;     
                    g_arm_interface_svc_client.call(arm_nav_msg);   
                    arm_server_busy_wait_done();
                    break;
                    
                case RQST_COMPUTE_MOVE_ARM_TO_APPROACH_POSE:
                    ROS_INFO("case RQST_COMPUTE_MOVE_ARM_TO_APPROACH_POSE; doing nothing!");
                    break;
                case RQST_COMPUTE_MOVE_ARM_TO_GRASP_POSE:
                    ROS_INFO("case RQST_COMPUTE_MOVE_ARM_TO_GRASP_POSE; doing nothing!");
                    arm_server_busy_wait_done();
                    break;
                case RQST_COMPUTE_MOVE_ARM_DEPART:
                    ROS_INFO("case RQST_COMPUTE_MOVE_ARM_DEPART; doing nothing!");
                    arm_server_busy_wait_done();
                    break;
                case RQST_COMPUTE_MOVE_ARM_TO_MARKER: //misonomer--this is a request to compute a plan
                    ROS_INFO("RQST_COMPUTE_MOVE_ARM_TO_MARKER: planning move to marker pose");
                    IM_6dof_srv_msg.request.cmd_mode = IM_GET_CURRENT_MARKER_POSE;
                    status = IM_6dof_svc_client.call(IM_6dof_srv_msg);
                    ROS_INFO("got current marker pose: x,y,z = %f, %f, %f",
                            IM_6dof_srv_msg.response.poseStamped_IM_current.pose.position.x,
                            IM_6dof_srv_msg.response.poseStamped_IM_current.pose.position.y,
                            IM_6dof_srv_msg.response.poseStamped_IM_current.pose.position.z);
                    //fill in IM goal; arm interface will get starting pose from sensor vals
                    arm_nav_msg.request.poseStamped_goal = IM_6dof_srv_msg.response.poseStamped_IM_current;
                    
                    arm_nav_msg.request.cmd_mode= ARM_PLAN_PATH_CURRENT_TO_GOAL_POSE;  

                    g_arm_interface_svc_client.call(arm_nav_msg);           
                    wait_done=arm_server_busy_wait_done(); 
                    //let's see if resulting plan was successful:
                    arm_nav_msg.request.cmd_mode= ARM_QUERY_IS_PATH_VALID; 
                    g_arm_interface_svc_client.call(arm_nav_msg);
                    if (arm_nav_msg.response.rtn_code==ARM_PATH_IS_VALID) {
                        ROS_INFO("computed a valid path");
                    }
                    else
                        ROS_WARN("did not compute a valid path");
                    break;
                    /**/

                default:
                    ROS_WARN("this mode is not implemented");

            }
        }

        ros::spinOnce();
        rate.sleep();
    }
    return 0;
}
