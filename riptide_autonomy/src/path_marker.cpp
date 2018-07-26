#include "riptide_autonomy/path_marker.h"

#define ALIGN_CENTER 0
#define ALIGN_BBOX_WIDTH 1
#define ALIGN_OFFSET 2

//  Path Marker, what it does:
// 1: If we see the Path Marker, abort tslam & get path heading
// 2: Once we have determined the heading start aligning
// 3: Once we are at the right angle, go forward
// 4: Once we are over the center, turn
// 5: Once we have turned, go
// 6: Once we have gone, be done

PathMarker::PathMarker(BeAutonomous *master)
{
  this->master = master;
  od = new ObjectDescriber(master);
  PathMarker::Initialize();
}

void PathMarker::Initialize()
{
  detections = 0;
  attempts = 0;

  for (int i = 0; i < sizeof(active_subs) / sizeof(active_subs[0]); i++)
    active_subs[i]->shutdown();
}

void PathMarker::Start()
{
  align_cmd.surge_active = false;
  align_cmd.sway_active = false;
  align_cmd.heave_active = false;
  align_cmd.object_name = master->object_names.at(0); // PathMarker
  align_cmd.alignment_plane = master->alignment_plane;
  align_cmd.bbox_dim = (int)(master->frame_height * 0.7);
  align_cmd.bbox_control = rc::CONTROL_BBOX_HEIGHT;
  align_cmd.target_pos.x = 0;
  align_cmd.target_pos.y = 0;
  align_cmd.target_pos.z = 0;
  master->alignment_pub.publish(align_cmd);
  ROS_INFO("PathMarker: alignment command published (but disabled)");

  task_bbox_sub = master->nh.subscribe<darknet_ros_msgs::BoundingBoxes>("/task/bboxes", 1, &PathMarker::IDPathMarker, this);
}

// If we see the Path Marker, abort tslam & get angle
void PathMarker::IDPathMarker(const darknet_ros_msgs::BoundingBoxes::ConstPtr &bbox_msg)
{
  // Get number of objects and make sure you have 'x' many within 't' seconds
  // Simply entering this callback signifies the object was detected (unless it was a false-positive)
  detections++;
  if (detections == 1)
  {
    detect_start = ros::Time::now();
    attempts++;
  }

  if (ros::Time::now().toSec() - detect_start.toSec() > master->detection_duration_thresh)
  {
    if (detections >= master->detections_req)
    {
      task_bbox_sub.shutdown();
      master->tslam->Abort(true);

      od->GetPathHeading(&PathMarker::GotHeading, this);
      ROS_INFO("Found path, getting heading");
    }
    else
    {
      ROS_INFO("PathMarker: Attempt %i to ID PathMarker", attempts);
      ROS_INFO("PathMarker: %i detections", detections);
      ROS_INFO("PathMarker: Beginning attempt %i", attempts + 1);
      detections = 0;
    }
  }
}

// Once we have determined the heading start aligning
void PathMarker::GotHeading(double heading)
{
  ROS_INFO("PathMarker angle: %f", heading);

  attitude_cmd.roll_active = true;
  attitude_cmd.pitch_active = true;
  attitude_cmd.yaw_active = true;
  attitude_cmd.euler_rpy.x = 0;
  attitude_cmd.euler_rpy.y = 0;

  if (heading < 90 && heading > -90)
  {
    pathDirection = right;
    ROS_INFO("Path Right");
    path_heading = master->euler_rpy.z + (heading - (-22.5));
  }
  else
  {
    pathDirection = left;
    ROS_INFO("Path Left");
    path_heading = master->euler_rpy.z + (heading - (-157.5));
  }

  path_heading = master->tslam->KeepHeadingInRange(path_heading);

  attitude_cmd.euler_rpy.z = path_heading;
  master->attitude_pub.publish(attitude_cmd);
  align_cmd.surge_active = false;
  align_cmd.sway_active = true;
  align_cmd.heave_active = false;
  master->alignment_pub.publish(align_cmd);
  attitude_status_sub = master->nh.subscribe<riptide_msgs::ControlStatusAngular>("/status/controls/angular", 1, &PathMarker::FirstAttitudeStatusCB, this);

  ROS_INFO("PathMarker: Identified Heading. Now rotating and aligning along y");
}

// Once we are at the right angle, go forward
void PathMarker::FirstAttitudeStatusCB(const riptide_msgs::ControlStatusAngular::ConstPtr &status_msg)
{
  heading_average = (heading_average * 99 + abs(status_msg->yaw.error)) / 100;

  if (heading_average < master->yaw_thresh)
  {
    attitude_status_sub.shutdown();
    heading_average = 360;

    // TODO: If you dont see it, do something

    align_cmd.surge_active = true;
    align_cmd.sway_active = true;
    align_cmd.heave_active = false;
    master->alignment_pub.publish(align_cmd);
    alignment_status_sub = master->nh.subscribe<riptide_msgs::ControlStatusLinear>("/status/controls/linear", 1, &PathMarker::AlignmentStatusCB, this);
    ROS_INFO("PathMarker: At heading. Now aligning center");
  }
}

// Once we are over the center, turn
void PathMarker::AlignmentStatusCB(const riptide_msgs::ControlStatusLinear::ConstPtr &status_msg)
{
  x_average = (x_average * 9 + abs(status_msg->x.error)) / 10;
  y_average = (y_average * 9 + abs(status_msg->y.error)) / 10;

  if (x_average < master->align_thresh && y_average < master->align_thresh)
  {
    alignment_status_sub.shutdown();

    if (pathDirection == right)
      attitude_cmd.euler_rpy.z = path_heading - 45;
    else
      attitude_cmd.euler_rpy.z = path_heading + 45;

    attitude_cmd.euler_rpy.z = master->tslam->KeepHeadingInRange(attitude_cmd.euler_rpy.z);
    master->attitude_pub.publish(attitude_cmd);

    x_average = 100;
    y_average = 100;

    attitude_status_sub = master->nh.subscribe<riptide_msgs::ControlStatusAngular>("/status/controls/angular", 1, &PathMarker::SecondAttitudeStatusCB, this);
    ROS_INFO("Now center, turning to %f", attitude_cmd.euler_rpy.z);

  }
}

// Once we have turned, go
void PathMarker::SecondAttitudeStatusCB(const riptide_msgs::ControlStatusAngular::ConstPtr &status_msg)
{
  heading_average = (heading_average * 99 + abs(status_msg->yaw.error)) / 100;

  if (heading_average < master->yaw_thresh)
  {
    attitude_status_sub.shutdown();

    geometry_msgs::Vector3 msg;
    msg.x = 0;
    msg.y = 0.8;
    msg.z = 0;
    master->linear_accel_pub.publish(msg);

    ROS_INFO("HALF SPEED AHEAD!!!");
    timer = master->nh.createTimer(ros::Duration(2), &PathMarker::Success, this, true);
  }
}

// Once we have gone, be done
void PathMarker::Success(const ros::TimerEvent &event)
{
  geometry_msgs::Vector3 msg;
  msg.x = 0;
  msg.y = 0;
  msg.z = 0;
  master->linear_accel_pub.publish(msg);
  Abort();
  master->tslam->SetEndPos();
  master->StartTask();
}

// Shutdown all active subscribers
void PathMarker::Abort()
{
  PathMarker::Initialize();

  align_cmd.surge_active = false;
  align_cmd.sway_active = false;
  align_cmd.heave_active = false;
  master->alignment_pub.publish(align_cmd);
  ROS_INFO("PathMarker: Aborting");
}
