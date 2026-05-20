#ifndef DDS_HELPER_HPP
#define DDS_HELPER_HPP

#include "dds/dds.h"

static const size_t MAX_TYPENAME_LENGTH = 512;

// Transform the topic name for ROS2
// Append "rt" to the topic name
// eg. /actuation/actuation_command -> rt/actuation/actuation_command
static std::string transformTopicName(const std::string& topic_name) {
    return "rt" + topic_name;
}

// Transform the topic descriptor for ROS2
// Append "dds_" and "_" to the type name
// eg. geometry_msgs::msg::dds_::PoseStamped_ -> geometry_msgs::msg::dds_::PoseStamped__
static dds_topic_descriptor_t transformTopicDescriptor(const dds_topic_descriptor_t* input) {
    static char transformed_typename[MAX_TYPENAME_LENGTH];
    int i = 0, j = 0;
    int lastColonPos = -1;
    dds_topic_descriptor_t output = *input; // Copy other fields first
    
    // Find the last "::" position in the input typename
    while (input->m_typename[i] != '\0' && i < MAX_TYPENAME_LENGTH - 10) { // Prevent buffer overflow during scan
        if (input->m_typename[i] == ':' && input->m_typename[i+1] == ':') {
            lastColonPos = i;
        }
        i++;
    }
    
    // Reset and copy/transform into the new buffer
    i = 0;
    while (input->m_typename[i] != '\0' && j < MAX_TYPENAME_LENGTH - 2) { // Leave space for '_' and '\0'
        // When we reach the last "::", insert "::dds_::"
        if (i == lastColonPos) {
            if (j + 8 >= MAX_TYPENAME_LENGTH) break; // Prevent buffer overflow
            transformed_typename[j++] = ':';
            transformed_typename[j++] = ':';
            transformed_typename[j++] = 'd';
            transformed_typename[j++] = 'd';
            transformed_typename[j++] = 's';
            transformed_typename[j++] = '_';
            transformed_typename[j++] = ':';
            transformed_typename[j++] = ':';
            i += 2; // Skip the original "::"
        } else {
                if (j + 1 >= MAX_TYPENAME_LENGTH) break; // Prevent buffer overflow
            transformed_typename[j++] = input->m_typename[i++];
        }
    }
    
    // Add "_" at the end
    if (j < MAX_TYPENAME_LENGTH - 1) {
            transformed_typename[j++] = '_';
    }
    transformed_typename[j] = '\0'; // Null-terminate

    // Point output.m_typename to the new buffer
    output.m_typename = transformed_typename;
    log_debug("Transformed topic descriptor: %s\n", output.m_typename);
    return output;
}

#endif // DDS_HELPER_HPP
