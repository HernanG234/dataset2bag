#include <ros/ros.h>
#include <rosbag/bag.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <boost/numeric/ublas/io.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <nav_msgs/Odometry.h>
#include <geometry_msgs/TransformStamped.h>
#include <tf/tf.h>
#include <tf/tfMessage.h>
using std::cout;
using std::endl;
using std::cerr;
namespace po = boost::program_options;


bool process_args(int argc, char** argv, po::variables_map& options)
{
  po::options_description options_description("Options");
  options_description.add_options()
    ("images,l", po::value<std::string>(), "pattern of left images or path to video (or mono-camera)") // (Example: the pattern could be form image_%3d.png for images of the form image_000.png)
    ("images_right,r", po::value<std::string>(), "pattern of right images or path to video") // (Example: the pattern could be form image_%3d.png for images of the form image_000.png)
    ("poses,p", po::value<std::string>(), "2D odometry/ground truth in CSV format (x,y,theta)")

    ("output,o", po::value<std::string>()->required(), "output bag file")
    ("skip,s", po::value<int>()->default_value(0), "skip an ammount of frames for each frame processed")
    ("message,m", po::value<std::string>(), "message parameters file")
    /*("image-timestamps", po::value<std::string>(), "file with one timestamp per frame")
    ("image-delta", po::value<int>(), "artificial delta between frames in milliseconds")
    
    ("poses-have-timestamps", "indicates that poses file have a leading field including timestamp")
    ("pose-delta", po::value<int>(), "artificial delta between poses in milliseconds")*/
    
    ("help,h", "show this help")
  ;

  try {
    po::store(po::parse_command_line(argc, argv, options_description), options);
    po::notify(options);
  }
  catch(po::error& e) { 
    cerr << options_description << endl << endl;
    cerr << endl << "ERROR: " << e.what() << endl;
    return false;
  }
  if (options.count("help")) { cerr << options_description << endl; return false; }
  return true;
}






int main(int argc, char** argv)
{
  po::variables_map options;
  if (!process_args(argc, argv, options)) return 1;

  // open bag file
  cout << "Opening bag file..." << endl;
  rosbag::Bag bag;
  bag.open(options["output"].as<std::string>(), rosbag::bagmode::Write);

  // starting point for timestamps
  boost::posix_time::ptime t0 = boost::posix_time::microsec_clock::local_time();
  // TODO: read first of list otherwise
  

  // open parameters file
  cout << "Loading and writing message information..." << endl;
  if (options.count("message")) {
      std::ifstream ifs(options["message"].as<std::string>().c_str());
      boost::array<double,9> calibration, rotation;

      // read height and width
      unsigned int height, width;
      ifs >> height;
      ifs >> width;

      cout << height;
      cout << width;

      // read calibration matrix
      for(int i = 0; i < 9; ++i) {
        ifs >> calibration[i];
        cout << calibration[i];
      }

      // create CameraInfo message
      sensor_msgs::CameraInfo camera_info;
      camera_info.height = height;
      camera_info.width = height;
      camera_info.K = calibration;
      camera_info.R = rotation;
  }

  // open poses file
  cout << "Loading and writing poses..." << endl;
  if (options.count("poses")) {
    boost::posix_time::time_duration pose_delta = boost::posix_time::milliseconds(30);
    // TODO: read from list otherwise
    
    std::ifstream ifs(options["poses"].as<std::string>().c_str());
    ifs >> boost::tuples::set_delimiter(',') >> boost::tuples::set_open(' ') >> boost::tuples::set_close(' ');

    boost::posix_time::ptime t = t0;
    while(ifs.good()) {
      // TODO: poses with timestamps case
      boost::tuple<float, float, float, float> tuple;
      ifs >> tuple;

      // write odometry message
      nav_msgs::Odometry odo_msg;
      odo_msg.header.frame_id = "odom";
      odo_msg.child_frame_id = "base_link";
      odo_msg.pose.pose.position.x = tuple.get<0>();
      odo_msg.pose.pose.position.y = tuple.get<1>();
      odo_msg.pose.pose.position.z = 0;
      odo_msg.pose.pose.orientation = tf::createQuaternionMsgFromYaw(tuple.get<3>());
      bag.write("/robot/odometry", ros::Time::fromBoost(t), odo_msg);

      // write tf message
      geometry_msgs::TransformStamped tmsg;
      tmsg.header.frame_id = "odom";
      tmsg.child_frame_id = "base_link";
      tmsg.transform.translation.x = odo_msg.pose.pose.position.x;
      tmsg.transform.translation.y = odo_msg.pose.pose.position.y;
      tmsg.transform.translation.z = 0;
      tmsg.transform.rotation = odo_msg.pose.pose.orientation;
      
      tf::tfMessage tf_msg;
      tf_msg.transforms.push_back(tmsg);
      tf_msg.transforms.back().header.frame_id = tf::resolve("", tf_msg.transforms.back().header.frame_id);
      tf_msg.transforms.back().child_frame_id = tf::resolve("", tf_msg.transforms.back().child_frame_id);
      
      bag.write("/tf", ros::Time::fromBoost(t), tf_msg);
      
      t += pose_delta;
    }
  }

  // is a stereo dataset?
  bool IsStereo =  options.count("images_right") > 0;



  // open images
  if (options.count("images") > 0) {
    cv::VideoCapture capture(options["images"].as<std::string>());

    // declare capture_right
    cv::VideoCapture capture_right;
    if (IsStereo) // if there is an stereo source open it
      capture_right = cv::VideoCapture(options["images_right"].as<std::string>());

    // image timestamps
    std::list<double> image_timestamps;
    if (options.count("image-timestamps")) {
      throw std::runtime_error("finish!");
      cout << "Loading image timestamps... " << endl;
      std::ifstream ifs(options["image-timestamps"].as<std::string>().c_str());
      double timestamp;
      while (ifs) { ifs >> timestamp; image_timestamps.push_back(timestamp); }
    }

    cout << "Loading and writing images..." << endl;
    cv_bridge::CvImage ros_image, ros_image_right;
    uint seq = 0;
    boost::posix_time::ptime t = t0;
    boost::posix_time::time_duration image_delta = boost::posix_time::milliseconds(30);

    // TODO: read from list otherwise
    int total_frames = (int)capture.get(CV_CAP_PROP_FRAME_COUNT);
    int i = 0, skip_counter = 0;
    cout << "total_frames: " << total_frames << endl;

    while (capture.read(ros_image.image)) {
      if (options.count("skip") > 0 && skip_counter == options["skip"].as<int>())
      {
        cv::cvtColor(ros_image.image, ros_image.image, CV_BGR2RGB);
        ros_image.encoding = "rgb8";
        // stereo behavior
        if (IsStereo) {
          capture_right.read(ros_image_right.image);
          cv::cvtColor(ros_image_right.image, ros_image_right.image, CV_BGR2RGB);
          ros_image_right.encoding = "rgb8";

          // create Image message left
          sensor_msgs::ImagePtr ros_image_left_msg, ros_image_right_msg;
          ros_image_left_msg = ros_image.toImageMsg();
          ros_image_left_msg->header.seq = seq;
          ros_image_left_msg->header.stamp = ros::Time::fromBoost(t);
          ros_image_left_msg->header.frame_id = "camera";

          // create image message right
          ros_image_right_msg = ros_image_right.toImageMsg();
          ros_image_right_msg->header.seq = seq;
          ros_image_right_msg->header.stamp = ros::Time::fromBoost(t);
          ros_image_right_msg->header.frame_id = "camera";


          bag.write("/stereo/left/image_raw", ros::Time::fromBoost(t), ros_image_left_msg);
          bag.write("/stereo/right/image_raw", ros::Time::fromBoost(t), ros_image_right_msg);

          // create CameraInfo messages
          sensor_msgs::CameraInfo camera_info;
          camera_info.header.seq = seq;
          camera_info.header.stamp = ros::Time::fromBoost(t);
          camera_info.header.frame_id = "camera";
          bag.write("/stereo/left/camera_info", ros::Time::fromBoost(t), camera_info);
          bag.write("/stereo/right/camera_info", ros::Time::fromBoost(t), camera_info);

        }
        else {
          bag.write("/camera/image_raw", ros::Time::fromBoost(t), ros_image.toImageMsg());
        }

        skip_counter = 0;
      }
      else skip_counter++;
      
      t += image_delta;
      seq++;
      if (i % (total_frames / 10) == 0) cout << (i / (total_frames / 100)) << "%..." << std::flush;
      i++;
    }
    cout << endl;
  }
  bag.close();
}
