#ifndef COMMON__DDS_HPP_
#define COMMON__DDS_HPP_

#include <string>
#include <memory>
#include <vector>
#include <dds/dds.h>

#include "common/dds/config.hpp"
#include "common/dds/publisher.hpp"
#include "common/dds/subscriber.hpp"
#include "common/logger/logger.hpp"
using namespace common::logger;

// Diagnostic bracket around dds_create_domain_with_rawconfig() below.
//
// This is a weak DECLARATION with no definition of its own anywhere in the
// shared tree, which is what keeps it free for every platform that does not
// want it: an unresolved weak symbol links as address 0, so the guarded
// call below compiles to a null test that is never taken, and nothing but
// that test is added to any build that does not define it. The
// freertos_x5h target defines it strongly
// (actuation_module/freertos_x5h/x5h_diag.c), so in that one image the mark
// always prints -- it is not behind a build flag that could default off.
//
// Why here specifically: on the X5H safety island the CR52 prints the
// log_info() line below and then goes permanently silent, with the
// "DDS domain created" line never appearing on any boot. The mark records
// a resource snapshot at the last instant before the call that never
// returns, so that reading can be compared against the liveness beacon's
// last line.
//
// What exactly it reports is the implementer's business, not this header's
// -- this is a shared header, also compiled for Zephyr, POSIX and S32Z2,
// and a description written against one platform's implementation is a
// claim the other three cannot keep true. On freertos_x5h it reports the
// registered LAUNCHER task's stack high-water mark (which happens to be
// the calling task on that image, but the hook does not require it) plus
// heap headroom; see actuation_module/freertos_x5h/x5h_diag.h for the
// definitive description.
extern "C" void actuation_diag_mark(const char * tag) __attribute__((weak));

/**
 * @brief Main DDS communication class
 */
class DDS {
public:
    DDS(const std::string& node_name)
    : node_name_(node_name)
    , m_dds_participant(0)
    , m_dds_qos(nullptr)
    {
        // Initialize DDS settings
        struct ddsi_config dds_cfg;
        init_config(dds_cfg);
        log_info("%s -> Creating DDS domain with raw config\n", node_name_.c_str());

        // Create a DDS domain with raw config
        if (actuation_diag_mark != nullptr) {
            actuation_diag_mark("pre-dds_create_domain_with_rawconfig");
        }
        dds_entity_t domain = dds_create_domain_with_rawconfig(CONFIG_DDS_DOMAIN_ID, &dds_cfg);
        if (domain < 0 && domain != DDS_RETCODE_PRECONDITION_NOT_MET) {
            log_error("%s -> dds_create_domain_with_rawconfig: %s\n", 
                node_name_.c_str(), dds_strretcode(-domain));
            exit(-1);
        }
        log_info("%s -> DDS domain created with DOMAIN_ID: %d\n", node_name_.c_str(), CONFIG_DDS_DOMAIN_ID);

        // Create a DDS participant
        m_dds_participant = dds_create_participant(CONFIG_DDS_DOMAIN_ID, NULL, NULL);
        if (m_dds_participant < 0) {
            log_error("%s -> dds_create_participant: %s\n", 
                node_name_.c_str(), dds_strretcode(-m_dds_participant));
            exit(-1);
        }
        log_info("%s -> DDS participant created\n", node_name_.c_str());
        
        // Reliable QoS
        m_dds_qos = dds_create_qos();
        dds_qset_reliability(m_dds_qos, DDS_RELIABILITY_RELIABLE, DDS_MSECS(30));
        // KEEP_LAST depth 1: the controller only ever consumes the most recent
        // sample of each input (trajectory, kinematic state, acceleration, ...),
        // so a deep history just buffers stale samples. The previous depth of 500
        // was fatal with real (>1400 B) Autoware trajectories: each reader history
        // cache could hold 500 * ~8.8 KiB ~= 4.4 MiB of serdata, and because the
        // ~1.7 Hz control cycle consumes slower than the 10 Hz producer, the cache
        // grew unbounded until pvPortMalloc returned NULL and ddsrt_malloc called
        // abort() -> _exit() (silent board death seconds after the first large
        // trajectory arrived). The 10-point bench trajectory (~900 B) stayed under
        // the heap and hid this. Depth 1 bounds every reader/writer cache to one
        // sample regardless of message size.
        dds_qset_history(m_dds_qos, DDS_HISTORY_KEEP_LAST, 1);
        log_info("%s -> DDS QoS created\n", node_name_.c_str());
    }

    ~DDS() 
    {
        // Delete DDS participant (this will clean up all DDS entities)
        dds_return_t rc = dds_delete(m_dds_participant);
        if (rc != DDS_RETCODE_OK) {
            log_error("%s -> dds_delete: %s\n", 
                node_name_.c_str(), dds_strretcode(-rc));
            exit(-1);
        }
        
        // Delete QoS separately
        dds_delete_qos(m_dds_qos);
    }

    // Prevent copying
    DDS(const DDS&) = delete;
    DDS& operator=(const DDS&) = delete;

    /**
     * @brief Create a publisher for a specific message type
     * @tparam MessageT The message type for the publisher
     * @param topic_name The name of the topic
     * @param topic_descriptor The DDS topic descriptor
     * @return std::shared_ptr<Publisher<MessageT>> Smart pointer to the created publisher
     */
    template<typename MessageT>
    std::shared_ptr<Publisher<MessageT>> create_publisher_dds(
        const std::string& topic_name, 
        const dds_topic_descriptor_t* topic_descriptor) 
    {
        return std::make_shared<Publisher<MessageT>>(
            node_name_, topic_name, m_dds_participant, m_dds_qos, topic_descriptor);
    }

    /**
     * @brief Create a subscription for a specific message type
     * @tparam T The message type for the subscription
     * @param topic_name The name of the topic
     * @param topic_descriptor The DDS topic descriptor
     * @param callback The callback function to be called when a message is received
     * @return std::shared_ptr<Subscriber<T>> Pointer to the created subscriber
     */
    template<typename T>
    std::shared_ptr<Subscriber<T>> create_subscription_dds(const std::string& topic_name, 
                            const dds_topic_descriptor_t* topic_descriptor, 
                            callback_subscriber<T> callback, void* arg) 
    {
        try {
            auto subscriber = std::make_shared<Subscriber<T>>(
                node_name_, topic_name, m_dds_participant, m_dds_qos, topic_descriptor, callback, arg);
            
            // Store as ISubscriptionHandler
            subscriptions_.push_back(std::static_pointer_cast<ISubscriptionHandler>(subscriber));
            return subscriber;
        } catch (const std::exception& e) {
            log_error("%s -> create_subscription_dds: %s\n", 
                node_name_.c_str(), e.what());
            return nullptr;
        }
    }

    /**
     * @brief Execute all subscription handlers
     */
    void execute_subscriptions() {
        for (auto& sub_handler_ptr : subscriptions_) {
            if (sub_handler_ptr) {
                sub_handler_ptr->process_next_message();
            }
        }
    }

    /**
     * @brief Check if there are any subscriptions
     * @return true if there are subscriptions, false otherwise
     */
    bool has_subscriptions() {
        return !subscriptions_.empty();
    }

private:
    std::string node_name_;
    dds_entity_t m_dds_participant;
    dds_qos_t* m_dds_qos;
    std::vector<std::shared_ptr<ISubscriptionHandler>> subscriptions_;
};

#endif // COMMON__DDS_HPP_