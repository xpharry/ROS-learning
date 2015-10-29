// traj_action_client_pre_pose: 
// wsn, June, 2015
// uses traj_interpolator_as to send robot to pre pose--right arm lifted/retracted for grasp from above

#include <ros/ros.h>
#include <actionlib/client/simple_action_client.h>
#include <actionlib/client/terminal_state.h>
#include <my_interesting_moves/baxter_right_arm.h>
//this #include refers to the new "action" message defined for this package
// the action message can be found in: .../my_interesting_moves/action/traj.action
// automated header generation creates multiple headers for message I/O
// these are referred to by the root name (traj) and appended name (Action)
// If you write a new client of the server in this package, you will need to include Baxter_right_arm in your package.xml,
// and include the header file below
#include <my_interesting_moves/trajAction.h>
using namespace std;
#define VECTOR_DIM 7 // e.g., a 7-dof vector

// This function will be called once when the goal completes
// this is optional, but it is a convenient way to get access to the "result" message sent by the server
void doneCb(const actionlib::SimpleClientGoalState& state,
        const my_interesting_moves::trajResultConstPtr& result) {
    ROS_INFO(" doneCb: server responded with state [%s]", state.toString().c_str());
    ROS_INFO("got return val = %d; traj_id = %d",result->return_val,result->traj_id);
}


int main(int argc, char** argv) {
    ros::init(argc, argv, "traj_action_client_node"); // name this node 
    ros::NodeHandle nh; //standard ros node handle        
    int g_count = 0;
    int ans;

    //cin>>ans;
    Baxter_right_arm baxter_right_arm(&nh); //instantiate a Baxter_right_arm object and pass in pointer to nodehandle for constructor to use  
    // warm up the joint-state callbacks;
    cout<<"warming up callbacks..."<<endl;
    for (int i=0;i<100;i++) {
        ros::spinOnce();
        //cout<<"spin "<<i<<endl;
        ros::Duration(0.01).sleep();
    }
        
    trajectory_msgs::JointTrajectory des_trajectory; // an empty trajectory 
    
    baxter_right_arm.set_goal_salute(des_trajectory);

    // here is a "goal" object compatible with the server, as defined in example_action_server/action
    my_interesting_moves::trajGoal goal; 
    // does this work?  copy traj to goal:
    goal.trajectory = des_trajectory;
    //cout<<"ready to connect to action server; enter 1: ";
    //cin>>ans;
    // use the name of our server, which is: trajActionServer (named in traj_interpolator_as.cpp)
    actionlib::SimpleActionClient<my_interesting_moves::trajAction> action_client("trajActionServer", true);
    
    // attempt to connect to the server:
    ROS_INFO("waiting for server: ");
    bool server_exists = action_client.waitForServer(ros::Duration(5.0)); // wait for up to 5 seconds
    // something odd in above: does not seem to wait for 5 seconds, but returns rapidly if server not running


    if (!server_exists) {
        ROS_WARN("could not connect to server; will wait forever");
        return 0; // bail out; optionally, could print a warning message and retry
    }
    server_exists = action_client.waitForServer(); //wait forever 
    
   
    ROS_INFO("connected to action server");  // if here, then we connected to the server;

    //while(true) {
    // stuff a goal message:
    g_count++;
    goal.traj_id = g_count; // this merely sequentially numbers the goals sent
    ROS_INFO("sending traj_id %d",g_count);
    //action_client.sendGoal(goal); // simple example--send goal, but do not specify callbacks
    action_client.sendGoal(goal,&doneCb); // we could also name additional callback functions here, if desired
    //    action_client.sendGoal(goal, &doneCb, &activeCb, &feedbackCb); //e.g., like this
    
    bool finished_before_timeout = action_client.waitForResult(ros::Duration(5.0));
    //bool finished_before_timeout = action_client.waitForResult(); // wait forever...
    if (!finished_before_timeout) {
        ROS_WARN("giving up waiting on result for goal number %d",g_count);
        return 0;
    }
    else {
        ROS_INFO("finished before timeout");
    }
    
    //}

    return 0;
}

