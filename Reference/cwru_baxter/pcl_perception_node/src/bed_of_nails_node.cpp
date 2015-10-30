// bed_of_nails_node.cpp: wsn, July, 2015
// variant on pcl_perception; 
// for cluster of points of a perceived object, consider 
// viewpoint of z-depth of ordered (x,y)--> bed-of-nails description
// choose resolution, e.g. 128x128
// for each patch (w/ points), set depth = avg z value
// complementary map is binary: points or no points in cell (ix,jy)
// 


// example code to acquire a pointcloud from disk, then perform various processing steps interactively.
// processing is invoked by point-cloud selections in rviz, as well as "mode" settings via a service
// e.g.:  rosservice call process_mode 0 induces processing in mode zero (plane fitting)

#include <stdlib.h>
#include <math.h>
#include <iostream>
#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h> 
#include <geometry_msgs/PointStamped.h>
#include <geometry_msgs/Quaternion.h>
#include <geometry_msgs/Transform.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/Vector3.h>
#include <geometry_msgs/Wrench.h>
#include <std_msgs/Float64MultiArray.h>
#include <geometry_msgs/Point.h>
#include <geometry_msgs/Pose.h>
//#include <tf_eigen.h>
//#include <tf_conversions.h>

#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
//#include <pcl_ros/point_cloud.h>
//#include <pcl/ros/conversions.h>
#include <pcl/features/normal_3d.h>

#include <pcl/PCLPointCloud2.h>

#include <cwru_srv/simple_int_service_message.h> // this is a pre-defined service message, contained in shared "cwru_srv" package
#include <cwru_srv/IM_node_service_message.h>

#include <Eigen/Eigen>
#include <Eigen/Dense>
#include <Eigen/Geometry>

#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>
//#include <tf/transform_datatypes.h>

//#include <pcl_ros/transforms.h>
#include <pcl/filters/extract_indices.h>
#include <pcl-1.7/pcl/impl/point_types.hpp>

#include <pcl_conversions/pcl_conversions.h>

//typedef pcl::PointCloud<pcl::PointXYZ> PointCloud; // can use this for short-hand

using namespace std;
using namespace Eigen;
using namespace pcl;
using namespace pcl::io;




//#include <pcl_perception_node/pcl_perception_defs.h>
/**/
//define some processing modes; set these interactively via service
const int PCL_IDENTIFY_PLANE = 0;
const int PCL_FIND_PNTS_ABOVE_PLANE = 1;
const int PCL_COMPUTE_CYLINDRICAL_FIT_ERR_INIT = 2;
const int PCL_COMPUTE_CYLINDRICAL_FIT_ERR_ITERATE = 3;
const int PCL_MAKE_CAN_CLOUD = 4;
const int PCL_FIND_ON_TABLE = 5;
const int PCL_TAKE_SNAPSHOT = 6;
const int PCL_COMPUTE_Z_BED_OF_NAILS = 7;
const int PCL_FIND_COKE_FRAME = 8;
const int PCL_FIND_GAZEBO_BEER_FRAME = 9;
const int PCL_SORT_POINTS_BY_HORIZ_SLABS = 10;
const int PCL_FIND_FLOOR = 11;
const int PCL_FIND_TABLE = 12;
const int PCL_INQUIRE_STATUS = 13;
const int PCL_FIND_GAZEBO_BEER_GRASP_FROM_ABOVE_FRAME = 14;
const int PCL_FIND_COKE_CAN_GRASP_FROM_ABOVE_FRAME = 15;
const int PCL_TRAIN_NEW_OBJECT = 16;
const int PCL_SAMPLE_OBJECT = 17;

const int PCL_STATUS_BUSY = 1;
const int PCL_STATUS_IDLE = 0;
bool g_status = PCL_STATUS_IDLE;

const int PCL_FRAME_SVC_GET_MODEL_FRAME_WRT_KINECT = 1;
const int PCL_FRAME_SVC_GET_MODEL_FRAME_WRT_TORSO = 2;
const int PCL_FRAME_SVC_GET_GRASP_FRAME_WRT_MODEL = 3;
const int PCL_FRAME_SVC_GET_GRASP_FRAME_WRT_TORSO = 4;

const double Z_EPS = 0.02; //choose a tolerance for plane fitting, e.g. 1cm
const double R_EPS = 0.05; // choose a tolerance for cylinder-fit outliers

//const double R_CYLINDER = 0.055; //estimated from ruler tool...example to fit a cylinder of this radius to data
//const double H_CYLINDER = 0.24; // estimated height of cylinder

//fit to coke can from Kinect view
const double R_CYLINDER = 0.03; //estimated from ruler tool...example to fit a cylinder of this radius to data
const double H_CYLINDER = 0.12; // estimated height of cylinder
/**/
// set training window x and y range; nom x = 0.7, y = 0
const double g_x_view_min = 0.35; //0.3 picks up some of e-stop box
const double g_x_view_max = 0.9;
const double g_y_view_min = -0.2;
const double g_y_view_max = 0.2;
const int silhouette_size = 128; //set chosen resolution for sampling "bed of nails"
//const int g_ictr = floor(silhouette_size/2);
//const int g_jctr = floor(silhouette_size/2);

const double default_table_z_height_wrt_baxter_base_frame = -0.213; // experimentally, this is height of stool relative to baxter frame;
// OK for default, but more generally, replace this with data from perception in calls to find objects
// gazebo: table height w/rt base = -0.154706



Eigen::Vector3f computeCentroid(PointCloud<pcl::PointXYZ>::Ptr pcl_cloud);
void computeRsqd(PointCloud<pcl::PointXYZ>::Ptr pcl_cloud, Eigen::Vector3f centroid, std::vector<float> &rsqd_vec);
Eigen::Vector3f computeCentroid(PointCloud<pcl::PointXYZ>::Ptr pcl_cloud, std::vector<int>iselect);
void transform_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, Eigen::Matrix3f R_xform, PointCloud<pcl::PointXYZ>::Ptr outputCloud);
void transform_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, Eigen::Matrix3f R_xform, Eigen::Vector3f offset, PointCloud<pcl::PointXYZ>::Ptr outputCloud);
void transform_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, Eigen::Affine3f A, PointCloud<pcl::PointXYZ>::Ptr outputCloud);
//void transform_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, tf::Transform xform,  PointCloud<pcl::PointXYZ>::Ptr outputCloud);

void filter_cloud_z(PointCloud<pcl::PointXYZ>::Ptr inputCloud, double z_nom, double z_eps, vector<int> &indices);
void filter_cloud_above_z(PointCloud<pcl::PointXYZ>::Ptr inputCloud, double z_threshold, vector<int> &indices);
//variant to restrict search to specified bounding box (in base coords)
void filter_cloud_above_z(PointCloud<pcl::PointXYZ>::Ptr inputCloud, double z_threshold, double x_min, double x_max,
        double y_min, double y_max, vector<int> &indices);

void compute_depth_map_z(PointCloud<pcl::PointXYZ>::Ptr inputCloud, vector<int> &indices,
        float x_min, float x_max, float y_min, float y_max,
        int silhouette_z[silhouette_size][silhouette_size], float depth_z[silhouette_size][silhouette_size],
        int &ix_centroid, int &jy_centroid);
void find_bounding_box(PointCloud<pcl::PointXYZ>::Ptr inputCloud, vector<int> &indices,
        float &x_min, float &x_max, float &y_min, float &y_max, float &z_min, float &z_max);

void copy_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, PointCloud<pcl::PointXYZ>::Ptr outputCloud);
void copy_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, vector<int> &indices, PointCloud<pcl::PointXYZ>::Ptr outputCloud);

void find_plane(Eigen::Vector4f plane_params, std::vector<int> &indices_z_eps);
// given a cloud, sort the points by horizontal "slabs" of thickness dz_slab from z_min to z_max search range;
// return the indices of points within each slab, as well as top/bottom dims of each slab;
// slabs overlap slightly
void sort_pts_by_horiz_planes(PointCloud<pcl::PointXYZ>::Ptr inputCloudWrtBase, double z_search_min, double z_search_max, double dz_slab,
        std::vector<vector<int> > &indices_by_slab, int &i_best_slab, vector<float> &z_mins, vector<float> &z_maxs);

void sort_pts_by_horiz_planes(PointCloud<pcl::PointXYZ>::Ptr inputCloudWrtBase, vector<int> indices, double z_search_min, double z_search_max, double dz_slab,
        std::vector<vector<int> > &indices_by_slab, int &i_best_slab, vector<float> &z_mins, vector<float> &z_maxs);

void process_patch(std::vector<int> &iselect_filtered, Eigen::Vector3f &centroidEvec3f, Eigen::Vector4f &plane_params);
void compute_radial_error(PointCloud<pcl::PointXYZ>::Ptr inputCloud, std::vector<int> indices, double r, Eigen::Vector3f center, double &E, double &dEdCx, double &dEdCy);
void make_can_cloud(PointCloud<pcl::PointXYZ>::Ptr canCloud, double r_can, double h_can);
int compute_silhouette_centroid(int silhouette_z[silhouette_size][silhouette_size], int &ix_centroid, int &jy_centroid);
bool recenter_silhouette(int silhouette_z[silhouette_size][silhouette_size], int ix_centroid, int jy_centroid);
bool recenter_silhouette_and_depth_map_z(int silhouette_z[silhouette_size][silhouette_size], float depth_z[silhouette_size][silhouette_size],
        int ix_centroid, int jy_centroid);
void display_silhouette_z(int silhouette_z[silhouette_size][silhouette_size]);
bool sample_object(); // take N snapshots and compute average (centered) top-view projection, silhouette(i,j) and z(i,j)

ros::Publisher g_pubCloud;  // make this publisher global

//next line causes a seg-fault!! even if you don't use ne
// SEEMS TO BE DUE TO C++11 FLAGS--DO NOT COMPILE W/ C++11 !!
//pcl::NormalEstimation<pcl::PointXYZ, pcl::Normal> ne;
// a bunch of pointcloud holders, all global

//pcl::PointCloud<pcl::PointXYZ>::Ptr g_pclKinect(new PointCloud<pcl::PointXYZ>);
pcl::PointCloud<pcl::PointXYZ>::Ptr g_cloud_from_disk(new pcl::PointCloud<pcl::PointXYZ>); //this one is read from PCD file on disk
pcl::PointCloud<pcl::PointXYZ>::Ptr g_cloud_out(new pcl::PointCloud<pcl::PointXYZ>); // holder for processed point clouds
pcl::PointCloud<pcl::PointXYZ>::Ptr g_cloud_transformed(new pcl::PointCloud<pcl::PointXYZ>); // holder for processed point clouds
pcl::PointCloud<pcl::PointXYZ>::Ptr g_cloud_wrt_base(new pcl::PointCloud<pcl::PointXYZ>); // holder for processed point clouds

pcl::PointCloud<pcl::PointXYZ>::Ptr g_display_cloud(new pcl::PointCloud<pcl::PointXYZ>); // this cloud gets published--viewable in rviz

//use this cloud to hold points of an object model, expressed relative to table-top height;
//ideally, these would be in a table frame...but for now, assume table normal is parallel to torso-frame z-component
pcl::PointCloud<pcl::PointXYZ>::Ptr g_object_cloud(new pcl::PointCloud<pcl::PointXYZ>); // this cloud gets published--viewable in rviz
int g_silhouette_z[silhouette_size][silhouette_size];
float g_depth_z[silhouette_size][silhouette_size];
int g_num_snapshots_avg = 10;
std::string g_object_name;
int g_ans;

//PointCloud<pcl::PointXYZ>::Ptr canCloud

pcl::PointCloud<pcl::PointXYZ>::Ptr g_pclSelect(new pcl::PointCloud<pcl::PointXYZ>); // holds published points, per Rviz tool
pcl::PointCloud<pcl::PointXYZ>::Ptr g_canCloud(new pcl::PointCloud<pcl::PointXYZ>); // holds model for a can
pcl::PointCloud<pcl::PointXYZ>::Ptr g_pclKinect(new pcl::PointCloud<pcl::PointXYZ>); //this one is from kinect stream

sensor_msgs::PointCloud2::Ptr g_pcl2_display_cloud(new sensor_msgs::PointCloud2);
// PointXYZRGB would be colorized

Eigen::Matrix3f g_R_transform; // a matrix useful for rotating the data

Eigen::Vector3f g_cylinder_origin; // origin of model for cylinder registration

int g_pcl_process_mode = PCL_TAKE_SNAPSHOT; // mode--set by service
bool g_trigger = true; // a trigger, to tell "main" to process points in the currently selected mode
bool g_processed_patch = false; // a state--to let us know if a default set of plane_parameters exists
bool g_take_snapshot = true; // let kinect CB save data ONLY when desired

//more globals--to share info on planes and patches
Eigen::Vector4f g_plane_params;
Eigen::Vector3f g_patch_centroid;
Eigen::Vector3f g_plane_normal;
Eigen::Vector3f g_plane_origin;
Eigen::Vector3f g_base_origin;

Eigen::Vector3f g_patch_origin;
Eigen::Affine3f g_A_plane, g_A_model_wrt_torso, g_A_model_wrt_base;
Eigen::Affine3f g_A_grasp_wrt_model, g_A_grasp_wrt_base;
Eigen::Affine3d g_A3d_model;
Eigen::Affine3f g_affine_kinect_to_base;
Eigen::Affine3f g_affine_kinect_to_head, g_affine_head_to_base;
double g_z_plane_nom;
double g_table_z_wrt_base = -0.217; // default value for stool
std::vector<int> g_indices_of_plane; //indices of patch that do not contain outliers 
tf::StampedTransform g_kinect_sensor_to_base_link;
tf::StampedTransform g_head_to_base_link, g_kinect_sensor_to_head_link;
//void pcl_ros::transformPointCloud 	( 	const pcl::PointCloud< PointT > &  	cloud_in,
//		pcl::PointCloud< PointT > &  	cloud_out,
//		const tf::Transform &  	transform 
//	) 	
tf::Transform g_tf_kinect_sensor_to_base_link;
tf::Transform g_tf_kinect_sensor_to_head_link;
tf::Transform g_tf_head_to_base_link;
// shortcut for now...later, object_finder should be a library
#include "object_finder.cpp"

Eigen::Affine3f eigen_affine_3d_to_3f(Eigen::Affine3d affine3d) {
    Eigen::Vector3f origin3f;
    Eigen::Vector3d origin3d;
    Eigen::Matrix3f R3f;
    Eigen::Matrix3d R3d;
    Eigen::Affine3f affine3f;
    origin3d = affine3d.translation();
    R3d = affine3d.linear();
    for (int i = 0; i < 3; i++) {
        origin3f(i) = origin3d(i);
    }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            R3f(i, j) = R3d(i, j);
        }
    affine3f.translation() = origin3f;
    affine3f.linear() = R3f;
    return affine3f;
}

void transformTFToEigen(const tf::Transform &t, Eigen::Affine3f &e) {
    for (int i = 0; i < 3; i++) {
        e.matrix()(i, 3) = t.getOrigin()[i];
        for (int j = 0; j < 3; j++) {
            e.matrix()(i, j) = t.getBasis()[i][j];
        }
    }
    // Fill in identity in last row
    for (int col = 0; col < 3; col++)
        e.matrix()(3, col) = 0;
    e.matrix()(3, 3) = 1;
}

geometry_msgs::Pose transformEigenAffine3fToPose(Eigen::Affine3f e) {
    Eigen::Vector3f Oe;
    Eigen::Matrix3f Re;
    geometry_msgs::Pose pose;
    Oe = e.translation();
    Re = e.linear();

    Quaternionf q(Re); // convert rotation matrix Re to a quaternion, q
    pose.position.x = Oe(0);
    pose.position.y = Oe(1);
    pose.position.z = Oe(2);

    pose.orientation.x = q.x();
    pose.orientation.y = q.y();
    pose.orientation.z = q.z();
    pose.orientation.w = q.w();

    return pose;
}

//UNTESTED!!

void transformEigenAffine3fToTF(Eigen::Affine3f e, tf::Transform &t) {
    tf::Matrix3x3 Rtf;
    tf::Vector3 Otf;
    Eigen::Vector3f Oe;
    Eigen::Matrix3f Re;
    Oe = e.translation();
    Re = e.linear();
    for (int i = 0; i < 3; i++) {
        Otf[i] = Oe[i];
    }
    t.setOrigin(Otf);

    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            Rtf[i][j] = Re(i, j);
        }
    t.setBasis(Rtf);
    // need to do something with header...
}

/**/
void display_pts(PointCloud<pcl::PointXYZ>::Ptr pcl_cloud, std::vector<int>iselect) {
    Eigen::Vector3f pt;
    int nselect = iselect.size();
    for (int i = 0; i < nselect; i++) {
        pt = pcl_cloud->points[iselect[i]].getVector3fMap();
        cout << pt.transpose() << endl;
    }
}

// process patch: filter selected points to remove outliers;
// then compute the centroid and the plane parameters of the filtered points
// return these values in centroidEvec3f and plane_params

void process_patch(std::vector<int> &iselect_filtered, Eigen::Vector3f &centroidEvec3f, Eigen::Vector4f &plane_params) {
    ROS_INFO("PROCESSING THE PATCH: ");
    int npts = g_pclSelect->width * g_pclSelect->height;
    if (npts < 1) {
        ROS_WARN("received patch has no points!!!");
        return;
    }
    //ROS_INFO("frame_id of g_pclSelect: %s\n",g_pclSelect->header.frame_id.c_str());
    //cout<<("(this seems to be a lie; presumably this is actually kinect_pc_frame or camera_rgb_optical_frame)");
    centroidEvec3f = computeCentroid(g_pclSelect); // compute the centroid of this point cloud (selected patch)

    std::vector<float> rsqd_vec;
    computeRsqd(g_pclSelect, centroidEvec3f, rsqd_vec);
    ROS_INFO("computing rsqd vec: ");
    float variance = 0.0;
    for (int i = 0; i < rsqd_vec.size(); i++) {
        variance += rsqd_vec[i];
        //cout<<rsqd_vec[i]<<", ";      
    }
    //cout << endl;
    if (npts > 0)
        variance /= ((float) npts);

    // now, eliminate any outliers; actually, keep only points withing 1 std;
    for (int i = 0; i < npts; i++) {
        if (rsqd_vec[i] < variance) {
            iselect_filtered.push_back(i); // choosey: retain only points within 1 std dev
        }
    }
    ROS_INFO("computing centroid \n");
    ROS_INFO("npts = %d", npts);
    int npts_filtered = iselect_filtered.size();
    ROS_INFO("npts of filtered patch: %d \n", npts_filtered);
    //cout << "variance = " << variance << endl;
    //cout << "std_dev: " << sqrt(variance) << endl;
    centroidEvec3f = computeCentroid(g_pclSelect, iselect_filtered);
    ROS_INFO("refined centroid:   %f, %f, %f \n", centroidEvec3f(0), centroidEvec3f(1), centroidEvec3f(2));

    // next line causes a core dump, if compile w/ C++11 !!
    //  pcl::NormalEstimation<pcl::PointXYZ, pcl::Normal> ne;
    NormalEstimation<PointXYZ, Normal> n; // object to compute the normal to a set of points

    float curvature;

    n.computePointNormal(*g_pclSelect, iselect_filtered, plane_params, curvature); // find plane params for filtered patch
    // any surface viewed w/ z_optical pointing "out" from camera must have a surface normal with z-component that is negative w/rt camera
    if (plane_params[2] > 0.0) {
        //need to negate the surface normal, as well as (signed) distance of plane from base origin
        for (int i = 0; i < 4; i++)
            plane_params[i] *= -1.0;
    }
    ROS_INFO("plane_params, filtered patch: %f, %f, %f, %f \n", plane_params(0), plane_params(1), plane_params(2), plane_params(3));
    /*
    std::cout<< "here are z-values of preserved points: "<<endl;
    int j;
    Eigen::Vector3f sel_pt;
    for (int i=0;i<iselect_filtered.size();i++) {
        j = iselect_filtered[i];
        sel_pt= g_pclSelect->points[j].getVector3fMap();   
        cout<<sel_pt.transpose()<<endl;
    }
     */
    ROS_INFO("done processing patch\n");
}


// this function operates on the global cloud pointer g_pclKinect;
// g_cloud_transformed contains a cloud rotated s.t. identified plane has normal (0,0,1), 
//  indices_z_eps contain the indices of the points on the identified plane;
//  g_display_cloud is a reduced version of g_pclKinect, w/ only the planar points (expressed in original frame)

/**/
void find_plane(Eigen::Vector4f plane_params, std::vector<int> &indices_z_eps) {
    float curvature;
    std::vector<int> iselect_all;
    NormalEstimation<PointXYZ, Normal> n; // object to compute the normal to a set of points 

    //Eigen::Vector3f plane_normal;
    // plane normal, in sensor-frame coords 
    Eigen::Vector3f x_dir;
    for (int i = 0; i < 3; i++) g_plane_normal[i] = plane_params[i]; //for selected patch, get plane normal from point-cloud processing result

    // construct a frame w/rt planar patch, expressed in sensor coords
    x_dir << 1, 0, 0; // keep x-axis the same...nominally
    x_dir = x_dir - g_plane_normal * (g_plane_normal.dot(x_dir)); // force x-dir to be orthogonal to z-dir
    x_dir /= x_dir.norm(); // want this to be unit length as well
    //populate g_R_transform with the direction vectors of the plane frame, with respect to the kinect frame
    g_R_transform.col(0) = x_dir;
    g_R_transform.col(2) = g_plane_normal; // want the z-axis to be the plane normal    
    g_R_transform.col(1) = g_R_transform.col(2).cross(g_R_transform.col(0)); //make y-axis consistent right-hand triad
    // let's define an origin on this plane as well.  The patch centroid should do
    g_plane_origin = g_patch_centroid;
    // nope...try using base-frame origin:
    //g_base_origin(0)=0.0;
    //g_base_origin(1)=0.0;
    //g_base_origin(2)=g_patch_centroid(2); //0.0; //set z height of this frame equal to elevation of planar surface


    // define the transform s.t. A*pt_wrt_plane coords = pt_wrt_sensor_coords
    g_A_plane.linear() = g_R_transform;
    //g_A_plane.translation() = g_base_origin; //g_plane_origin; //hmm...did not work
    g_A_plane.translation() = g_plane_origin;
    cout << "Defining plane coord sys: origin: " << g_plane_origin.transpose() << endl;
    cout << "Orientation from plane: " << endl;
    cout << g_R_transform << endl;

    // test: transform the selected points into new plane frame:
    Eigen::Vector3f selected_pt;

    transform_cloud(g_pclSelect, g_A_plane.inverse(), g_cloud_transformed); // transform the point cloud selection

    int npts_selected = g_cloud_transformed->width * g_cloud_transformed->height;
    cout << "transformed " << npts_selected << " points from selection" << endl;
    for (int i = 0; i < npts_selected; ++i) {
        selected_pt = g_cloud_transformed->points[i].getVector3fMap();
        cout << selected_pt.transpose() << endl;
    }

    // use the following to transform kinect points into the plane frame; could do translation as well, but not done here
    //Eigen::Matrix3f R_transpose = g_R_transform.transpose();

    g_z_plane_nom = 0.0; //plane_params[3]; // distance of plane from sensor origin--same as distance measured along plane normal--so should be negative
    //cout<<"nom plane dist from sensor origin: "<<g_z_plane_nom<<endl;

    // after rotating the points to align with the plane of the selected patch, all z-values should be approximately the same,
    // = z_plane_nom
    double z_eps = Z_EPS; // choose a tolerance for plane inclusion +/- z; 1cm??

    //int npts_pclKinect = g_pclKinect->width * g_pclKinect->height;
    // cout<<"npts_pclKinect = "<<npts_pclKinect<<endl;

    //transform the ENTIRE point cloud:
    //transform_cloud(g_pclKinect, R_transpose, g_cloud_transformed); // rotate the entire point cloud
    transform_cloud(g_pclKinect, g_A_plane.inverse(), g_cloud_transformed); // transform the entire point cloud   
    //test test test...look for points near z=0
    int npts_transformed = g_pclKinect->width * g_pclKinect->height;
    cout << "from kinect cloud, g_pclKinect npts_transformed = " << npts_transformed << endl;

    //int ans;
    //cout<<"transformed "<<npts_transformed<<" points from selection"<<endl;
    /*
    for (int i = 0; i < npts_transformed; ++i) {
        selected_pt = g_cloud_transformed->points[i].getVector3fMap();
        cout<<selected_pt.transpose()<<endl;
        if (fabs(selected_pt[2])<z_eps) {
            cout<<"found a point on selected plane...enter 1:";
            cin>>ans;
        }
    }    
     * */
    // g_cloud_transformed is now expressed in the frame of the selected plane;
    // let's extract all of the points (i.e., name the indices of these points) for which the z value corresponds to the chosen plane,
    // within tolerance z_eps
    //filter_cloud_z(g_cloud_transformed, g_z_plane_nom, z_eps, indices_z_eps);
    filter_cloud_z(g_cloud_transformed, 0.0, z_eps, indices_z_eps); //transform --> z-values of points on plane should be 0.0
    //display_pts(g_cloud_transformed,indices_z_eps);
    // point indices of interest are in indices_z_eps; use this to extract this subset from the parent cloud to create a new cloud
    copy_cloud(g_pclKinect, indices_z_eps, g_display_cloud); //g_display_cloud is being published regularly by "main"
    //g_cloud_wrt_base= g_kinect_sensor_to_base_link*g_pclKinect;
    cout << "transforming cloud into base coords" << endl;
    transform_cloud(g_pclKinect, g_affine_kinect_to_base.inverse(), g_cloud_wrt_base);
    // compute the height of the table in base coords
    g_table_z_wrt_base = 0.0;
    int npts = indices_z_eps.size();
    Eigen::Vector3f pt;

    //unnecessarily slow--could simply transform centroid of the selected points
    for (int i = 0; i < npts; i++) {
        pt = g_cloud_wrt_base->points[indices_z_eps[i]].getVector3fMap();
        g_table_z_wrt_base += pt(2);
    }
    g_table_z_wrt_base /= npts;
    ROS_INFO("table height w/rt base = %f", g_table_z_wrt_base);
    //display_pts(g_cloud_wrt_base,indices_z_eps);
}


// given a point cloud, compute the centroid. Mostly useful for small patches, since centroid of the whole cloud is not too useful
//no selection vector provided, --> use all the points

Eigen::Vector3f computeCentroid(PointCloud<pcl::PointXYZ>::Ptr pcl_cloud) {
    Eigen::Vector3f centroid;
    centroid << 0, 0, 0;

    int size = pcl_cloud->width * pcl_cloud->height;
    std::cout << "frame: " << pcl_cloud->header.frame_id << std::endl;
    for (size_t i = 0; i != size; ++i) {
        centroid += pcl_cloud->points[i].getVector3fMap();
    }
    if (size > 0) {
        centroid /= ((float) size);
    }
    return centroid;
}

// second version: 
// will operate only on the points listed by index in iselect

Eigen::Vector3f computeCentroid(PointCloud<pcl::PointXYZ>::Ptr pcl_cloud, std::vector<int>iselect) {
    Eigen::Vector3f centroid;
    centroid << 0, 0, 0;
    int nselect = iselect.size();
    for (int i = 0; i < nselect; i++) {
        centroid += pcl_cloud->points[iselect[i]].getVector3fMap();
    }
    if (nselect > 0) {
        centroid /= ((float) nselect);
    }
    return centroid;
}


// compute the distance-squared of each point from the provided centroid
// presumably useful for outlier removal filtering

void computeRsqd(PointCloud<pcl::PointXYZ>::Ptr pcl_cloud, Eigen::Vector3f centroid, std::vector<float> &rsqd_vec) {
    Eigen::Vector3f evec3f;
    int npts = pcl_cloud->points.size();
    rsqd_vec.clear();
    rsqd_vec.resize(npts);
    for (int i = 0; i < npts; i++) {
        evec3f = pcl_cloud->points[i].getVector3fMap();
        evec3f -= centroid;
        rsqd_vec[i] = evec3f.dot(evec3f);
    }
}

// given an input cloud, rotate ALL points using matrix R_xform, and put the result in outputCloud
// This should be generalized for an affine transform (translation plus rotation)

void transform_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, Eigen::Matrix3f R_xform, PointCloud<pcl::PointXYZ>::Ptr outputCloud) {
    // copy over the header info from the inputCloud.
    // NOTE: copying the 
    outputCloud->header = inputCloud->header;
    outputCloud->is_dense = inputCloud->is_dense;
    outputCloud->width = inputCloud->width;
    outputCloud->height = inputCloud->height;
    int npts = inputCloud->points.size();
    cout << "transforming npts = " << npts << endl;
    outputCloud->points.resize(npts);
    Eigen::Vector3f pt;
    for (int i = 0; i < npts; ++i) {
        pt = R_xform * inputCloud->points[i].getVector3fMap();
        //cout<<"transformed pt: "<<pt.transpose()<<endl;
        outputCloud->points[i].getVector3fMap() = pt; //R_xform * inputCloud->points[i].getVector3fMap ();
    }
}

void transform_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, Eigen::Matrix3f R_xform, Eigen::Vector3f offset, PointCloud<pcl::PointXYZ>::Ptr outputCloud) {
    // copy over the header info from the inputCloud.
    // NOTE: copying the 
    outputCloud->header = inputCloud->header;
    outputCloud->is_dense = inputCloud->is_dense;
    outputCloud->width = inputCloud->width;
    outputCloud->height = inputCloud->height;
    int npts = inputCloud->points.size();
    cout << "transforming npts = " << npts << endl;
    outputCloud->points.resize(npts);
    Eigen::Vector3f pt;
    for (int i = 0; i < npts; ++i) {
        pt = R_xform * inputCloud->points[i].getVector3fMap() + offset; // + 
        //cout<<"transformed pt: "<<pt.transpose()<<endl;
        outputCloud->points[i].getVector3fMap() = pt; //R_xform * inputCloud->points[i].getVector3fMap ();
    }
}

//for this version, provide an affine transform:

void transform_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, Eigen::Affine3f A, PointCloud<pcl::PointXYZ>::Ptr outputCloud) {
    outputCloud->header = inputCloud->header;
    outputCloud->is_dense = inputCloud->is_dense;
    outputCloud->width = inputCloud->width;
    outputCloud->height = inputCloud->height;
    int npts = inputCloud->points.size();
    cout << "transforming npts = " << npts << endl;
    outputCloud->points.resize(npts);
    Eigen::Vector3f pt;
    for (int i = 0; i < npts; ++i) {
        outputCloud->points[i].getVector3fMap() = A * inputCloud->points[i].getVector3fMap(); // + 
        //cout<<"transformed pt: "<<pt.transpose()<<endl;
        //outputCloud->points[i].getVector3fMap() = pt; //R_xform * inputCloud->points[i].getVector3fMap ();
    }
}



//function to copy ALL of the cloud points

void copy_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, PointCloud<pcl::PointXYZ>::Ptr outputCloud) {
    int npts = inputCloud->points.size(); //how many points to extract?
    outputCloud->header = inputCloud->header;
    outputCloud->is_dense = inputCloud->is_dense;
    outputCloud->width = npts;
    outputCloud->height = 1;

    cout << "copying cloud w/ npts =" << npts << endl;
    outputCloud->points.resize(npts);
    for (int i = 0; i < npts; ++i) {
        outputCloud->points[i].getVector3fMap() = inputCloud->points[i].getVector3fMap();
    }
}

//make a new point cloud, extracted from inputCloud using only points listed  in "indices"

void copy_cloud(PointCloud<pcl::PointXYZ>::Ptr inputCloud, vector<int> &indices, PointCloud<pcl::PointXYZ>::Ptr outputCloud) {
    int npts = indices.size(); //how many points to extract?
    outputCloud->header = inputCloud->header;
    outputCloud->is_dense = inputCloud->is_dense;
    outputCloud->width = npts;
    outputCloud->height = 1;

    cout << "copying cloud w/ npts =" << npts << endl;
    outputCloud->points.resize(npts);
    for (int i = 0; i < npts; ++i) {
        outputCloud->points[i].getVector3fMap() = inputCloud->points[indices[i]].getVector3fMap();
    }
}

/* * */

//given a cloud, identify which points are within z_eps of z_nom; put these point indices in "indices"
// If the cloud has been rotated such that we expect a plane with normals (0,0,1) and offset z_nom,
// this function will find the points within tolerance z_eps of such a plane

void filter_cloud_z(PointCloud<pcl::PointXYZ>::Ptr inputCloud, double z_nom, double z_eps, vector<int> &indices) {
    int npts = inputCloud->points.size();
    Eigen::Vector3f pt;
    indices.clear();
    double dz;
    int ans;
    for (int i = 0; i < npts; ++i) {
        pt = inputCloud->points[i].getVector3fMap();
        //cout<<"pt: "<<pt.transpose()<<endl;
        dz = pt[2] - z_nom;
        if (fabs(dz) < z_eps) {
            indices.push_back(i);
            //cout<<"dz = "<<dz<<"; saving this point...enter 1 to continue: ";
            //cin>>ans;
        }
    }
    int n_extracted = indices.size();
    cout << " number of points in range = " << n_extracted << endl;
}

//given a cloud, identify which points are ABOVE z_threshold; put these point indices in "indices"
// this can be useful, e.g., for finding objects "on" a table

void filter_cloud_above_z(PointCloud<pcl::PointXYZ>::Ptr inputCloud, double z_threshold, vector<int> &indices) {
    int npts = inputCloud->points.size();
    Eigen::Vector3f pt;
    indices.clear();
    //double dz;
    int ans;
    for (int i = 0; i < npts; ++i) {
        pt = inputCloud->points[i].getVector3fMap();
        //cout<<"pt: "<<pt.transpose()<<endl;
        //dz = pt[2] - z_threshold;
        if (pt[2] > z_threshold) {
            indices.push_back(i);
            //cout<<"dz = "<<dz<<"; saving this point...enter 1 to continue: ";
            //cin>>ans;
        }
    }
    int n_extracted = indices.size();
    cout << " number of points extracted = " << n_extracted << endl;
}

// specify search box in x,y; presumably, input cloud is in base frame, and x and y range are
// set for nominal viewing area

void filter_cloud_above_z(PointCloud<pcl::PointXYZ>::Ptr inputCloud, double z_threshold, double x_min, double x_max,
        double y_min, double y_max, vector<int> &indices) {
    int npts = inputCloud->points.size();
    Eigen::Vector3f pt;
    indices.clear();
    //double dz;
    int ans;
    for (int i = 0; i < npts; ++i) {
        pt = inputCloud->points[i].getVector3fMap();
        //cout<<"pt: "<<pt.transpose()<<endl;
        //dz = pt[2] - z_threshold;
        if (pt[2] > z_threshold) {
            if ((pt[0] > x_min)&&(pt[0] < x_max)&&(pt[1] > y_min)&&(pt[1] < y_max)) {
                indices.push_back(i);
                //cout<<"dz = "<<dz<<"; saving this point...enter 1 to continue: ";
                //cin>>ans;
            }
        }
    }
    int n_extracted = indices.size();
    cout << " number of points extracted = " << n_extracted << endl;
}

float larger(float x1, float x2) {
    if (x1 > x2)
        return x1;
    else
        return x2;
}

float smaller(float x1, float x2) {
    if (x1 < x2)
        return x1;
    else
        return x2;
}

// bins start from x_min and have width del_x;
// find appropriate index, ix, for value x_val;
// ix starts from 0;

int int_bin(float x_min, float del_x, float x_val) {
    int ix;
    // if xmin<= x_val < x_min+del_x, set ix=0;
    ix = (x_val - x_min) / del_x;
    return ix;
}

void find_bounding_box(PointCloud<pcl::PointXYZ>::Ptr inputCloud, vector<int> &indices,
        float &x_min, float &x_max, float &y_min, float &y_max, float &z_min, float &z_max) {
    int npts = indices.size();
    Eigen::Vector3f pt;

    //init extrema:
    pt = inputCloud->points[indices[0]].getVector3fMap();
    cout << "first point: " << pt.transpose() << endl;
    ROS_INFO("first pt: %f, %f, %f", pt(0), pt(1), pt(2));
    x_min = pt(0);
    x_max = pt(0);
    y_min = pt(1);
    y_max = pt(1);
    z_min = pt(2);
    z_max = pt(2);
    ROS_INFO(" x_min, x_max, y_min, y_max, z_min, z_max = %f %f %f %f %f %f", x_min, x_max, y_min, y_max, z_min, z_max);

    for (int i = 1; i < npts; i++) {
        pt = inputCloud->points[indices[i]].getVector3fMap();
        //ROS_INFO(" pt %d: %f, %f, %f",i,pt(0),pt(1),pt(2));
        x_min = smaller(x_min, pt(0));
        x_max = larger(x_max, pt(0));
        y_min = smaller(y_min, pt(1));
        y_max = larger(y_max, pt(1));
        z_min = smaller(z_min, pt(2));
        z_max = larger(z_max, pt(2));
        //ROS_INFO(" x_min, x_max, y_min, y_max, z_min, z_max = %f %f %f %f %f %f",x_min,x_max,y_min,y_max,z_min,z_max);
    }
    ROS_INFO(" x_min, x_max, y_min, y_max, z_min, z_max = %f %f %f %f %f %f", x_min, x_max, y_min, y_max, z_min, z_max);

}

void sort_pts_by_horiz_planes(PointCloud<pcl::PointXYZ>::Ptr inputCloudWrtBase, double z_search_min, double z_search_max, double dz_slab,
        std::vector<vector<int> > &indices_by_slab, int &i_best_slab, vector<float> &z_mins, vector<float> &z_maxs) {

    int npts = inputCloudWrtBase->width * inputCloudWrtBase->height;
    Eigen::Vector3f pt;
    // sort points from z_min to z_max in slabs of thickness dz_slab
    // allow overlap of z_overlap to avoid boundary issues
    double z_overlap = 0.02; // choose this much overlap
    int nslabs = 1 + (z_search_max - z_search_min + z_overlap) / dz_slab;
    ROS_INFO("sorting points into %d slabs", nslabs);
    //vector<float> z_mins;
    //vector<float> z_maxs;
    z_mins.resize(nslabs);
    z_maxs.resize(nslabs);
    z_mins[0] = z_search_min;
    z_maxs[0] = z_search_min + dz_slab + z_overlap;
    for (int i = 1; i < nslabs; i++) {
        z_mins[i] = z_mins[i - 1] + dz_slab;
        z_maxs[i] = z_mins[i] + dz_slab + z_overlap;
    }
    //classify each pt by slab membership
    //vector<vector<int> > indices_by_slab; 
    indices_by_slab.clear();
    indices_by_slab.resize(nslabs);
    ROS_INFO("clearing out vector of vectors...");
    for (int i = 0; i < nslabs; i++) indices_by_slab[i].clear();
    double z_height;
    int i_slab;
    // given z-height, nominal slab number = floor((z_height-z_search_min)/dz_slab)
    // but also check above and below overlaps
    for (int ipt = 0; ipt < npts; ipt++) {
        pt = inputCloudWrtBase->points[ipt].getVector3fMap();
        //ROS_INFO(" pt %d: %f, %f, %f",ipt,pt(0),pt(1),pt(2));
        z_height = pt(2);
        if (z_height == z_height) { // watch out for nan?
            i_slab = floor((z_height - z_search_min) / dz_slab); //nom slab membership--but check above and below as well
            indices_by_slab[i_slab].push_back(ipt);
            if (i_slab < nslabs - 2) { // not the top slab--so look above
                if (z_height > z_mins[i_slab + 1]) {
                    //pt also fits in overlap w/ next higher slab:
                    indices_by_slab[i_slab + 1].push_back(ipt);
                }
            }
            //check below:
            if (i_slab > 0) { // not the bottom slab
                if (z_height < z_maxs[i_slab - 1]) {
                    //pt also fits in overlap w/ next lower slab:
                    indices_by_slab[i_slab - 1].push_back(ipt);
                }
            }
        } else {
            //ROS_INFO("pt %d: Nan!", ipt);  //got LOTS of NaN's; (pts > 260,000); don't know why
        }
    } // done sorting points by slabs
    // find the slab with the most points:
    i_best_slab = 0;
    int npts_islab = indices_by_slab[i_best_slab].size();
    int npts_best_slab = npts_islab;


    ROS_INFO("npts slab %d = %d", i_best_slab, npts_best_slab);
    for (int islab = 1; islab < nslabs; islab++) {
        npts_islab = indices_by_slab[islab].size();
        ROS_INFO("npts slab %d = %d", islab, npts_islab);
        if (npts_islab > npts_best_slab) {
            npts_best_slab = npts_islab;
            i_best_slab = islab;
        }
    }
    ROS_INFO("best slab num = %d with %d pts", i_best_slab, npts_best_slab);
}

// variant--operate only on listed points from pointcloud

void sort_pts_by_horiz_planes(PointCloud<pcl::PointXYZ>::Ptr inputCloudWrtBase, vector<int> indices, double z_search_min, double z_search_max, double dz_slab,
        std::vector<vector<int> > &indices_by_slab, int &i_best_slab, vector<float> &z_mins, vector<float> &z_maxs) {
    int npts = indices.size(); //inputCloudWrtBase->width * inputCloudWrtBase->height;
    Eigen::Vector3f pt;
    // sort points from z_min to z_max in slabs of thickness dz_slab
    // allow overlap of z_overlap to avoid boundary issues
    double z_overlap = 0.001; // choose this much overlap
    int nslabs = 1 + (z_search_max - z_search_min + z_overlap) / dz_slab;
    ROS_INFO("sorting points into %d slabs", nslabs);
    //vector<float> z_mins;
    //vector<float> z_maxs;
    z_mins.resize(nslabs);
    z_maxs.resize(nslabs);
    z_mins[0] = z_search_min;
    z_maxs[0] = z_search_min + dz_slab + z_overlap;
    for (int i = 1; i < nslabs; i++) {
        z_mins[i] = z_mins[i - 1] + dz_slab;
        z_maxs[i] = z_mins[i] + dz_slab + z_overlap;
    }
    //classify each pt by slab membership
    //vector<vector<int> > indices_by_slab; 
    indices_by_slab.clear();
    indices_by_slab.resize(nslabs);
    ROS_INFO("clearing out vector of vectors...");
    for (int i = 0; i < nslabs; i++) indices_by_slab[i].clear();
    double z_height;
    int i_slab;
    // given z-height, nominal slab number = floor((z_height-z_search_min)/dz_slab)
    // but also check above and below overlaps
    int sel_pt;
    for (int ipt = 0; ipt < npts; ipt++) {
        sel_pt = indices[ipt];
        pt = inputCloudWrtBase->points[sel_pt].getVector3fMap();
        //ROS_INFO(" pt %d: %f, %f, %f",ipt,pt(0),pt(1),pt(2));
        z_height = pt(2);
        if (z_height == z_height) { // watch out for nan?
            i_slab = floor((z_height - z_search_min) / dz_slab); //nom slab membership--but check above and below as well
            indices_by_slab[i_slab].push_back(sel_pt);
            if (i_slab < nslabs - 2) { // not the top slab--so look above
                if (z_height > z_mins[i_slab + 1]) {
                    //pt also fits in overlap w/ next higher slab:
                    indices_by_slab[i_slab + 1].push_back(sel_pt);
                }
            }
            //check below:
            if (i_slab > 0) { // not the bottom slab
                if (z_height < z_maxs[i_slab - 1]) {
                    //pt also fits in overlap w/ next lower slab:
                    indices_by_slab[i_slab - 1].push_back(sel_pt);
                }
            }
        } else {
            //ROS_INFO("pt %d: Nan!", ipt);  //got LOTS of NaN's; (pts > 260,000); don't know why
        }
    } // done sorting points by slabs
    // find the slab with the most points:
    i_best_slab = 0;
    int npts_islab = indices_by_slab[i_best_slab].size();
    int npts_best_slab = npts_islab;


    ROS_INFO("npts slab %d = %d", i_best_slab, npts_best_slab);
    for (int islab = 1; islab < nslabs; islab++) {
        npts_islab = indices_by_slab[islab].size();
        ROS_INFO("npts slab %d = %d", islab, npts_islab);
        if (npts_islab > npts_best_slab) {
            npts_best_slab = npts_islab;
            i_best_slab = islab;
        }
    }
    ROS_INFO("best slab num = %d with %d pts", i_best_slab, npts_best_slab);
}

//compute a depth map, as viewed from above an object
// provide x and y range; make sure nsize is the array size

void compute_depth_map_z(PointCloud<pcl::PointXYZ>::Ptr inputCloud, vector<int> &indices,
        float x_min, float x_max, float y_min, float y_max,
        int silhouette_z[silhouette_size][silhouette_size], float depth_z[silhouette_size][silhouette_size],
        int &ix_centroid, int &jy_centroid) {
    float del_x, del_y;
    int bitmap[silhouette_size][silhouette_size];

    for (int ix = 0; ix < silhouette_size; ix++)
        for (int jy = 0; jy < silhouette_size; jy++) {
            silhouette_z[ix][jy] = 0;
            depth_z[ix][jy] = 0;
            bitmap[ix][jy] = 0;
        }
    int npts = indices.size();
    Eigen::Vector3f pt;
    int ix, jy;
    del_x = (g_x_view_max - g_x_view_min) / silhouette_size;
    del_y = (g_y_view_max - g_y_view_min) / silhouette_size;
    int ix_min, ix_max, jy_min, jy_max; // convert dimensions of bounding box to indices
    ix_min = int_bin(g_x_view_min, del_x, x_min);
    ix_max = int_bin(g_x_view_min, del_x, x_max);
    jy_min = int_bin(g_y_view_min, del_y, y_min);
    jy_max = int_bin(g_y_view_min, del_y, y_max);


    for (int i = 0; i < npts; i++) {
        pt = inputCloud->points[indices[i]].getVector3fMap();
        ix = int_bin(g_x_view_min, del_x, pt[0]);
        jy = int_bin(g_y_view_min, del_y, pt[1]);
        silhouette_z[ix][jy]++;
        depth_z[ix][jy] += pt[2] - g_table_z_wrt_base; // this is z w/rt table height
        bitmap[ix][jy] = 1;
    }
    //compute avg z-height and centroid in ix,jy coords
    ix_centroid = 0;
    jy_centroid = 0;
    int n_bins_on = 0; //how many of the pixels in the top-down silhouette are occupied?
    for (int ix = 0; ix < silhouette_size; ix++)
        for (int jy = 0; jy < silhouette_size; jy++) {
            if (silhouette_z[ix][jy]) {
                ix_centroid += ix;
                jy_centroid += jy;
                n_bins_on++;
                depth_z[ix][jy] /= silhouette_z[ix][jy]; // compute avg depth for each nonzero cell
            }
        }
    // these values should be useful--should return them
    ix_centroid /= n_bins_on;
    jy_centroid /= n_bins_on;
    ROS_INFO("centroid ix,jy = %d, %d", ix_centroid, jy_centroid);
    ROS_INFO("bits on = %d", n_bins_on);
    ROS_INFO("indices of bounding box: xmin, xmax, ymin, ymax = %d, %d, %d, %d",
            ix_min, ix_max, jy_min, jy_max);
}

//given silhouette = array of ints, with object centroid at ix_centroid,jy_centroid,
// shift the contents of silhouette_z such that ix_centroid is at g_ictr and iy_centroid is g_jctr

bool recenter_silhouette(int silhouette_z[silhouette_size][silhouette_size], int ix_centroid, int jy_centroid) {
    const int ictr = floor(silhouette_size / 2);
    const int jctr = floor(silhouette_size / 2);
    int silhouette_z_copy[silhouette_size][silhouette_size];
    for (int i = 0; i < silhouette_size; i++)
        for (int j = 0; j < silhouette_size; j++) {
            silhouette_z_copy[i][j] = silhouette_z[i][j];
            silhouette_z[i][j] = 0;
        }
    int i_shifted; // = ictr-ix_centroid;
    int j_shifted; // = jctr-jy_centroid);
    for (int i = 0; i < silhouette_size; i++) {
        i_shifted = i + ictr - ix_centroid;
        if ((i_shifted >= 0)&&(i_shifted < silhouette_size)) { // here if i_shifted is in range

            for (int j = 0; j < silhouette_size; j++) {
                j_shifted = j + jctr - jy_centroid;
                if ((j_shifted >= 0)&&(j_shifted < silhouette_size)) {
                    silhouette_z[i_shifted][j_shifted] = silhouette_z_copy[i][j];
                }
            }
        }
    }

}

//given silhouette = array of ints, with object centroid at ix_centroid,jy_centroid,
// shift the contents of silhouette_z such that ix_centroid is at g_ictr and iy_centroid is g_jctr

bool recenter_silhouette_and_depth_map_z(int silhouette_z[silhouette_size][silhouette_size], float depth_z[silhouette_size][silhouette_size],
        int ix_centroid, int jy_centroid) {
    const int ictr = floor(silhouette_size / 2);
    const int jctr = floor(silhouette_size / 2);
    int silhouette_z_copy[silhouette_size][silhouette_size];
    float depth_z_copy[silhouette_size][silhouette_size];
    for (int i = 0; i < silhouette_size; i++)
        for (int j = 0; j < silhouette_size; j++) {
            silhouette_z_copy[i][j] = silhouette_z[i][j];
            silhouette_z[i][j] = 0;
            depth_z_copy[i][j] = depth_z[i][j];
            depth_z[i][j] = 0.0;
        }
    int i_shifted; // = ictr-ix_centroid;
    int j_shifted; // = jctr-jy_centroid);
    for (int i = 0; i < silhouette_size; i++) {
        i_shifted = i + ictr - ix_centroid;
        if ((i_shifted >= 0)&&(i_shifted < silhouette_size)) { // here if i_shifted is in range

            for (int j = 0; j < silhouette_size; j++) {
                j_shifted = j + jctr - jy_centroid;
                if ((j_shifted >= 0)&&(j_shifted < silhouette_size)) {
                    silhouette_z[i_shifted][j_shifted] = silhouette_z_copy[i][j];
                    depth_z[i_shifted][j_shifted] = depth_z_copy[i][j];
                }
            }
        }
    }

}

int compute_silhouette_centroid(int silhouette_z[silhouette_size][silhouette_size], int &ix_centroid, int &jy_centroid) {
    ix_centroid = 0;
    jy_centroid = 0;
    int n_bins_on = 0; //how many of the pixels in the top-down silhouette are occupied?
    for (int ix = 0; ix < silhouette_size; ix++)
        for (int jy = 0; jy < silhouette_size; jy++) {
            if (silhouette_z[ix][jy]) {
                ix_centroid += ix;
                jy_centroid += jy;
                n_bins_on++;
            }
        }
    if (n_bins_on == 0) {
        return n_bins_on;
    }

    ix_centroid /= n_bins_on;
    jy_centroid /= n_bins_on;
    ROS_INFO("centroid ix,jy = %d, %d", ix_centroid, jy_centroid);
    ROS_INFO("number of populated cells in = %d", n_bins_on);
}

//given silhouette and dept_z matrices, find centroid; repopulate arrays w/ centroid in center of range
/*
int recenter_depth_z(silhouette_z[silhouette_size][silhouette_size], float depth_z[silhouette_size][silhouette_size], int &ix_centroid,int &jy_centroid) {
    //compute the centroid in ix,jy coords
    ix_centroid = 0;
    jy_centroid = 0;
    int n_bins_on = 0; //how many of the pixels in the top-down silhouette are occupied?
    for (int ix = 0; ix < silhouette_size; ix++)
        for (int jy = 0; jy < silhouette_size; jy++) {
            if (silhouette_z[ix][jy]) {
                ix_centroid += ix;
                jy_centroid += jy;
                n_bins_on++;
                depth_z[ix][jy] /= silhouette_z[ix][jy]; // compute avg depth for each nonzero cell
            }
            else {
                depth_z[ix][jy] = 0.0;
            }
        }
    if (n_bins_on==0) {
        return n_bins_on;
    }
    // these values should be useful--should return them
    ix_centroid /= n_bins_on;
    jy_centroid /= n_bins_on;
    ROS_INFO("centroid ix,jy = %d, %d", ix_centroid, jy_centroid);
    ROS_INFO("number of populated cells in top-down projection = %d", n_bins_on);
    
}
 */
//operates on g_cloud_wrt_base using g_table_z_wrt_base
//pcl::PointCloud<pcl::PointXYZ>::Ptr g_object_cloud(new pcl::PointCloud<pcl::PointXYZ>); // this cloud gets published--viewable in rviz
//int g_silhouette_z[silhouette_size][silhouette_size];
//float g_depth_z[silhouette_size][silhouette_size];

bool sample_object() {
    int silhouette_z[silhouette_size][silhouette_size];
    float depth_z[silhouette_size][silhouette_size];
    int ix_centroid, jy_centroid;
    Eigen::Vector3f pt;
    std::vector<int> indices_pts_above_plane;
    float x_min, x_max, y_min, y_max, z_min, z_max;

    ROS_INFO("performing new model sampling");
    ROS_INFO("filtering for points above identified plane");
    // w/ affine transform, z-coord of points on plane (in plane frame) should be ~0
    double z_threshold = 0.0 + Z_EPS + g_table_z_wrt_base; //g_plane_params[3] + Z_EPS;

    for (int num_snapshots = 0; num_snapshots < g_num_snapshots_avg; num_snapshots++) {
        g_take_snapshot = true;
        while (g_take_snapshot) {
                        ros::spinOnce();
        }
        ROS_INFO("got snapshot.");
        ROS_INFO("filtering for points above %f ", z_threshold);

        //this version of filter_cloud_above_z considers only points within specified x,y bounds
        filter_cloud_above_z(g_cloud_wrt_base, z_threshold, g_x_view_min, g_x_view_max, g_y_view_min, g_y_view_max, indices_pts_above_plane);
        if (indices_pts_above_plane.size() == 0) {
            ROS_WARN("no points found within filter volume!");
            return false;
        }
        //extract these points--but in original, non-rotated frame, and display them
        copy_cloud(g_pclKinect, indices_pts_above_plane, g_display_cloud);
        pcl::toROSMsg(*g_display_cloud, *g_pcl2_display_cloud);
        g_pcl2_display_cloud->header.stamp = ros::Time::now(); //update the time stamp, so rviz does not complain        
        g_pubCloud.publish(g_pcl2_display_cloud); //and also display whatever we choose to put in here
        ros::spinOnce();
        ROS_INFO("finding bounding box");
        //cout<<"finding bounding box; enter 1: ";
        //int ans;
        //cin>>ans;
        int silhouette_z[silhouette_size][silhouette_size];
        float depth_z[silhouette_size][silhouette_size];
        //transform_cloud(g_pclKinect, g_affine_kinect_to_base,  g_cloud_wrt_base);
        //find_bounding_box(g_cloud_transformed, indices_pts_above_plane,x_min,x_max,y_min,y_max,z_min,z_max);
        find_bounding_box(g_cloud_wrt_base, indices_pts_above_plane, x_min, x_max, y_min, y_max, z_min, z_max);

        compute_depth_map_z(g_cloud_wrt_base, indices_pts_above_plane, x_min, x_max, y_min, y_max,
                silhouette_z, depth_z, ix_centroid, jy_centroid);
        //display_silhouette_z(silhouette_z);
        ROS_INFO("bounds: x_min, x_max, y_min, y_max = %f, %f %f, %f", x_min, x_max, y_min, y_max);
        ROS_INFO("centroid indices: %d, %d", ix_centroid, jy_centroid);
        //cout << "enter 1: ";

        //cin>>ans;
        //success= recenter_silhouette(silhouette_z,ix_centroid,jy_centroid);
        bool success = recenter_silhouette_and_depth_map_z(silhouette_z, depth_z, ix_centroid, jy_centroid);

        //debug: 
        ROS_INFO("recentered silhouette: ");
        display_silhouette_z(silhouette_z);
        compute_silhouette_centroid(silhouette_z, ix_centroid, jy_centroid);
        // now have silhouette_z and depth_z from a snapshot; want to average these for multiple snapshots
        // to reduce noise, let's eliminate ALL points for which any one snapshot has zero signal...conservative min model?? 
        // may want to change this
        //        
        //for first snapshot, initialize cumulative arrays:
        if (num_snapshots == 0) {
            for (int i = 0; i < silhouette_size; i++)
                for (int j = 0; j < silhouette_size; j++) {
                    g_silhouette_z[i][j] = silhouette_z[i][j];
                    g_depth_z[i][j] = depth_z[i][j];
                }
        } else {
            // let's test each value and if present, average in depth_z;
            for (int i = 0; i < silhouette_size; i++) {
                for (int j = 0; j < silhouette_size; j++) {
                    if (g_silhouette_z[i][j] && silhouette_z[i][j]) {
                        g_depth_z[i][j] += depth_z[i][j];
                        g_silhouette_z[i][j] += silhouette_z[i][j];
                    } else {
                        g_silhouette_z[i][j] = 0; // suppress this cell
                    }

                }
            }
        }
        ros::spinOnce();
        //DEBUG
        ROS_INFO("snapshot number %d",num_snapshots);
        //cout<<"enter 1: ";
        //1cin>>g_ans;
    }

    ROS_INFO("computing avg properties");
       for (int i = 0; i < silhouette_size; i++) {
           for (int j = 0; j < silhouette_size; j++) {
                        g_depth_z[i][j] /= g_num_snapshots_avg;
                        g_silhouette_z[i][j] /= g_num_snapshots_avg;
                    } 

                }
    //recenter the averaged result:
    recenter_silhouette_and_depth_map_z(g_silhouette_z, g_depth_z, ix_centroid, jy_centroid);
    
    display_silhouette_z(g_silhouette_z);
    ROS_INFO("done w/ sampling/averaging object");
    /* DEBUG: display z-heights for non-empty cells 
             for (int i = 0; i < silhouette_size; i++) {
                for (int j = 0; j < silhouette_size; j++) {
                    if (g_silhouette_z[i][j]) {   
                        cout<<g_depth_z[i][j]<<",";
                    }
                }
             cout<<endl;
             }
    */

    return true;
}

void display_silhouette_z(int silhouette_z[silhouette_size][silhouette_size]) {
    int npts;
    for (int ix = 0; ix < silhouette_size; ix++) {
        for (int jy = 0; jy < silhouette_size; jy++) {
            npts = silhouette_z[ix][jy];
            if (npts) cout << "1";
            else cout << "0";

        }
        cout << endl;
    }
}


// this is a pretty specific function, but useful for illustrating how to create a point cloud that
// can be visualized in rviz.
// create a cloud to visualize a can of radius r, height h.
// assume origin of bottom of can is 0,0,0, and z-axis points up;
// resulting cloud of can model is contained both in arg (canCloud) and g_canCloud

void make_can_cloud(PointCloud<pcl::PointXYZ>::Ptr canCloud, double r_can, double h_can) {
    double theta, h;
    Eigen::Vector3f pt;
    int npts = 0;
    //ROS_INFO("making can cloud");
    //count the points:
    for (theta = 0; theta < 2.0 * M_PI; theta += 0.3)
        for (h = 0; h < h_can; h += 0.01)
            npts++;
    canCloud->points.resize(npts);

    int i = 0;
    for (theta = 0; theta < 2.0 * M_PI; theta += 0.3)
        for (h = 0; h < h_can; h += 0.01) {

            pt[0] = r_can * cos(theta);
            pt[1] = r_can * sin(theta);
            pt[2] = h;
            canCloud->points[i].getVector3fMap() = pt;
            i++;
        }
    // use camera_rgb_optical_frame for physical Kinect
    canCloud->header.frame_id = "camera_rgb_optical_frame"; //kinect_pc_frame"; //base"; //vs /base_link?
    //canCloud->header.stamp = ros::Time::now();
    canCloud->is_dense = true;
    canCloud->width = npts;
    canCloud->height = 1;

    copy_cloud(canCloud, g_canCloud);
    //ROS_INFO("done making/copying canCloud");
    //optionally, rotate the cylinder to make its axis parallel to the most recently defined normal axis
    //transform_cloud(g_canCloud, g_R_transform, canCloud);    
}

//COMPUTE_RADIAL_ERROR:  NOT COMPLETE; RUNS, BUT HAS NOT BEEN TESTED, SO PROBABLY HAS BUGS
//try to fit a cylinder to points in inputCloud, assuming cylinder axis is vertical (0,0,1).  
//given radius of model and center coords, cx, cy, return the fit error and derivatives dE/dCx and dE/dCy
// eliminate points that are too far from expectation
// define the error as: r_i = sqrt(c_vec - p_i); err_i = (r_i - r)^2; E = sum_i err_i
//inputCloud should be rotated appropriately, so expected cylinder has major axis along z

void compute_radial_error(PointCloud<pcl::PointXYZ>::Ptr inputCloud, std::vector<int> indices, double r, Eigen::Vector3f center, double &E, double &dEdCx, double &dEdCy) {
    std::vector<int> copy_indices;
    int npts = indices.size();
    copy_indices.resize(npts);

    cout << " number of initial points = " << npts << endl;
    for (int i = 0; i < npts; i++) copy_indices[i] = indices[i];
    indices.clear(); // we will re-populate this with inliers
    double sum_sqd_err = 0.0;
    double r_i, err_i, r_sqd_err;
    dEdCx = 0.0;
    dEdCy = 0.0;
    E = 0.0;
    double dx, dy;
    Eigen::Vector3f pt;
    //indices.clear();
    //double dz;
    int ans;
    for (int i = 0; i < npts; ++i) {
        pt = inputCloud->points[copy_indices[i]].getVector3fMap();
        dx = center[0] - pt[0];
        dy = center[1] - pt[1];
        r_i = sqrt(dx * dx + dy * dy);
        r_sqd_err = (r_i - r)*(r_i - r);
        err_i = sqrt(r_sqd_err);
        if (err_i < R_EPS) {
            // include this point as an inlier:
            indices.push_back(copy_indices[i]);
            sum_sqd_err += r_sqd_err;
            dEdCx += (r_i - r)*(dx) / r_i;
            dEdCy += (r_i - r)*(dy) / r_i;
        }
    }
    int n_extracted = indices.size();
    cout << " number of inliers = " << n_extracted << endl;
    E = 0.5 * sum_sqd_err / n_extracted;
    dEdCx /= n_extracted;
    dEdCy /= n_extracted;

    cout << "rms radial error: " << sqrt(2.0 * E) << endl;
    cout << "E = " << E << endl;
    cout << "dE/dCx = " << dEdCx << "; dEdCy = " << dEdCy << endl;
}
/**/

//use this service to set processing modes interactively

bool modeService(cwru_srv::simple_int_service_messageRequest& request, cwru_srv::simple_int_service_messageResponse& response) {
    ROS_INFO("mode select service callback activated w mode %d", request.req);
    if (request.req != PCL_INQUIRE_STATUS) {
        g_status = PCL_STATUS_BUSY;
        g_pcl_process_mode = request.req;
        //ROS_INFO("mode select service callback activated w mode %d", g_pcl_process_mode);
        g_trigger = true; //signal that we received a request; trigger a response
        //cout << "Mode set to: " << g_pcl_process_mode << endl;        
    } else {
        if (g_status == PCL_STATUS_BUSY)
            ROS_INFO("PCL_STATUS_BUSY");
        else
            ROS_INFO("PCL_STATUS_IDLE");
    }
    response.resp = g_status; // return busy/idle status
    return true;
}

bool getFrameService(cwru_srv::IM_node_service_messageRequest& request, cwru_srv::IM_node_service_messageResponse& response) {
    ROS_INFO("pcl_perception_node: received request for frame");
    geometry_msgs::Pose pose;
    geometry_msgs::PoseStamped poseStamped;
    //if pay attention to input codes, could choose what frame to return
    // by default, return the frame of the model (can)
    switch (request.cmd_mode) {
        case PCL_FRAME_SVC_GET_GRASP_FRAME_WRT_MODEL:
            ROS_INFO("requesting grasp pose w/rt model");
            pose = transformEigenAffine3fToPose(g_A_grasp_wrt_model);
            ROS_INFO("grasp pose origin: %f, %f, %f", pose.position.x, pose.position.y, pose.position.z);
            ROS_INFO("grasp pose quat: %f, %f, %f, %f", pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w);

            poseStamped.pose = pose;
            poseStamped.header.stamp = ros::Time().now();
            poseStamped.header.frame_id = "model"; // this frame is unknown to rviz...no transform to base (yet)
            response.poseStamped_IM_current = poseStamped;
            break;

        case PCL_FRAME_SVC_GET_GRASP_FRAME_WRT_TORSO:
            ROS_INFO("requesting grasp pose w/rt torso");
            pose = transformEigenAffine3fToPose(g_A_grasp_wrt_base);
            ROS_INFO("grasp pose origin: %f, %f, %f", pose.position.x, pose.position.y, pose.position.z);
            ROS_INFO("grasp pose quat: %f, %f, %f, %f", pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w);

            poseStamped.pose = pose;
            poseStamped.header.stamp = ros::Time().now();
            poseStamped.header.frame_id = "torso"; // this frame is unknown to rviz...no transform to base (yet)
            response.poseStamped_IM_current = poseStamped;
            break;

        case PCL_FRAME_SVC_GET_MODEL_FRAME_WRT_TORSO:
            ROS_INFO("requesting model pose w/rt torso");
            pose = transformEigenAffine3fToPose(g_A_model_wrt_base);
            ROS_INFO("model pose origin: %f, %f, %f", pose.position.x, pose.position.y, pose.position.z);
            ROS_INFO("model pose quat: %f, %f, %f, %f", pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w);

            poseStamped.pose = pose;
            poseStamped.header.stamp = ros::Time().now();
            poseStamped.header.frame_id = "torso"; // this mode assumes affine is w/rt torso
            response.poseStamped_IM_current = poseStamped;
            break;

        case PCL_FRAME_SVC_GET_MODEL_FRAME_WRT_KINECT:
            ROS_INFO("requesting model pose w/rt kinect");
            pose = transformEigenAffine3fToPose(g_A_model_wrt_torso);

            //cout << "model frame origin: " << g_A_model.translation().transpose() << endl;
            //cout << "model frame orientation: " << endl;
            //cout << g_A_model.linear() << endl;

            poseStamped.pose = pose;
            poseStamped.header.stamp = ros::Time().now();
            poseStamped.header.frame_id = "camera_rgb_optical_frame"; // had to change topic for physical kinect; kinect_pc_frame";
            // need to convert this frame to a poseStamped and put into response
            response.poseStamped_IM_current = poseStamped;
            break;
        default:
            ROS_WARN("frame service mode not recognized");
            return false;
            break;
    }

    return true;
}


// this callback wakes up when a new "selected Points" message arrives

void selectCB(const sensor_msgs::PointCloud2ConstPtr& cloud) {
    pcl::PointCloud<pcl::PointXYZ>::Ptr pclKinect(new PointCloud<pcl::PointXYZ>);
    pcl::fromROSMsg(*cloud, *pclKinect);
    g_pclSelect = pclKinect;
    //pcl::fromROSMsg(*cloud, *g_pclSelect);
    ROS_INFO("RECEIVED NEW PATCH w/  %d * %d points", g_pclSelect->width, g_pclSelect->height);
    //ROS_INFO("frame id is: %s",cloud->header.frame_id);
    ROS_INFO("setting frame_id to camera_rgb_optical_frame");
    g_pclSelect->header.frame_id = "camera_rgb_optical_frame";
    //cout << "header frame: " << cloud->header.frame_id << endl;
    int npts = g_pclSelect->width * g_pclSelect->height;
    std::vector<int> iselect_filtered; //indices of patch that do not contain outliers

    //find centroid and plane params
    process_patch(iselect_filtered, g_patch_centroid, g_plane_params); // operate on selected points to remove outliers and

    g_processed_patch = true; // update our states to note that we have processed a patch, and thus have valid plane info
}

// this callback wakes up when a new kinect pointcloud arrives

void kinectCB(const sensor_msgs::PointCloud2ConstPtr& cloud) {
    pcl::PointCloud<pcl::PointXYZ>::Ptr pclKinect(new PointCloud<pcl::PointXYZ>);
    pcl::fromROSMsg(*cloud, *pclKinect);

    //pcl::fromROSMsg(*cloud, *g_pclKinect);
    if (g_take_snapshot) {
        g_pclKinect = pclKinect;
        ROS_INFO("RECEIVED NEW KINECT POINTCLOUD w/  %d * %d points", g_pclKinect->width, g_pclKinect->height);
        //ROS_INFO("frame id is: %s",cloud->header.frame_id);
        cout << "header frame: " << g_pclKinect->header.frame_id << endl;
        int npts = g_pclKinect->width * g_pclKinect->height;
        ROS_INFO("npts = %d", npts);
        cout << "transforming new kinect cloud into base coords" << endl;
        transform_cloud(g_pclKinect, g_affine_kinect_to_base.inverse(), g_cloud_wrt_base);
        npts = g_cloud_wrt_base->width * g_cloud_wrt_base->height;
        ROS_INFO("npts transformed = %d", npts);

    }
    g_take_snapshot = false; //don't do this again until requested
    g_status = PCL_STATUS_IDLE;

}

int main(int argc, char** argv) {
    // Do some initialization here
    ros::init(argc, argv, "pcl_perception_node");
    ros::NodeHandle nh;
    ros::Rate rate(2);
    int ans;
    float x_min, x_max, y_min, y_max, z_min, z_max;

    tf::TransformListener tfListener;
    bool tferr = true;
    ROS_INFO("waiting for tf between kinect sensor and base...");
    //PROBLEM: kinect driver on remote computer out of sync w/ master;
    // try to fix this by finding separate transform for kinect_sensor to head (static), then head to torso
    while (tferr) {
        tferr = false;
        try {
            ros::spinOnce();
            //try to lookup transform from target frame "base" to source frame "kinect_pc_frame"
            //The direction of the transform returned will be from the target_frame to the source_frame. 
            //Which if applied to data, will transform data in the source_frame into the target_frame. See tf/CoordinateFrameConventions#Transform_Direction
            // this version for physical Kinect
            tfListener.lookupTransform("head", "torso", ros::Time(0), g_head_to_base_link);
            tfListener.lookupTransform("camera_rgb_optical_frame", "head", ros::Time(0), g_kinect_sensor_to_head_link);
            //g_head_to_base_link,g_kinect_sensor_to_head_link;
            //tfListener.lookupTransform("camera_rgb_optical_frame", "base", ros::Time(0), g_kinect_sensor_to_base_link);
            //g_kinect_sensor_to_base_link = g_kinect_sensor_to_head_link*g_kinect_head_to_base_link;


            //tfListener.lookupTransform("kinect_pc_frame", "base", ros::Time(0), g_kinect_sensor_to_base_link);
        } catch (tf::TransformException &exception) {
            ROS_WARN("%s", exception.what());
            tferr = true;
            ros::Duration(1).sleep(); // sleep for half a second
            ros::spinOnce();
        }
    }
    ROS_INFO("tf is good");
    // g_tf_kinect_sensor_to_base_link.setOrigin(tf::Vector3(g_kinect_sensor_to_base_link.pose.position.x, g_kinect_sensor_to_base_link.pose.position.y, g_kinect_sensor_to_base_link.pose.position.z));
    // extract transform from stamped transform:
    // for some reason, Kinect transforms are out of sync;
    // try to fix this by finding a static transform from sensor to head, but let head to torso by time varying
    g_tf_kinect_sensor_to_head_link.setOrigin(g_kinect_sensor_to_head_link.getOrigin());
    g_tf_kinect_sensor_to_head_link.setRotation(g_kinect_sensor_to_head_link.getRotation());
    transformTFToEigen(g_tf_kinect_sensor_to_head_link, g_affine_kinect_to_head);

    g_tf_head_to_base_link.setOrigin(g_head_to_base_link.getOrigin());
    g_tf_head_to_base_link.setRotation(g_head_to_base_link.getRotation());
    transformTFToEigen(g_tf_head_to_base_link, g_affine_head_to_base);

    // had to cascade these in the following order...then use inverse of affine_kinect_to_base to transform pt into base frame
    g_affine_kinect_to_base = g_affine_kinect_to_head*g_affine_head_to_base; //g_affine_head_to_base*g_affine_kinect_to_head;
    //g_tf_kinect_sensor_to_base_link.setOrigin(g_kinect_sensor_to_base_link.getOrigin());
    //g_tf_kinect_sensor_to_base_link.setRotation(g_kinect_sensor_to_base_link.getRotation());
    //transformTFToEigen(g_tf_kinect_sensor_to_base_link, g_affine_kinect_to_base);    
    // Subscribers
    cout << "subscribing to kinect depth points" << endl;
    // Subscribers
    // use the following, if have "live" streaming from a Kinect
    ros::Subscriber getPCLPoints = nh.subscribe<sensor_msgs::PointCloud2> ("/camera/depth_registered/points", 1, kinectCB);
    ros::Subscriber getPCLPoints_sim = nh.subscribe<sensor_msgs::PointCloud2> ("/kinect/depth/points", 1, kinectCB); // this for gazebo

    cout << "subscribing to selected points" << endl;

    // subscribe to "selected_points", which is published by Rviz tool
    ros::Subscriber selectedPoints = nh.subscribe<sensor_msgs::PointCloud2> ("/selected_points", 1, selectCB);

    // have rviz display both of these topics
    g_pubCloud = nh.advertise<sensor_msgs::PointCloud2> ("/pcl_perception_display", 1);
    ros::Publisher pubModel = nh.advertise<sensor_msgs::PointCloud2> ("/model_display", 1);
    ros::Publisher pubPcdCloud = nh.advertise<sensor_msgs::PointCloud2> ("/kinect_pointcloud", 1);

    // service used to interactively change processing modes
    ros::ServiceServer service = nh.advertiseService("pcl_perception_svc", modeService);
    ros::ServiceServer frame_service = nh.advertiseService("pcl_getframe_svc", getFrameService);
    std::vector<int> indices_pts_above_plane;

    // init display cloud header--just so Rviz will be happy until a genuine display cloud is populated
    g_display_cloud->header = g_pclKinect->header;
    /*
        if (pcl::io::loadPCDFile<pcl::PointXYZ> ("test_pcd.pcd", *g_cloud_from_disk) == -1) //* load the file
        {
            PCL_ERROR("Couldn't read file test_pcd.pcd \n");
            return (-1);
        }
        std::cout << "Loaded "
                << g_cloud_from_disk->width * g_cloud_from_disk->height
                << " data points from test_pcd.pcd  " << std::endl;

        g_cloud_from_disk->header.frame_id = "world"; //kinect_pc_frame"; // why is this necessary?  Did PCD delete the reference frame id?
     */

    Object_finder object_finder;
    double z_threshold = 0.0;
    double E;
    double dEdCx = 0.0;
    double dEdCy = 0.0;
    double dEnorm;
    float cx, cy;

    // init values in g_A_model_wrt_base to test service
    Eigen::Matrix3f Rmodel;
    Rmodel << 1, 0, 0,
            0, 1, 0,
            0, 0, 1;

    Eigen::Vector3f can_center_wrt_plane;
    can_center_wrt_plane << 2, 1, 0;
    g_A_model_wrt_base.linear() = Rmodel;
    g_A_model_wrt_base.translation() = can_center_wrt_plane;
    Eigen::Vector3f height_delta_wrt_sensor_frame;
    Eigen::Affine3f A_plane_to_sensor;
    //std::vector<Eigen::Affine3d> grasp_xforms;
    Eigen::Vector3d model_frame_origin;
    Eigen::Matrix3d model_frame_R;
    Eigen::Affine3d affine_model_frame_wrt_baxter_base;
    Eigen::Vector3f grasp_origin_wrt_model;
    int n_grasp_frames;
    //cout<<"enter 1 to start loop: ";
    //cin>>ans;

    double z_slabs_min = -1.5;
    double z_slabs_max = 1.0;
    double dz_slab = 0.05;
    double z_table;
    int i_best_slab = 0;
    int nslabs;
    vector<int> indices_slab_i;
    std::vector<vector<int> > indices_by_slab;
    vector<int> indices_windowed;
    vector<int> indices_object_top;
    int npts_windowed, npts_slab;
    int window_bkpt;
    int islab_can_top = 31; //want to find this...be more intelligent about it
    vector<float> z_mins;
    vector<float> z_maxs;
    bool success;
    //int ans;
    int ix_centroid, jy_centroid;
    while (ros::ok()) {
        if (g_trigger) {
            g_trigger = false; // reset the trigger

            switch (g_pcl_process_mode) { // what we do here depends on our mode; mode is settable via a service
                case PCL_TAKE_SNAPSHOT:
                    ROS_INFO("case TAKE_SNAPSHOT:");
                    g_take_snapshot = true;
                    // set status back to IDLE in callback, when done                    
                    break;
                case PCL_SORT_POINTS_BY_HORIZ_SLABS:
                    ROS_INFO("sort points by slabs...");
                    sort_pts_by_horiz_planes(g_cloud_wrt_base, z_slabs_min, z_slabs_max, dz_slab, indices_by_slab, i_best_slab, z_mins, z_maxs);
                    nslabs = indices_by_slab.size();
                    ROS_INFO("windowing nslabs = %d", nslabs);
                    for (int islab = 0; islab < nslabs; islab++) {
                        indices_slab_i = indices_by_slab[islab];
                        npts_slab = indices_slab_i.size();
                        if (npts_slab > 0) {

                            ROS_INFO("slab %d has %d points btwn z= %f and %f", islab, npts_slab, z_mins[islab], z_maxs[islab]);
                            //debug display slab by slab
                            copy_cloud(g_pclKinect, indices_slab_i, g_display_cloud);
                            pcl::toROSMsg(*g_display_cloud, *g_pcl2_display_cloud);
                            g_pcl2_display_cloud->header.stamp = ros::Time::now(); //update the time stamp, so rviz does not complain        
                            g_pubCloud.publish(g_pcl2_display_cloud); //and also display whatever we choose to put in here
                            ros::spinOnce();
                            ROS_INFO("enter 1: ");
                            cin>>window_bkpt;
                            object_finder.xy_window(g_cloud_wrt_base, indices_slab_i, indices_windowed);
                            npts_windowed = indices_windowed.size();
                            ROS_INFO("windowed slab %d has %d points;  ", islab, npts_windowed);
                            //debug display slab by slab
                            copy_cloud(g_pclKinect, indices_windowed, g_display_cloud);
                            pcl::toROSMsg(*g_display_cloud, *g_pcl2_display_cloud);
                            g_pcl2_display_cloud->header.stamp = ros::Time::now(); //update the time stamp, so rviz does not complain        
                            g_pubCloud.publish(g_pcl2_display_cloud); //and also display whatever we choose to put in here
                            ros::spinOnce();
                            ROS_INFO("enter 1: ");
                            cin>>window_bkpt;
                        }
                    }
                    // reslice slabnum 31...will want to make this more intelligent
                    indices_slab_i = indices_by_slab[islab_can_top];
                    object_finder.xy_window(g_cloud_wrt_base, indices_slab_i, indices_windowed); //repeated from above
                    z_slabs_min = z_mins[islab_can_top];
                    z_slabs_max = z_maxs[islab_can_top];
                    dz_slab = 0.01;

                    // for points within chosen thick slab, windowed in x,y, slice into thinner slabs:
                    sort_pts_by_horiz_planes(g_cloud_wrt_base, indices_windowed, z_slabs_min, z_slabs_max, dz_slab, indices_by_slab, i_best_slab, z_mins, z_maxs);
                    nslabs = indices_by_slab.size();
                    for (int islab = 0; islab < nslabs; islab++) {
                        indices_slab_i = indices_by_slab[islab];
                        npts_slab = indices_slab_i.size();
                        if (npts_slab > 0) {
                            ROS_INFO("slab %d has %d points btwn z= %f and %f", islab, npts_slab, z_mins[islab], z_maxs[islab]);
                            //debug display slab by slab
                            copy_cloud(g_pclKinect, indices_slab_i, g_display_cloud);
                            pcl::toROSMsg(*g_display_cloud, *g_pcl2_display_cloud);
                            g_pcl2_display_cloud->header.stamp = ros::Time::now(); //update the time stamp, so rviz does not complain        
                            g_pubCloud.publish(g_pcl2_display_cloud); //and also display whatever we choose to put in here
                            ros::spinOnce();
                            ROS_INFO("enter 1: ");
                            cin>>window_bkpt;
                        }
                    }

                    ROS_INFO("done analyzing slabs");
                    g_status = PCL_STATUS_IDLE;

                    break;
                case PCL_MAKE_CAN_CLOUD:
                    ROS_INFO("creating a can cloud");
                    make_can_cloud(g_display_cloud, R_CYLINDER, H_CYLINDER);
                    g_status = PCL_STATUS_IDLE;
                    break;
                    /**/
                case PCL_IDENTIFY_PLANE:
                    ROS_INFO("MODE 0: identifying plane based on patch selection...");
                    find_plane(g_plane_params, g_indices_of_plane); // results in g_display_cloud (in orig frame), as well as 
                    //g_cloud_transformed (rotated version of original cloud); g_indices_of_plane indicate points on the plane
                    g_status = PCL_STATUS_IDLE;
                    break;
                case PCL_SAMPLE_OBJECT:
                    for (int i = 0; i < silhouette_size; i++) {
                        for (int j = 0; j < silhouette_size; j++) {
                            g_silhouette_z[i][j] = 0;
                            g_depth_z[i][j] = 0.0;
                        }
                    }     
                    success = sample_object();
                    if (success) { ROS_INFO("have new, averaged, sampled object in memory"); }
                    else { ROS_INFO("sample_object() returned FALSE"); }
                    g_status = PCL_STATUS_IDLE;                    
                    break;
                case PCL_TRAIN_NEW_OBJECT:
                    for (int i = 0; i < silhouette_size; i++) {
                        for (int j = 0; j < silhouette_size; j++) {
                            g_silhouette_z[i][j] = 0;
                            g_depth_z[i][j] = 0.0;
                        }
                    }
                    ROS_INFO("enter object name:  ");
                    cin>>g_object_name;
                    ROS_INFO("new object name is %s", g_object_name.c_str()); //.c_str());
                    ROS_INFO("taking snapshot...");
                    g_take_snapshot = true;
                    while (g_take_snapshot) {
                        ros::spinOnce();
                    }
                    ROS_INFO("got snapshot.");
                    success = sample_object();
                    g_status = PCL_STATUS_IDLE;
                    break;
                case PCL_FIND_PNTS_ABOVE_PLANE:
                    ROS_INFO("filtering for points above identified plane");
                    // w/ affine transform, z-coord of points on plane (in plane frame) should be ~0
                    z_threshold = 0.0 + Z_EPS + g_table_z_wrt_base; //g_plane_params[3] + Z_EPS;
                    ROS_INFO("filtering for points above %f ", z_threshold);

                    //filter_cloud_above_z(g_cloud_transformed, z_threshold, indices_pts_above_plane);
                    filter_cloud_above_z(g_cloud_wrt_base, z_threshold, g_x_view_min, g_x_view_max, g_y_view_min, g_y_view_max, indices_pts_above_plane);
                    if (indices_pts_above_plane.size() == 0) {
                        ROS_WARN("no points found within filter volume!");
                        break;
                    }
                    //extract these points--but in original, non-rotated frame; useful for display
                    copy_cloud(g_pclKinect, indices_pts_above_plane, g_display_cloud);
                    //test:
                    ROS_INFO("finding bounding box");
                    //cout<<"finding bounding box; enter 1: ";
                    //int ans;
                    //cin>>ans;
                    int silhouette_z[silhouette_size][silhouette_size];
                    float depth_z[silhouette_size][silhouette_size];
                    //transform_cloud(g_pclKinect, g_affine_kinect_to_base,  g_cloud_wrt_base);
                    //find_bounding_box(g_cloud_transformed, indices_pts_above_plane,x_min,x_max,y_min,y_max,z_min,z_max);
                    find_bounding_box(g_cloud_wrt_base, indices_pts_above_plane, x_min, x_max, y_min, y_max, z_min, z_max);

                    compute_depth_map_z(g_cloud_wrt_base, indices_pts_above_plane, x_min, x_max, y_min, y_max,
                            silhouette_z, depth_z, ix_centroid, jy_centroid);
                    display_silhouette_z(silhouette_z);
                    ROS_INFO("bounds: x_min, x_max, y_min, y_max = %f, %f %f, %f", x_min, x_max, y_min, y_max);
                    ROS_INFO("centroid indices: %d, %d", ix_centroid, jy_centroid);
                    cout << "enter 1: ";

                    cin>>ans;
                    //success= recenter_silhouette(silhouette_z,ix_centroid,jy_centroid);
                    success = recenter_silhouette_and_depth_map_z(silhouette_z, depth_z, ix_centroid, jy_centroid);

                    ROS_INFO("recentered silhouette: ");
                    display_silhouette_z(silhouette_z);
                    compute_silhouette_centroid(silhouette_z, ix_centroid, jy_centroid);

                    //
                    g_status = PCL_STATUS_IDLE;
                    break;
                case PCL_COMPUTE_CYLINDRICAL_FIT_ERR_INIT:
                    //double cx,cy;
                    // base coords on centroid of most recent patch, and offset normal to this patch by radius of cylinder
                    ROS_INFO("creating a can cloud");
                    make_can_cloud(g_display_cloud, R_CYLINDER, H_CYLINDER);

                    for (int i = 0; i < 3; i++) {
                        g_cylinder_origin[i] = g_patch_centroid[i] - R_CYLINDER * g_plane_params[i]; // TEST TEST TEST -R_CYLINDER*g_plane_params[i];
                    }

                    // fix the z-height, based on plane height: watch out for signs
                    cout << "g_z_plane_nom: " << g_z_plane_nom << endl;
                    cout << "g_cylinder_origin: " << g_cylinder_origin.transpose() << endl;
                    cout << "g_plane_normal: " << g_plane_normal.transpose() << endl;
                    cout << "g_plane_origin: " << g_plane_origin.transpose() << endl;
                    //height_delta_wrt_sensor_frame = (g_z_plane_nom - g_plane_origin).dot(g_plane_normal)*g_plane_normal;
                    //cout<<"height_delta_wrt_sensor_frame: "<<height_delta_wrt_sensor_frame.transpose()<<endl;
                    //redo:
                    height_delta_wrt_sensor_frame = (g_cylinder_origin - g_plane_origin).dot(g_plane_normal) * g_plane_normal;
                    cout << "height_delta_wrt_sensor_frame: " << height_delta_wrt_sensor_frame.transpose() << endl;
                    g_cylinder_origin -= height_delta_wrt_sensor_frame; // - (g_z_plane_nom - g_plane_normal.dot(g_cylinder_origin))*g_plane_normal;

                    // now, cast this into the rotated coordinate frame:
                    can_center_wrt_plane = g_A_plane.inverse() * g_cylinder_origin; //g_R_transform.transpose()*can_center_wrt_plane;

                    cout << "initial guess for cylinder fit: " << endl;
                    cout << " attempting fit at c = " << can_center_wrt_plane.transpose() << endl;

                    compute_radial_error(g_cloud_transformed, indices_pts_above_plane, R_CYLINDER, can_center_wrt_plane, E, dEdCx, dEdCy);
                    cout << "E: " << E << "; dEdCx: " << dEdCx << "; dEdCy: " << dEdCy << endl;
                    cout << "R_xform: " << endl;
                    cout << g_R_transform << endl;
                    A_plane_to_sensor.linear() = g_R_transform;
                    A_plane_to_sensor.translation() = g_cylinder_origin;
                    g_A_model_wrt_torso = A_plane_to_sensor;
                    transform_cloud(g_canCloud, A_plane_to_sensor, g_display_cloud);
                    g_status = PCL_STATUS_IDLE;

                    break;
                case PCL_COMPUTE_CYLINDRICAL_FIT_ERR_ITERATE:
                    cout << "current cx,cy = " << can_center_wrt_plane[0] << ", " << can_center_wrt_plane[1] << endl;
                    //cout<<"enter new cx: ";
                    //cin>>cx;
                    //scale dEdC:
                    dEnorm = sqrt(dEdCx * dEdCx + dEdCy * dEdCy);

                    can_center_wrt_plane[0] -= 0.001 * dEdCx / dEnorm; //move along negative gradient by 1mm
                    can_center_wrt_plane[1] -= 0.001 * dEdCy / dEnorm;

                    ROS_INFO("attempting to fit points to cylinder, radius %f, cx = %f, cy = %f", R_CYLINDER, can_center_wrt_plane[0], can_center_wrt_plane[1]);
                    compute_radial_error(g_cloud_transformed, indices_pts_above_plane, R_CYLINDER, can_center_wrt_plane, E, dEdCx, dEdCy);
                    cout << "E: " << E << "; dEdCx: " << dEdCx << "; dEdCy: " << dEdCy << endl;
                    //cout<<"R_xform: "<<g_R_transform<<endl;
                    g_cylinder_origin = g_A_plane*can_center_wrt_plane;
                    A_plane_to_sensor.translation() = g_cylinder_origin;
                    g_A_model_wrt_torso = A_plane_to_sensor;
                    transform_cloud(g_canCloud, A_plane_to_sensor, g_display_cloud);
                    //g_canCloud
                    g_status = PCL_STATUS_IDLE;

                    break;

                case PCL_COMPUTE_Z_BED_OF_NAILS:
                    find_bounding_box(g_cloud_transformed, indices_pts_above_plane, x_min, x_max, y_min, y_max, z_min, z_max);
                    g_status = PCL_STATUS_IDLE;
                    break;

                case PCL_FIND_FLOOR:
                    ROS_INFO("case PCL_FIND_FLOOR");
                    object_finder.find_floor(g_cloud_wrt_base, indices_slab_i);
                    npts_slab = indices_slab_i.size();

                    ROS_INFO("presumed floor slab has %d points ", npts_slab);
                    //debug display slab by slab
                    copy_cloud(g_pclKinect, indices_slab_i, g_display_cloud);
                    pcl::toROSMsg(*g_display_cloud, *g_pcl2_display_cloud);
                    g_pcl2_display_cloud->header.stamp = ros::Time::now(); //update the time stamp, so rviz does not complain        
                    g_pubCloud.publish(g_pcl2_display_cloud); //and also display whatever we choose to put in here
                    ros::spinOnce();
                    g_status = PCL_STATUS_IDLE;
                    break;

                case PCL_FIND_TABLE:
                    ROS_INFO("case PCL_FIND_TABLE");
                    npts_slab = object_finder.find_table(g_cloud_wrt_base, indices_slab_i, z_table);
                    //npts_slab= indices_slab_i.size();

                    ROS_INFO("presumed table slab has %d points at height %f ", npts_slab, z_table);
                    //debug: display the table pts
                    copy_cloud(g_pclKinect, indices_slab_i, g_display_cloud);
                    pcl::toROSMsg(*g_display_cloud, *g_pcl2_display_cloud);
                    g_pcl2_display_cloud->header.stamp = ros::Time::now(); //update the time stamp, so rviz does not complain        
                    g_pubCloud.publish(g_pcl2_display_cloud); //and also display whatever we choose to put in here
                    ros::spinOnce();
                    g_status = PCL_STATUS_IDLE;
                    break;

                case PCL_FIND_COKE_FRAME:
                    ROS_INFO("case PCL_FIND_COKE_FRAME");
                    z_table = object_finder.get_table_height();

                    //affine_model_frame_wrt_baxter_base = object_finder.find_coke_can_frame(g_cloud_wrt_base, default_table_z_height_wrt_baxter_base_frame);
                    affine_model_frame_wrt_baxter_base = object_finder.find_coke_can_frame(g_cloud_wrt_base, indices_object_top, z_table);

                    copy_cloud(g_pclKinect, indices_object_top, g_display_cloud);
                    pcl::toROSMsg(*g_display_cloud, *g_pcl2_display_cloud);
                    g_pcl2_display_cloud->header.stamp = ros::Time::now(); //update the time stamp, so rviz does not complain        
                    g_pubCloud.publish(g_pcl2_display_cloud); //and also display whatever we choose to put in here
                    ros::spinOnce();
                    // debug output:
                    model_frame_origin = affine_model_frame_wrt_baxter_base.translation();
                    ROS_INFO("coke can model frame w/rt base: %f, %f, %f \n ", model_frame_origin(0), model_frame_origin(1), model_frame_origin(2));
                    cout << "R = " << endl;
                    cout << affine_model_frame_wrt_baxter_base.linear() << endl;
                    g_A_model_wrt_base = eigen_affine_3d_to_3f(affine_model_frame_wrt_baxter_base); // this value will be available for service to report out
                    cout << "g_A_model_wrt_base origin: " << g_A_model_wrt_base.translation().transpose() << endl;
                    g_status = PCL_STATUS_IDLE;
                    break;

                case PCL_FIND_GAZEBO_BEER_FRAME:
                    ROS_INFO("case PCL_FIND_GAZEBO_BEER_FRAME");
                    z_table = object_finder.get_table_height();

                    affine_model_frame_wrt_baxter_base = object_finder.find_gazebo_beer_can_frame(g_cloud_wrt_base, indices_object_top, z_table);
                    //ROS_INFO("presumed table slab has %d points at height %f ",npts_slab,z_table);
                    //debug: display the table pts
                    copy_cloud(g_pclKinect, indices_object_top, g_display_cloud);
                    pcl::toROSMsg(*g_display_cloud, *g_pcl2_display_cloud);
                    g_pcl2_display_cloud->header.stamp = ros::Time::now(); //update the time stamp, so rviz does not complain        
                    g_pubCloud.publish(g_pcl2_display_cloud); //and also display whatever we choose to put in here
                    ros::spinOnce();
                    // debug output:
                    model_frame_origin = affine_model_frame_wrt_baxter_base.translation();
                    ROS_INFO("beer can model frame w/rt base: %f, %f, %f \n ", model_frame_origin(0), model_frame_origin(1), model_frame_origin(2));
                    cout << "R = " << endl;
                    cout << affine_model_frame_wrt_baxter_base.linear() << endl;
                    g_A_model_wrt_base = eigen_affine_3d_to_3f(affine_model_frame_wrt_baxter_base); // this value will be available for service to report out
                    cout << "g_A_model_wrt_base origin: " << g_A_model_wrt_base.translation().transpose() << endl;
                    g_status = PCL_STATUS_IDLE;
                    break;

                case PCL_FIND_GAZEBO_BEER_GRASP_FROM_ABOVE_FRAME:
                    ROS_INFO("case PCL_FIND_GAZEBO_BEER_GRASP_FROM_ABOVE_FRAME");
                    g_A_grasp_wrt_model = object_finder.get_grasp_transform_gazebo_beer_grasp_from_above();
                    grasp_origin_wrt_model = g_A_grasp_wrt_model.translation();

                    ROS_INFO("grasp offset w/rt model: %f, %f, %f", grasp_origin_wrt_model[0], grasp_origin_wrt_model[1], grasp_origin_wrt_model[2]);
                    g_A_grasp_wrt_base = g_A_model_wrt_base*g_A_grasp_wrt_model;
                    g_status = PCL_STATUS_IDLE;
                    break;
                case PCL_FIND_COKE_CAN_GRASP_FROM_ABOVE_FRAME:
                    ROS_INFO("case PCL_FIND_COKE_CAN_GRASP_FROM_ABOVE_FRAME");
                    g_A_grasp_wrt_model = object_finder.get_grasp_transform_coke_can_grasp_from_above();
                    grasp_origin_wrt_model = g_A_grasp_wrt_model.translation();

                    ROS_INFO("grasp offset w/rt model: %f, %f, %f", grasp_origin_wrt_model[0], grasp_origin_wrt_model[1], grasp_origin_wrt_model[2]);
                    g_A_grasp_wrt_base = g_A_model_wrt_base*g_A_grasp_wrt_model;
                    g_status = PCL_STATUS_IDLE;
                    break;


                case PCL_FIND_ON_TABLE:
                    ROS_INFO("filtering for objects on most recently defined plane: not implemented yet");
                    g_status = PCL_STATUS_IDLE;

                    break;

                default:
                    ROS_WARN("this mode is not implemented");
                    g_status = PCL_STATUS_IDLE;

            }
        }

        // these are broken--need new conversions?
        //void pcl::toROSMsg (const pcl::PointCloud< PointT > &cloud,  sensor_msgs::PointCloud2 & msg
        //pubPcdCloud.publish(g_pclKinect); //keep displaying the original scene
        //convert pcl::PointCloud<pcl::PointXYZ>::Ptr to sensor_msgs::PointCloud2
        //pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered (new pcl::PointCloud<pcl::PointXYZ>), cloud_p (new pcl::PointCloud<pcl::PointXYZ>)
        //sensor_msgs::PointCloud2::Ptr final_cloud (new sensor_msgs::PointCloud2);
        //pcl::PointCloud<pcl::PointXYZ>::Ptr g_display_cloud(new pcl::PointCloud<pcl::PointXYZ>); // this cloud gets published--viewable in rviz
        //pcl::toROSMsg (*cloud_p, *final_cloud);

        pcl::toROSMsg(*g_display_cloud, *g_pcl2_display_cloud);
        g_pcl2_display_cloud->header.stamp = ros::Time::now(); //update the time stamp, so rviz does not complain        
        g_pubCloud.publish(g_pcl2_display_cloud); //and also display whatever we choose to put in here

        pcl::toROSMsg(*g_canCloud, *g_pcl2_display_cloud);
        g_pcl2_display_cloud->header.stamp = ros::Time::now(); //update the time stamp, so rviz does not complain        
        pubModel.publish(g_pcl2_display_cloud);
        ros::spinOnce();
        rate.sleep();
    }
    return 0;
}