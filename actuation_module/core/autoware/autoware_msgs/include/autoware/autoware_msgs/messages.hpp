#ifndef COMMON__DDS__MESSAGES_HPP_
#define COMMON__DDS__MESSAGES_HPP_

#include <vector>

#include "SteeringReport.h"
#include "Trajectory.h"
#include "Odometry.h"
#include "AccelWithCovarianceStamped.h"
#include "OperationModeState.h"
#include "Control.h"
#include "Longitudinal.h"
#include "Lateral.h"
#include "PoseStamped.h"
#include "Float64Stamped.h"
#include "TrajectoryPoint.h"
#include "Point.h"
#include "Pose.h"
#include "PoseWithCovarianceStamped.h"
#include "Vector3.h"
#include "Quaternion.h"
#include "Transform.h"
#include "TransformStamped.h"
#include "Float32MultiArrayStamped.h"
#include "Float32Stamped.h"
#include "Twist.h"

using Float32MultiArrayStampedMsg = tier4_debug_msgs_msg_Float32MultiArrayStamped;
using Float32StampedMsg = tier4_debug_msgs_msg_Float32Stamped;
using TwistMsg = geometry_msgs_msg_Twist;
using LateralMsg = autoware_control_msgs_msg_Lateral;
using LongitudinalMsg = autoware_control_msgs_msg_Longitudinal;
using Vector3Msg = geometry_msgs_msg_Vector3;
using QuaternionMsg = geometry_msgs_msg_Quaternion;
using PoseWithCovarianceStampedMsg = geometry_msgs_msg_PoseWithCovarianceStamped;
using TransformMsg = geometry_msgs_msg_Transform;
using TransformStampedMsg = geometry_msgs_msg_TransformStamped;
using PointMsg = geometry_msgs_msg_Point;
using PoseMsg = geometry_msgs_msg_Pose;
using TrajectoryMsg_Raw = autoware_planning_msgs_msg_Trajectory;
using AccelerationMsg = geometry_msgs_msg_AccelWithCovarianceStamped;
using OperationModeStateMsg = autoware_adapi_v1_msgs_msg_OperationModeState;
using Float64StampedMsg = tier4_debug_msgs_msg_Float64Stamped;
using ControlMsg = autoware_control_msgs_msg_Control;
using OdometryMsg = nav_msgs_msg_Odometry;
using SteeringReportMsg = autoware_vehicle_msgs_msg_SteeringReport;
using AccelWithCovarianceStampedMsg = geometry_msgs_msg_AccelWithCovarianceStamped;
using TrajectoryPointMsg = autoware_planning_msgs_msg_TrajectoryPoint;
using PoseStampedMsg = geometry_msgs_msg_PoseStamped;

#define OPERATION_MODE_STATE_UNKNOWN autoware_adapi_v1_msgs_msg_OperationModeState_Constants_UNKNOWN
#define OPERATION_MODE_STATE_STOP autoware_adapi_v1_msgs_msg_OperationModeState_Constants_STOP
#define OPERATION_MODE_STATE_AUTONOMOUS autoware_adapi_v1_msgs_msg_OperationModeState_Constants_AUTONOMOUS
#define OPERATION_MODE_STATE_LOCAL autoware_adapi_v1_msgs_msg_OperationModeState_Constants_LOCAL
#define OPERATION_MODE_STATE_REMOTE autoware_adapi_v1_msgs_msg_OperationModeState_Constants_REMOTE

// TODO: This is a temporary solution to convert the raw DDS sequence to a vector
typedef struct TrajectoryMsg
{
    struct std_msgs_msg_Header header;
    std::vector<autoware_planning_msgs_msg_TrajectoryPoint> points;
    
    TrajectoryMsg() { points.reserve(250); }  // Reserves capacity for 250 elements but vector is still empty
    
    TrajectoryMsg(const TrajectoryMsg_Raw* msg) {
        header = msg->header;
        points.reserve(250);  // Reserve capacity first
        points.assign(msg->points._buffer, msg->points._buffer + msg->points._length);
    }
} TrajectoryMsg;

#endif // COMMON__DDS__MESSAGES_HPP_