/**
 * Copyright (C) 2023, Proyectos y Sistemas de Mantenimiento SL (eProsima)
 *
 * This program is commercial software licensed under the terms of the
 * eProsima Software License Agreement Rev 03 (the "License")
 *
 * You may obtain a copy of the License at
 * https://www.eprosima.com/licenses/LICENSE-REV03
 *
 */

#include <safedds/transport.hpp>

#include <safedds/dds/DomainParticipantFactory.hpp>
#include <safedds/dds/TypedDataReader.hpp>
#include <safedds/dds/TypedDataWriter.hpp>

#include "idl/GetControlValueReq.hpp"
#include "idl/GetControlValueRes.hpp"

#include <iostream>
#include <functional>
#include <string>
#include <thread>
#include <chrono>

using namespace eprosima;
using namespace eprosima::safedds;
using namespace eprosima::safedds::dds;

#define CHECK_ENTITY_CREATION(entity) \
    if (entity == nullptr){ \
        std::cerr << "Error creating " << #entity << std::endl; \
        return 1; \
    }

struct DomainParticipantCallbacks :
    public DomainParticipantListener
{
    void on_subscription_matched(
            DataReader& reader,
            const dds::SubscriptionMatchedStatus& /* info */) noexcept override
    {
        std::cout << "Subscriber matched " << reader.get_topicdescription().get_name().const_string_data() << std::endl;
    }

    void on_publication_matched(
            DataWriter& writer,
            const dds::PublicationMatchedStatus& /* info */) noexcept override
    {
        std::cout << "Publisher matched " << writer.get_topic().get_name().const_string_data() << std::endl;
    }

};

template<typename RequestTS, typename ResponseTS>
class ServiceClient :
    private DataReaderListener
{
public:

    using RequestId = datacentric::SampleKey;

    using CallBackType = std::function<void (const RequestId&, const typename ResponseTS::DataType&)>;

    template<class TS>
    struct ExtendedTypedDataWriter :
        public TypedDataWriter<TS>
    {
        using TypedDataWriter<TS>::extended_write;
    };

    ServiceClient(
            DomainParticipant& participant,
            const std::string& service_name,
            const std::string& service_type_name,
            CallBackType callback
            )
        : participant_(participant)
        , callback_(callback)
    {
        request_topic_name_ =
                memory::container::StaticString256((std::string("rq/") + service_name +
                        std::string("Request")).c_str());
        response_topic_name_ =
                memory::container::StaticString256((std::string("rr/") + service_name +  std::string("Reply")).c_str());

        request_type_name_ = memory::container::StaticString256((service_type_name + std::string("Request_")).c_str());
        response_type_name_ =
                memory::container::StaticString256((service_type_name + std::string("Response_")).c_str());

        request_type_support_.register_type(participant, request_type_name_);
        response_type_support_.register_type(participant, response_type_name_);

        request_topic_ = participant.create_topic(request_topic_name_, request_type_name_, TopicQos{}, nullptr,
                        NONE_STATUS_MASK);
        response_topic_ = participant.create_topic(response_topic_name_, response_type_name_, TopicQos{}, nullptr,
                        NONE_STATUS_MASK);

        publisher_ = participant.create_publisher(PublisherQos{}, nullptr, NONE_STATUS_MASK);
        subscriber_ = participant.create_subscriber(SubscriberQos{}, nullptr, NONE_STATUS_MASK);

        request_writer_ = publisher_->create_datawriter(*request_topic_, DataWriterQos{}, nullptr, NONE_STATUS_MASK);
        response_reader_ =
                subscriber_->create_datareader(*response_topic_, DataReaderQos{}, this, DATA_AVAILABLE_STATUS);

        request_topic_->enable();
        response_topic_->enable();
        publisher_->enable();
        subscriber_->enable();
        request_writer_->enable();
        response_reader_->enable();
    }

    RequestId send_request(
            const typename RequestTS::DataType& request)
    {
        auto typed_writer = TypedDataWriter<RequestTS>::downcast(*request_writer_);
        ExtendedTypedDataWriter<RequestTS>* extended_typed_writer =
                reinterpret_cast<ExtendedTypedDataWriter<RequestTS>*>(typed_writer);

        datacentric::SampleKey key = {};

        extended_typed_writer->extended_write(request, HANDLE_NIL, get_platform().get_current_timepoint(), nullptr,
                key);

        return key;
    }

    void on_data_available(
            DataReader& reader) noexcept override
    {
        auto typed_reader = TypedDataReader<ResponseTS>::downcast(reader);

        typename ResponseTS::DataType response;
        SampleInfo info;

        while (typed_reader->take_next_sample(response, info) == dds::ReturnCode::OK)
        {
            if (info.valid_data && info.extension.related_sample_identity != datacentric::SAMPLEKEY_INVALID)
            {
                callback_(info.extension.related_sample_identity, response);
            }
        }
    }

private:

    DomainParticipant& participant_;
    CallBackType callback_;

    memory::container::StaticString256 request_topic_name_;
    memory::container::StaticString256 response_topic_name_;
    memory::container::StaticString256 request_type_name_;
    memory::container::StaticString256 response_type_name_;

    Topic* request_topic_;
    Topic* response_topic_;

    Publisher* publisher_;
    Subscriber* subscriber_;

    DataWriter* request_writer_;
    DataReader* response_reader_;

    RequestTS request_type_support_;
    ResponseTS response_type_support_;
};

int main(
        int /* argc */,
        char** /* argv */)
{
    // QoS
    // Select a unicast random port in range [8000, 8100]
    srand(time(nullptr));
    int random_port = rand() % 100 + 8000;

    DomainParticipantQos participant_qos{};
    participant_qos.wire_protocol_qos().announced_locator = transport::Locator::from_ipv4({127, 0, 0, 1}, random_port);

    // Participant
    DomainParticipantFactory factory;
    DomainId domain_id = 0;
    DomainParticipantCallbacks participant_listener;
    DomainParticipant* participant = factory.create_participant(domain_id, participant_qos, &participant_listener,
                    PUBLICATION_MATCHED_STATUS |
                    SUBSCRIPTION_MATCHED_STATUS);
    CHECK_ENTITY_CREATION(participant);

    // Create service server
    using GetControlValueClient = ServiceClient<GetControlValueReqTypeSupport, GetControlValueResTypeSupport>;
    GetControlValueClient service_client(
        *participant,
        "d555_poc_get_control_value",
        "GenControlValueSrv_",
        [](const GetControlValueClient::RequestId& request_id,
        const typename GetControlValueResTypeSupport::DataType& response) -> void
        {
            std::cout << "Received response (" << request_id.sequence_number.to_int64() << ") : " << response.value <<
                std::endl;
        });

    // Enable entities
    bool enabled = true;
    enabled &= dds::ReturnCode::OK == participant->enable();

    if (!enabled)
    {
        std::cout << "Error enabling entities" << std::endl;
    }

    // Spin entities
    execution::Timer request_timer({0, 100 * execution::NS_PER_MS});
    execution::ISpinnable* executor = factory.create_default_executor();

    while (1)
    {
        while (executor->has_pending_work())
        {
            executor->spin(execution::TIME_ZERO);
        }

        if (request_timer.is_triggered_and_reset())
        {
            static GetControlValueReqTypeSupport::DataType request = {};
            request.control_name = "rgb_module.exposure";
            std::cout << "Getting Control: " << request.control_name << std::endl;

            GetControlValueClient::RequestId id = service_client.send_request(request);

            std::cout << "Sent request (" <<  id.sequence_number.to_int64() << ") : " << request.control_name << std::endl;
        }

        execution::TimePoint next_work_timepoint = executor->get_next_work_timepoint();
        next_work_timepoint = execution::TimePoint::min(next_work_timepoint, request_timer.next_trigger());

        // Block on transport until next work timepoint or data is received
        executor->spin(next_work_timepoint);

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return 0;
}
