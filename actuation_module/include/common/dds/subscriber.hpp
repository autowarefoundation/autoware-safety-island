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
        // Loan-mode take: pass a NULL buffer pointer so CycloneDDS lends us a
        // pointer into its own internal sample storage, then hand that storage
        // back with dds_return_loan() once we are done with it.
        //
        // The buffer pointer MUST be re-initialised to NULL on every call. If it
        // is non-NULL, dds_take() treats it as a caller-owned buffer and
        // deserialises the sample into it instead of lending one; for message
        // types with heap sequences (e.g. Trajectory.points) that writes the
        // sequence through a stale/aliased pointer and corrupts the heap. The
        // previous implementation used a `static void* msg_ptr` that was never
        // returned, so the first take loaned a buffer and every subsequent take
        // reused that stale loaned pointer -> silent heap corruption that only
        // manifested once a variable-length topic (trajectory) was received.
        void* msg_ptr = nullptr;
        dds_sample_info_t info;

        int count = dds_take(m_reader_entity, &msg_ptr, &info, 1, 1);
        if (count < 0) {
            if (count != DDS_RETCODE_NO_DATA && count != DDS_RETCODE_TRY_AGAIN) {
                 log_debug("Error: %s -> dds_take failed for topic %s: %s\n",
                        node_name_.c_str(), topic_name_.c_str(), dds_strretcode(-count));
            }
            return;
        }
        if (count == 0) {
            return;
        }

        if (info.valid_data) {
            // The callback deep-copies anything it needs (e.g. callbackTrajectory
            // builds a std::vector from points._buffer) before we return the loan.
            T msg = *static_cast<T*>(msg_ptr);
            callback_(&msg, callback_arg_);
        }

        // Return the loaned sample storage to CycloneDDS, matched 1:1 with the take.
        dds_return_loan(m_reader_entity, &msg_ptr, count);
    }

private:
    std::string node_name_;
    std::string topic_name_;
    dds_entity_t m_reader_entity;
    callback_subscriber<T> callback_;
    void* callback_arg_;
};

#endif  // COMMON__SUBSCRIBER_HPP_