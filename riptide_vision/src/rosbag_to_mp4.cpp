#include "riptide_vision/rosbag_to_mp4.h"
using namespace cv;
static const std::string OPENCV_WINDOW = "Image Window";

int main(int argc, char** argv) {
  ros::init(argc, argv, "rosbag_to_mp4");
  RosbagToMP4 rtm;
  rtm.Loop();
}

RosbagToMP4::RosbagToMP4() {
  nh.param<std::string>("/rosbag_to_mp4/topic", topic, (std::string)"/forward/image_raw");
  nh.param<std::string>("/rosbag_to_mp4/username", username, (std::string)"tsender");
  nh.param<std::string>("/rosbag_to_mp4/file_name", file_name, (std::string)"pool_test");
  nh.param<std::string>("/rosbag_to_mp4/ext", ext, (std::string)".mp4");
  nh.param<int>("/rosbag_to_mp4/frame_rate", frame_rate, 30); // Could also be 16
  nh.param<int>("/rosbag_to_mp4/width", width, 1288); // Max 1288
  nh.param<int>("/rosbag_to_mp4/height", height, 964); // Max 964

  Size frame_size(width, height);
  file_path = "/home/" + username + "/rosbags/" + file_name + ext;
  videoWriter = VideoWriter(file_path, CV_FOURCC('M', 'J', 'P', 'G'), frame_rate, frame_size, true);
  namedWindow(OPENCV_WINDOW, WINDOW_NORMAL);

  image_sub = nh.subscribe<sensor_msgs::Image>(topic, 1, &RosbagToMP4::WriteVideo, this);

  frames = 0;
}

RosbagToMP4::~RosbagToMP4() {
  videoWriter.release();
  destroyAllWindows();
}

void RosbagToMP4::WriteVideo(const sensor_msgs::Image::ConstPtr &msg) {
  try {
    // Create a copy of the image and point to the copied image
    // Use the BGR8 image_encoding
    cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
  }
  catch (cv_bridge::Exception& e ){
    ROS_ERROR("cv_bridge exception:  %s", e.what());
    return;
  }

  if(!cv_ptr->image.empty()) {
    if(videoWriter.isOpened()) {
      videoWriter.write(cv_ptr->image);
      //frames++;
      //ROS_INFO("Wrote %i frames to %s", frames, (file_name + ext).c_str());
      imshow(OPENCV_WINDOW, cv_ptr->image);
      waitKey(3);
    }
  }
}

void RosbagToMP4::Loop()
{
  ros::Rate rate(2*frame_rate);
  while (ros::ok())
  {
    ros::spinOnce();
    rate.sleep();
  }
}