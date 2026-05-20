#ifndef COMMON__SUBSCRIBER_HPP_
#define COMMON__SUBSCRIBER_HPP_

#include <memory>
#include <string>

#include "common/dds/helper.hpp"
#include "common/logger/logger.hpp"
using namespace common::logger;

class ISubscriptionHandler {
public:
    virtual ~ISubscriptionHandler() = default;
    virtual void process_next_message() = 0;
};

template<typename T>
using callback_subscriber = void (*)(const T* msg, void* arg);

template<typename T>
class Subscriber : public ISubscriptionHandler {
public:
    Subscriber(const std::string& node_name, const std::string& topic_name, 
                dds_entity_t dds_participant, dds_qos_t* dds_qos, 
                const dds_topic_descriptor_t* topic_descriptor,
                callback_subscriber<T> callback, void* arg)
            : node_name_(node_name)
            , topic_name_(topic_name)
            , m_reader_entity(0)
            , callback_(callback)
            , callback_arg_(arg)
    {
        // Manipulate topic name and topic descriptor for ROS2 compatibility
        std::string topic_name_ros2 = transformTopicName(topic_name);
        dds_topic_descriptor_t topic_descriptor_ros2 = transformTopicDescriptor(topic_descriptor);

        // Create a DDS topic
        dds_entity_t topic = dds_create_topic(dds_participant, &topic_descriptor_ros2, 
                                                topic_name_ros2.c_str(), NULL, NULL);
        if (topic < 0) {
            log_error("%s -> dds_create_topic (%s): %s\n", 
                   node_name_.c_str(), topic_name_ros2.c_str(), dds_strretcode(-topic));
            return;
        }

        // Create a DDS reader
        m_reader_entity = dds_create_reader(dds_participant, topic, dds_qos, NULL);
        if (m_reader_entity < 0) {
            log_error("%s -> dds_create_reader (%s): %s\n", 
                   node_name_.c_str(), topic_name_ros2.c_str(), dds_strretcode(-m_reader_entity));
            dds_delete(topic);
            return;
        }

        log_info("%s -> Subscriber created for topic %s\n", node_name_.c_str(), topic_name_ros2.c_str());
    }

    ~Subscriber() {
        if (m_reader_entity != 0) {
            dds_delete(m_reader_entity);
        }
    }

    void process_next_message() override {
        // TODO: Check if this is required
        // Disable preemption for this critical section
        // k_sched_lock();

        int count = 0;
        static void* msg_ptr = nullptr;
        dds_sample_info_t info;

        count = dds_take(m_reader_entity, &msg_ptr, &info, 1, 1);
        if (count == 0) {
            return;
        }
        if (count < 0) {
            if (count != DDS_RETCODE_NO_DATA && count != DDS_RETCODE_TRY_AGAIN) {
                 log_debug("Error: %s -> dds_take failed for topic %s: %s\n", 
                        node_name_.c_str(), topic_name_.c_str(), dds_strretcode(-count));
            }
        }

        if (info.valid_data) {
            T msg = *static_cast<T*>(msg_ptr);
            callback_(&msg, callback_arg_);
        }

        // Re-enable preemption
        // TODO: finetuning threads for maximum network performance
        // k_sched_unlock();
    }

private:
    std::string node_name_;
    std::string topic_name_;
    dds_entity_t m_reader_entity;
    callback_subscriber<T> callback_;
    void* callback_arg_;
};

#endif  // COMMON__SUBSCRIBER_HPP_