// SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES
// Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef NVBLOX_ROS__NVBLOX_NODE_HPP_
#define NVBLOX_ROS__NVBLOX_NODE_HPP_

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <message_filters/subscriber.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <message_filters/sync_policies/exact_time.h>
#include <message_filters/synchronizer.h>
#include <nvblox_msgs/FilePath.h>
#include <ros/node_handle.h>
#include <ros/publisher.h>
#include <ros/ros.h>
#include <ros/service_server.h>
#include <ros/time.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/PointCloud2.h>
#include <std_msgs/String.h>
#include <visualization_msgs/Marker.h>

#include <nvblox/nvblox.h>

#include "nvblox_ros/conversions/esdf_slice_conversions.hpp"
#include "nvblox_ros/conversions/image_conversions.hpp"
#include "nvblox_ros/conversions/layer_conversions.hpp"
#include "nvblox_ros/conversions/mesh_conversions.hpp"
#include "nvblox_ros/conversions/pointcloud_conversions.hpp"
#include "nvblox_ros/mapper_initialization.hpp"
#include "nvblox_ros/transformer.hpp"

namespace nvblox {

class NvbloxNode {
 public:
  explicit NvbloxNode(ros::NodeHandle& nh, ros::NodeHandle& nh_private);
  virtual ~NvbloxNode();

  // Setup. These are called by the constructor.
  void getParameters();
  void subscribeToTopics();
  void advertiseTopics();
  void advertiseServices();
  void setupTimers();

  // Callback functions. These just stick images in a queue.
  void depthImageCallback(
      const sensor_msgs::ImageConstPtr& depth_img_ptr,
      const sensor_msgs::CameraInfo::ConstPtr& camera_info_msg);
  void depthImageCallback2(
      const sensor_msgs::ImageConstPtr& depth_img_ptr,
      const sensor_msgs::CameraInfo::ConstPtr& camera_info_msg);
  void depthImageCallback3(
      const sensor_msgs::ImageConstPtr& depth_img_ptr,
      const sensor_msgs::CameraInfo::ConstPtr& camera_info_msg);
  void depthImageCallback4(
      const sensor_msgs::ImageConstPtr& depth_img_ptr,
      const sensor_msgs::CameraInfo::ConstPtr& camera_info_msg);

  void colorImageCallback(
      const sensor_msgs::ImageConstPtr& color_img_ptr,
      const sensor_msgs::CameraInfo::ConstPtr& color_info_msg);
  void colorImageCallback2(
      const sensor_msgs::ImageConstPtr& color_img_ptr,
      const sensor_msgs::CameraInfo::ConstPtr& color_info_msg);

  void pointcloudCallback(const sensor_msgs::PointCloud2::ConstPtr pointcloud);

  bool savePly(nvblox_msgs::FilePath::Request& request,
               nvblox_msgs::FilePath::Response& response);
  bool saveMap(nvblox_msgs::FilePath::Request& request,
               nvblox_msgs::FilePath::Response& response);
  bool loadMap(nvblox_msgs::FilePath::Request& request,
               nvblox_msgs::FilePath::Response& response);

  // Does whatever processing there is to be done, depending on what
  // transforms are available.
  virtual void processDepthQueue(const ros::TimerEvent& /*event*/);
  virtual void processColorQueue(const ros::TimerEvent& /*event*/);
  virtual void processPointcloudQueue(const ros::TimerEvent& /*event*/);
  virtual void processEsdf(const ros::TimerEvent& /*event*/);
  virtual void processMesh(const ros::TimerEvent& /*event*/);

  // Alternative callbacks to using TF.
  void transformCallback(
      const geometry_msgs::TransformStampedConstPtr& transform_msg);
  void poseCallback(const geometry_msgs::PoseStampedConstPtr& transform_msg);

  // Publish data on fixed frequency
  void publishOccupancyPointcloud(const ros::TimerEvent& /*event*/);

  // Process data
  virtual bool processDepthImage(
      const std::pair<sensor_msgs::ImageConstPtr,
                      sensor_msgs::CameraInfo::ConstPtr>& depth_camera_pair);
  virtual bool processColorImage(
      const std::pair<sensor_msgs::ImageConstPtr,
                      sensor_msgs::CameraInfo::ConstPtr>& color_camera_pair);
  virtual bool processLidarPointcloud(
      const sensor_msgs::PointCloud2::ConstPtr& pointcloud_ptr);

  bool canTransform(const std_msgs::Header& header);

  void publishSlicePlane(const ros::Time& timestamp, const Transform& T_L_C);

 protected:
  // Map clearing
  void clearMapOutsideOfRadiusOfLastKnownPose(const ros::TimerEvent& /*event*/);

  /// Used by callbacks (internally) to add messages to queues.
  /// @tparam MessageType The type of the Message stored by the queue.
  /// @param message Message to be added to the queue.
  /// @param queue_ptr Queue where to add the message.
  /// @param queue_mutex_ptr Mutex protecting the queue.
  template <typename MessageType>
  void pushMessageOntoQueue(MessageType message,
                            std::deque<MessageType>* queue_ptr,
                            std::mutex* queue_mutex_ptr);

  // Used internally to unify processing of queues that process a message and a
  // matching transform.
  template <typename MessageType>
  using ProcessMessageCallback = std::function<bool(const MessageType&)>;
  template <typename MessageType>
  using MessageReadyCallback = std::function<bool(const MessageType&)>;

  /// Processes a queue of messages by detecting if they're ready and then
  /// passing them to a callback.
  /// @tparam MessageType The type of the messages in the queue.
  /// @param queue_ptr Queue of messages to process.
  /// @param queue_mutex_ptr Mutex protecting the queue.
  /// @param message_ready_check Callback called on each message to check if
  /// it's ready to be processed
  /// @param callback Callback to process each ready message.
  template <typename MessageType>
  void processMessageQueue(
      std::deque<MessageType>* queue_ptr, std::mutex* queue_mutex_ptr,
      MessageReadyCallback<MessageType> message_ready_check,
      ProcessMessageCallback<MessageType> callback);

  // Check if interval between current stamp
  bool isUpdateTooFrequent(const ros::Time& current_stamp,
                           const ros::Time& last_update_stamp,
                           float max_update_rate_hz);

  template <typename MessageType>
  void limitQueueSizeByDeletingOldestMessages(
      const size_t max_num_messages, const std::string& queue_name,
      std::deque<MessageType>* queue_ptr, std::mutex* queue_mutex_ptr);

  // ROS publishers and subscribers and misc
  // Node Handles
  ros::NodeHandle nh_;
  ros::NodeHandle nh_private_;

  // Callback queue for processing. All subs run on default thread but heavy
  // lifting is on the processing thread instead. This keeps pub/sub pretty
  // clean (main thread) and only the timers have to be moved to this.
  ros::CallbackQueue processing_queue_;
  ros::AsyncSpinner processing_spinner_;

  // Transformer to handle... everything, let's be honest.
  Transformer transformer_;

  // Time Sync
  typedef message_filters::sync_policies::ExactTime<sensor_msgs::Image,
                                                    sensor_msgs::CameraInfo>
      time_policy_t;

  // Depth sub.
  std::shared_ptr<message_filters::Synchronizer<time_policy_t>> timesync_depth_;
  std::shared_ptr<message_filters::Synchronizer<time_policy_t>> timesync_depth2_;
std::shared_ptr<message_filters::Synchronizer<time_policy_t>> timesync_depth3_;
  std::shared_ptr<message_filters::Synchronizer<time_policy_t>> timesync_depth4_;
  message_filters::Subscriber<sensor_msgs::Image> depth_sub_;
  message_filters::Subscriber<sensor_msgs::CameraInfo> depth_camera_info_sub_;

  message_filters::Subscriber<sensor_msgs::Image> depth_sub2_;
  message_filters::Subscriber<sensor_msgs::CameraInfo> depth_camera_info_sub2_;

  message_filters::Subscriber<sensor_msgs::Image> depth_sub3_;
  message_filters::Subscriber<sensor_msgs::CameraInfo> depth_camera_info_sub3_;

  message_filters::Subscriber<sensor_msgs::Image> depth_sub4_;
  message_filters::Subscriber<sensor_msgs::CameraInfo> depth_camera_info_sub4_;

  // Color sub
  std::shared_ptr<message_filters::Synchronizer<time_policy_t>> timesync_color_;
  std::shared_ptr<message_filters::Synchronizer<time_policy_t>> timesync_color2_;
  message_filters::Subscriber<sensor_msgs::Image> color_sub_;
  message_filters::Subscriber<sensor_msgs::CameraInfo> color_camera_info_sub_;

  message_filters::Subscriber<sensor_msgs::Image> color_sub2_;
  message_filters::Subscriber<sensor_msgs::CameraInfo> color_camera_info_sub2_;

  // Pointcloud sub.
  ros::Subscriber pointcloud_sub_;

  // Optional transform subs.
  ros::Subscriber transform_sub_;
  ros::Subscriber pose_sub_;

  // Publishers
  ros::Publisher mesh_publisher_;
  ros::Publisher esdf_pointcloud_publisher_;
  ros::Publisher occupancy_publisher_;
  ros::Publisher map_slice_publisher_;
  ros::Publisher slice_bounds_publisher_;
  ros::Publisher mesh_marker_publisher_;

  // Services.
  ros::ServiceServer save_ply_service_;
  ros::ServiceServer save_map_service_;
  ros::ServiceServer load_map_service_;

  // Timers.
  ros::Timer depth_processing_timer_;
  ros::Timer color_processing_timer_;
  ros::Timer pointcloud_processing_timer_;
  ros::Timer occupancy_publishing_timer_;
  ros::Timer esdf_processing_timer_;
  ros::Timer mesh_processing_timer_;
  ros::Timer clear_outside_radius_timer_;

  // ROS & nvblox settings
  float voxel_size_ = 0.05f;
  bool esdf_2d_ = false;
  bool esdf_distance_slice_ = true;
  float esdf_slice_height_ = 1.0f;
  ProjectiveLayerType static_projective_layer_type_ =
      ProjectiveLayerType::kTsdf;
  bool is_realsense_data_ = false;

  // Toggle parameters
  bool use_depth_ = true;
  bool use_lidar_ = false;
  bool use_color_ = true;
  bool compute_esdf_ = true;
  bool compute_mesh_ = true;

  // LIDAR settings, defaults for Velodyne VLP16
  int lidar_width_ = 1800;
  int lidar_height_ = 16;
  const float deg_to_rad = M_PI / 180.0;
  float lidar_vertical_fov_deg_ = 30.0;

  // Used for ESDF slicing. Everything between min and max height will be
  // compressed to a single 2D level (if esdf_2d_ enabled), output at
  // esdf_slice_height_.
  float esdf_2d_min_height_ = 0.0f;
  float esdf_2d_max_height_ = 1.0f;

  // Slice visualization params
  std::string slice_visualization_attachment_frame_id_ = "base_link";
  float slice_visualization_side_length_ = 10.0f;

  // ROS settings & update throttles
  std::string global_frame_ = "map";
  /// Pose frame to use if using transform topics.
  std::string pose_frame_ = "base_link";
  float max_depth_update_hz_ = 10.0f;
  float max_color_update_hz_ = 5.0f;
  float max_lidar_update_hz_ = 10.0f;
  float mesh_update_rate_hz_ = 5.0f;
  float esdf_update_rate_hz_ = 2.0f;
  float occupancy_publication_rate_hz_ = 2.0f;

  /// Specifies what rate to poll the color & depth updates at.
  /// Will exit as no-op if no new images are in the queue so it is safe to
  /// set this higher than you expect images to come in at.
  float max_poll_rate_hz_ = 100.0f;

  /// How many messages to store in the sensor messages queues (depth, color,
  /// lidar) before deleting oldest messages.
  int maximum_sensor_message_queue_length_ = 10;

  /// Map clearing params
  /// Note that values <=0.0 indicate that no clearing is performed.
  float map_clearing_radius_m_ = -1.0f;
  std::string map_clearing_frame_id_ = "lidar";
  float clear_outside_radius_rate_hz_ = 1.0f;

  // Mapper
  // Holds the map layers and their associated integrators
  // - TsdfLayer, ColorLayer, EsdfLayer, MeshLayer
  std::shared_ptr<Mapper> mapper_;

  // The most important part: the ROS converter. Just holds buffers as state.
  conversions::LayerConverter layer_converter_;
  conversions::PointcloudConverter pointcloud_converter_;
  conversions::EsdfSliceConverter esdf_slice_converter_;

  // Caches for GPU images
  ColorImage color_image_;
  DepthImage depth_image_;
  DepthImage pointcloud_image_;

  // State for integrators running at various speeds.
  ros::Time last_depth_update_time_;
  ros::Time last_color_update_time_;
  ros::Time last_lidar_update_time_;

  // Cache the last known number of subscribers.
  size_t mesh_subscriber_count_ = 0;

  // Image queues.
  std::deque<
      std::pair<sensor_msgs::ImageConstPtr, sensor_msgs::CameraInfo::ConstPtr>>
      depth_image_queue_;

  std::deque<
      std::pair<sensor_msgs::ImageConstPtr, sensor_msgs::CameraInfo::ConstPtr>>
      color_image_queue_;

  std::deque<sensor_msgs::PointCloud2::ConstPtr> pointcloud_queue_;

  // Image queue mutexes.
  std::mutex depth_queue_mutex_;
  std::mutex color_queue_mutex_;
  std::mutex pointcloud_queue_mutex_;
  // Safety check for only touching the map with one thread at a time.
  std::mutex map_mutex_;

  // Keeps track of the mesh blocks deleted such that we can publish them for
  // deletion in the rviz plugin
  Index3DSet mesh_blocks_deleted_;
};

}  // namespace nvblox

#include "nvblox_ros/impl/nvblox_node_impl.hpp"

#endif  // NVBLOX_ROS__NVBLOX_NODE_HPP_
