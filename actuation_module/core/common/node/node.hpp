#ifndef COMMON__NODE_HPP_
#define COMMON__NODE_HPP_

// C++ Standard Library
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <variant>
#include <optional>
#include <pthread.h>
#include <cstring>
#include <functional>

// Project headers
#include "common/dds/dds.hpp"
#include "common/node/timer.hpp"
#include "common/logger/logger.hpp"
using namespace common::logger;

using param_type = std::variant<bool,
                              int,
                              int64_t,
                              double,
                              std::string,
                              std::vector<bool>,
                              std::vector<int>,
                              std::vector<int64_t>,
                              std::vector<double>,
                              std::vector<std::string>,
                              std::vector<uint8_t>>;

/**
 * @brief Node base class implementation to replicate ROS2 nodes within the Zephyr RTOS
 * 
 * This class provides a minimal implementation of a ROS2-like node for Zephyr RTOS,
 * supporting message-based communication through DDS. It includes thread management,
 * timers, and parameter storage.
 */
class Node {
public:

    /**
     * @brief Construct a new Node object
     * @param node_name Name of the node
     */
    Node(const std::string& node_name, void* stack_area, size_t stack_size)
    : node_name_(node_name)
    , dds_(node_name)
    {
        pthread_attr_init(&main_thread_attr_);
        int ret = pthread_attr_setstack(&main_thread_attr_, stack_area, stack_size);
        if (ret != 0) {
            log_error("%s -> pthread_attr_setstack failed for main thread: %s. Exiting.\n", 
                      node_name_.c_str(), strerror(ret));
            std::exit(1);
        }
        timer_ = std::make_unique<Timer>(node_name_);
    }
    
    /**
     * @brief Destructor
     */
    ~Node() {
        stop();
        pthread_attr_destroy(&main_thread_attr_);
    }

    /**
     * @brief Spin the node thread
     * @return 0 on success, negative value on failure
     */
    int spin() {
        return pthread_create(&main_thread_, &main_thread_attr_, main_thread_entry_, this);
    }

    /**
     * @brief Wait for the node thread to complete
     */
    void wait_for_completion() {
        pthread_join(main_thread_, nullptr);
    }
    
    /**
     * @brief Stop the node thread
     */
    void stop() {
        pthread_cancel(main_thread_);
        pthread_join(main_thread_, nullptr);
    }

    /** 
     * @brief Create a publisher for a topic
     * @param topic_name Topic name
     * @param topic_descriptor Topic descriptor 
     * @return Publisher<MessageT>* Pointer to the publisher
     */
    template<typename MessageT>
    std::shared_ptr<Publisher<MessageT>> create_publisher(const std::string& topic_name, 
                                                        const dds_topic_descriptor_t* topic_descriptor) {
        return dds_.create_publisher_dds<MessageT>(topic_name, topic_descriptor);
    }

    /**
     * @brief Create a subscription for a topic
     * @param topic_name Topic name
     * @param topic_descriptor Topic descriptor 
     * @param callback Callback function
     * @param arg Callback user argument
     * @return bool true on success, false on failure
     */
    template<typename T>
    bool create_subscription(const std::string& topic_name, 
                           const dds_topic_descriptor_t* topic_descriptor, 
                           callback_subscriber<T> callback, void* arg) {

        auto subscription = dds_.create_subscription_dds<T>(topic_name, topic_descriptor, callback, arg);
        return subscription != nullptr;
    }

    /**
     * @brief Declare a parameter with a name and default value
     * @tparam ParamT Type of the parameter
     * @param name Name of the parameter
     * @param default_value Default value for the parameter
     * @return The current value of the parameter (default if not previously set)
     */
    template<typename ParamT>
    ParamT declare_parameter(const std::string& name, const ParamT& default_value = ParamT{}) {
        static_assert(std::is_constructible_v<param_type, ParamT>, 
                     "Parameter type must be one of the supported types in param_type variant");
        
        auto it = parameters_map_.find(name);
        if (it == parameters_map_.end()) {
            parameters_map_[name] = default_value;
            return default_value;
        } else {
            try {
                ParamT value = std::get<ParamT>(it->second);
                return value;
            } catch (const std::bad_variant_access&) {
                log_warn("Warning: Parameter '%s' exists with different type. Not overwriting.\n", name.c_str());
                return default_value;
            }
        }
    }

    /**
     * @brief Get a parameter value by name
     * @tparam ParamT Type of the parameter
     * @param name Name of the parameter
     * @return The parameter value or default-constructed value if not found
     */
    template<typename ParamT>
    ParamT get_parameter(const std::string& name) const {
        static_assert(std::is_constructible_v<param_type, ParamT>, 
                     "Parameter type must be one of the supported types in param_type variant");
        
        auto param = search_parameter_(name);
        if (param) {
            try {
                return std::get<ParamT>(*param);
            } catch (const std::bad_variant_access&) {
                log_warn("%s -> get_parameter failed-1: %s\n", node_name_.c_str(), name.c_str());
                return ParamT{};
            }
        }
        return ParamT{};
    }

    /**
     * @brief Set a parameter value by name
     * @tparam ParamT Type of the parameter
     * @param name Name of the parameter
     * @param value Value to set
     * @return true if parameter was set, false if parameter doesn't exist
     */
    template<typename ParamT>
    bool set_parameter(const std::string& name, const ParamT& value) {
        static_assert(std::is_constructible_v<param_type, ParamT>, 
                     "Parameter type must be one of the supported types in param_type variant");
        
        auto it = parameters_map_.find(name);
        if (it != parameters_map_.end()) {
            // Check if the new value's type matches the existing parameter's type
            if (it->second.index() == param_type(value).index()) {
                it->second = value;
                return true;
            }
            log_warn("%s -> Cannot set parameter '%s' with different type\n", node_name_.c_str(), name.c_str());
            return false;
        }
        
        return false;
    }

    /**
     * @brief Check if a parameter exists
     * @param name Name of the parameter
     * @return true if parameter exists, false otherwise
     */
    inline bool has_parameter(const std::string& name) const {
        bool exists = parameters_map_.find(name) != parameters_map_.end();
        return exists;
    }

    /**
     * @brief Get the name of the node
     * @return std::string Name of the node
     */
    inline std::string get_name() const {
        return node_name_;
    }

    /**
     * @brief Create a timer
     * @param period_ms Period in milliseconds
     * @param callback Callback function
     * @return true if timer was created, false otherwise
     */
    bool create_timer(uint32_t period_ms, std::function<void()> callback) {
        if (!timer_) {
            log_error("%s -> Timer object not initialized!\n", node_name_.c_str());
            return false;
        }
        return timer_->start(period_ms, callback);
    }

    /**
     * @brief Stop the timer
     */
    void stop_timer() {
        if (timer_) {
            timer_->stop();
            log_info("%s -> Timer stopped via Node request.\n", node_name_.c_str());
        } else {
            log_warn("%s -> Attempted to stop a non-initialized timer.\n", node_name_.c_str());
        }
    }

private:
    // Node
    std::string node_name_;

    // Parameter storage
    std::unordered_map<std::string, param_type> parameters_map_;

    // DDS
    DDS dds_;
    
    // Timer
    std::unique_ptr<Timer> timer_;

    // Main thread
    pthread_t main_thread_;
    pthread_attr_t main_thread_attr_;

    static void* main_thread_entry_(void* arg) {
        Node* node = static_cast<Node*>(arg);

        while (1) {
            if (node->dds_.has_subscriptions()) {   // Check and execute the subscriptions callbacks
                node->dds_.execute_subscriptions();
            }

            if (node->timer_) {
                node->timer_->execute();
            }
            usleep(1000);   // 1ms, if nothing else yields  // TODO: TUNE THIS
        }
        return nullptr; // Should never reach here
    }

    std::optional<param_type> search_parameter_(const std::string& name) const {
        auto it = parameters_map_.find(name);
        std::optional<param_type> result = std::nullopt;
        if (it != parameters_map_.end()) {
            result = it->second;
        }
        
        return result;
    }
};

#endif  // COMMON__NODE_HPP_
