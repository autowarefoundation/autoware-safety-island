#ifndef DDS_HELPER_HPP
#define DDS_HELPER_HPP

#include <algorithm>
#include <cstddef>
#include <string>

#include "dds/dds.h"
#include "common/logger/logger.hpp"
using namespace common::logger;

static const size_t MAX_TYPENAME_LENGTH = 512;

// Transform the topic name for ROS2
// Append "rt" to the topic name
// eg. /actuation/actuation_command -> rt/actuation/actuation_command
static std::string transformTopicName(const std::string& topic_name) {
    return "rt" + topic_name;
}

// Transform the topic descriptor for ROS2
// Insert "dds_::" before the type's own name and append "_"
// eg. geometry_msgs::msg::PoseStamped -> geometry_msgs::msg::dds_::PoseStamped_
//
// The returned descriptor points at a static buffer that the next call
// overwrites: pass it straight to dds_create_topic(), which copies the name.
static dds_topic_descriptor_t transformTopicDescriptor(const dds_topic_descriptor_t* input) {
    static char transformed_typename[MAX_TYPENAME_LENGTH];

    std::string name(input->m_typename);
    const std::size_t last_scope = name.rfind("::");
    if (last_scope != std::string::npos) {
        name.replace(last_scope, 2, "::dds_::");
    }
    name += '_';

    const std::size_t length = std::min(name.size(), MAX_TYPENAME_LENGTH - 1);
    name.copy(transformed_typename, length);
    transformed_typename[length] = '\0';

    dds_topic_descriptor_t output = *input;
    output.m_typename = transformed_typename;
    log_debug("Transformed topic descriptor: %s\n", output.m_typename);
    return output;
}

#endif // DDS_HELPER_HPP
