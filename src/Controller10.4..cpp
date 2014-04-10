#include "ros/ros.h"
#include "geometry_msgs/Twist.h"
#include "sensor_msgs/LaserScan.h"
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/Point.h"
#include "tf/transform_datatypes.h"
#include "tf/transform_broadcaster.h"
#include "bb_state/TwistWithID.h"
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

using namespace std;

class controller
{
  vector<geometry_msgs::Twist> path_with_information;
  geometry_msgs::Twist path_with_informationpoint;
  ros::Subscriber pose_sub;
  ros::Publisher twistvelocityoutputpub;
  double angledifference;
  double angular_vel_correction;
  double absolute_vel_correction;
  int i_nearest;
  int i_second_nearest;
  double desired_heading_at_i_nearest;
  double desired_heading_at_i_nearest_plus;
  double circle_radius;
  double phi_to_plus;
  double angular_velocity_;
  double rectangular_error;
  const static double absolute_velocity_ = 0.2;




  // Controller Gains:

    //gain must be negative:
    const static double gain_angular_err = -1.7;

    //gain must be positive:
    const static double gain_absolute_err = 5;

    //lookahead variable: (Designs the lookahead in default velocity prescriptions)
    const static int magic_number = 3;

  //End of Controller Gains..




public:

  void pose_sub_Callback(const geometry_msgs::PoseStamped &laserpose)
  {
    //Get Localization Measurements:
    double pose_x = laserpose.pose.position.x;
    double pose_y = laserpose.pose.position.y;
    //pose_theta = laserpose.pose.position.z; (Was for Testing!!, perhaps still used somtime..)
    double pose_theta = tf::getYaw(laserpose.pose.orientation);

    double x_nearest;
    double y_nearest;
    double x_second_nearest;
    double y_second_nearest;

    //Find the nearest point on the path:
    double distance_to_nearest_point_on_path = 10000000000000;
    double distance_to_second_nearest_point_on_path = 100000000000000;
    for(int i=0; i<path_with_information.size(); i++){
        double temp_var_for_distance = sqrt(
            (laserpose.pose.position.x - path_with_information[i].linear.x) *
            (laserpose.pose.position.x - path_with_information[i].linear.x) +
            (laserpose.pose.position.y - path_with_information[i].linear.y) *
            (laserpose.pose.position.y - path_with_information[i].linear.y)
        );
        if(temp_var_for_distance < distance_to_nearest_point_on_path){
            distance_to_nearest_point_on_path = temp_var_for_distance;
            x_nearest = path_with_information[i].linear.x;
            y_nearest = path_with_information[i].linear.y;
            i_nearest = i;
        }
    }
    //find closest neighbour
    double distance_neighbours[2];
    distance_neighbours[0]=sqrt(
            (laserpose.pose.position.x - path_with_information[i_nearest-1].linear.x) *
            (laserpose.pose.position.x - path_with_information[i_nearest-1].linear.x) +
            (laserpose.pose.position.y - path_with_information[i_nearest-1].linear.y) *
            (laserpose.pose.position.y - path_with_information[i_nearest-1].linear.y));
    distance_neighbours[1]=sqrt(
            (laserpose.pose.position.x - path_with_information[i_nearest+1].linear.x) *
            (laserpose.pose.position.x - path_with_information[i_nearest+1].linear.x) +
            (laserpose.pose.position.y - path_with_information[i_nearest+1].linear.y) *
            (laserpose.pose.position.y - path_with_information[i_nearest+1].linear.y));

    if(distance_neighbours[0]<=distance_neighbours[1])
    {
        i_second_nearest=i_nearest-1;
        x_second_nearest = path_with_information[i_second_nearest].linear.x;
        y_second_nearest = path_with_information[i_second_nearest].linear.y;
        distance_to_second_nearest_point_on_path = distance_neighbours[0];
    }
    else
    {
        i_second_nearest=i_nearest+1;
        x_second_nearest = path_with_information[i_second_nearest].linear.x;
        y_second_nearest = path_with_information[i_second_nearest].linear.y;
        distance_to_second_nearest_point_on_path = distance_neighbours[1];
    }

    double angle_of_line_between_two_nearest_points;
    int sign_of_error = 0;
    double faktor;

    //Function angle_of_line_b.... is defined before main.
    angle_of_line_between_two_nearest_points = angle_of_line_between_points(i_nearest, i_second_nearest);

    double rectangular_error_abs;

    if(distance_to_nearest_point_on_path < 0.00001){
        rectangular_error_abs = 0;
        faktor = 0;
    }
    else{
        //Calculate rectangular offset Error:
        double distance_between_two_nearest_points = sqrt((x_nearest-x_second_nearest)*(x_nearest-x_second_nearest)+
                                                          (y_nearest-y_second_nearest)*(y_nearest-y_second_nearest));

        if (i_nearest > i_second_nearest){
            angle_of_line_between_two_nearest_points = atan2((y_nearest-y_second_nearest),(x_nearest-x_second_nearest));
        }
        else angle_of_line_between_two_nearest_points = atan2((y_second_nearest-y_nearest),(x_second_nearest-x_nearest));

        faktor = acos((distance_to_nearest_point_on_path * distance_to_nearest_point_on_path -
                         distance_to_second_nearest_point_on_path * distance_to_second_nearest_point_on_path +
                         distance_between_two_nearest_points * distance_between_two_nearest_points) /
                        (2 * distance_to_nearest_point_on_path * distance_between_two_nearest_points));

        rectangular_error_abs = abs(sin(faktor) * distance_to_nearest_point_on_path);

        //Determine on which side of the path(left or right) according to the platforms heading the robot is:
        if (cos(angle_of_line_between_two_nearest_points) > 0){
            if ((y_nearest+tan(angle_of_line_between_two_nearest_points)*(pose_x-x_nearest)) >= pose_y){
                sign_of_error = 1;
            }
            else sign_of_error = -1;
        }
        else if(cos(angle_of_line_between_two_nearest_points) < 0){
            if ((y_nearest-tan(angle_of_line_between_two_nearest_points)*(pose_x-x_nearest)) >= pose_y){
                sign_of_error = -1;
                ROS_INFO("thats the case");
            }
            else sign_of_error = 1;
        }
        else if (fmod(abs(angle_of_line_between_two_nearest_points),(2*M_PI)) - M_PI/2 < 0.00001){
            if (x_nearest < pose_x){
                sign_of_error = 1;
                 }
            else sign_of_error = -1;
        }
        else{
            if (x_nearest < pose_x){
                sign_of_error = -1;
            }
            else sign_of_error = 1;
        }
    }

    rectangular_error = rectangular_error_abs * sign_of_error;

    if(abs(angle_of_line_between_two_nearest_points - pose_theta) < abs(pose_theta - angle_of_line_between_two_nearest_points)){
        angledifference = angle_of_line_between_two_nearest_points - pose_theta;
    }
    else{
        angledifference = pose_theta - angle_of_line_between_two_nearest_points;
    }

    double ang, absolute;
    ang = default_velocity_input() + get_velocity_corrections();

    //Saturation for angular velocity, not to get high values:
    if(abs(ang) > 0.5){
        ang = ang / abs(ang) * 0.5;
    }

    absolute = absolute_velocity_;
    ROS_INFO("angvel %f", ang);

    if(i_nearest > 970){
        ang,absolute = 0;
    }

    ROS_INFO("angular_vel: %f, i: %d, corr %f, heading i: %f, heading i plus %f", angular_velocity_, i_nearest, angular_vel_correction, desired_heading_at_i_nearest, desired_heading_at_i_nearest_plus);
    ROS_INFO("circle_radius: %f, phi_to_plus %f", circle_radius, phi_to_plus);

    geometry_msgs::Twist Velocityoutput;
    Velocityoutput.linear.x = absolute;
    Velocityoutput.angular.z = ang;
    //Velocityoutput.id = 3;

    twistvelocityoutputpub.publish(Velocityoutput);
  }
  
  //Read in the path:
  void ReadFile(const char *filename) {
      std::ifstream infile(filename);
      if (infile.is_open()) {
          ROS_INFO("reading file...");
          std::string line;
          while(std::getline(infile, line)) {
	      ROS_INFO("got here");
              std::istringstream iss(line);
              if (!(iss >> path_with_informationpoint.linear.x >> path_with_informationpoint.linear.y)) {
                  ROS_WARN("Du kannst nicht lesen?");
              }
              path_with_information.push_back(path_with_informationpoint);
          }
      }
      else ROS_WARN("failed to open file");
      ROS_INFO("see %f, %f", path_with_information[1].linear.y, path_with_information[9].linear.x);
      infile.close();
  }

  //Actual controller:
  double get_velocity_corrections(){
    angular_vel_correction = gain_angular_err * angledifference;
    absolute_vel_correction = gain_absolute_err * rectangular_error;
    return (angular_vel_correction + absolute_vel_correction);
  }
  
  double default_velocity_input(){

    int i_nearest_plus = i_nearest + magic_number;

    desired_heading_at_i_nearest = (angle_of_line_between_points(i_nearest+1, i_nearest) + angle_of_line_between_points(i_nearest, i_nearest-1))/2;
    desired_heading_at_i_nearest_plus = (angle_of_line_between_points(i_nearest_plus+1, i_nearest_plus) + angle_of_line_between_points(i_nearest_plus,i_nearest_plus-1))/2;
    phi_to_plus = desired_heading_at_i_nearest_plus - desired_heading_at_i_nearest;

    double chord_length =  sqrt((path_with_information[i_nearest].linear.x - path_with_information[i_nearest_plus].linear.x) *
                           (path_with_information[i_nearest].linear.x - path_with_information[i_nearest_plus].linear.x) +
                           (path_with_information[i_nearest].linear.y - path_with_information[i_nearest_plus].linear.y) *
                           (path_with_information[i_nearest].linear.y - path_with_information[i_nearest_plus].linear.y));

    circle_radius = chord_length / (2 * sin(phi_to_plus/2));

    //Assign angular velocity:
    angular_velocity_ = absolute_velocity_ / circle_radius + angular_vel_correction + absolute_vel_correction;

    return angular_velocity_;

  }

  double angle_of_line_between_points(int i1, int i2){

      double angle;

      if (i1 > i2){
           angle = atan2((path_with_information[i1].linear.y-path_with_information[i2].linear.y),(path_with_information[i1].linear.x-path_with_information[i2].linear.x));
      }
      else angle = atan2((path_with_information[i2].linear.y-path_with_information[i1].linear.y),(path_with_information[i2].linear.x-path_with_information[i1].linear.x));

      return angle;
  }
  
  
  controller()
  {
    ros::NodeHandle n;
    pose_sub = n.subscribe("ref_pose",1000, &controller::pose_sub_Callback, this);
    twistvelocityoutputpub = n.advertise<geometry_msgs::Twist>("cmd_vel", 100);
    this->ReadFile("/home/beachbot/Controllertestexample.txt");
  }
  
  ~controller()
  {
    //whatever
  }
  
};

int main(int argc, char **argv){

    ros::init(argc, argv, "Controller");
    controller *controllerinstance = new controller();

    double time = ros::Time::now().toNSec();
    ROS_INFO("reached here %f",time);

    ros::Rate loop_rate(25);

    while (ros::ok())
    {
        controllerinstance->get_velocity_corrections();
        controllerinstance->default_velocity_input();

        ros::spinOnce();

        loop_rate.sleep();
    }
    delete controllerinstance;
    return 0;
}
