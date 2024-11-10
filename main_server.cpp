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
class ServiceServer :
    private DataReaderListener
{
public:

    using CallBackType = std::function<typename ResponseTS::DataType (const typename RequestTS::DataType&)>;

    template<class TS>
    struct ExtendedTypedDataWriter :
        public TypedDataWriter<TS>
    {
        using TypedDataWriter<TS>::extended_write;
    };

    ServiceServer(
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

        DataWriterQos datawriter_qos{};
        datawriter_qos.reliability().kind = ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;
        response_writer_ = publisher_->create_datawriter(*response_topic_, datawriter_qos, nullptr, NONE_STATUS_MASK);
		
        DataReaderQos datareader_qos{};
        datareader_qos.reliability().kind = ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;
        request_reader_ = subscriber_->create_datareader(*request_topic_, datareader_qos, this, DATA_AVAILABLE_STATUS);

        request_topic_->enable();
        response_topic_->enable();
        publisher_->enable();
        subscriber_->enable();
        response_writer_->enable();
        request_reader_->enable();
    }

    void on_data_available(
            DataReader& reader) noexcept override
    {
        auto typed_reader = TypedDataReader<RequestTS>::downcast(reader);

        typename RequestTS::DataType request;
        SampleInfo info;

        while (typed_reader->take_next_sample(request, info) == dds::ReturnCode::OK)
        {
            if (info.valid_data)
            {
                std::cout << "Received request: " << request.control_name <<  std::endl;

                typename ResponseTS::DataType response = callback_(request);

                auto typed_writer = TypedDataWriter<ResponseTS>::downcast(*response_writer_);
                ExtendedTypedDataWriter<ResponseTS>* extended_typed_writer =
                        reinterpret_cast<ExtendedTypedDataWriter<ResponseTS>*>(typed_writer);

                std::cout << "Sending response: " << response.value << std::endl;

                InlineQoS inline_qos;
                inline_qos.related_sample_identity.init();
                inline_qos.related_sample_identity.related_sample_identity = info.extension.sample_identity;

                datacentric::SampleKey key = {};
                extended_typed_writer->extended_write(response, HANDLE_NIL,
                        get_platform().get_current_timepoint(), &inline_qos, key);
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

    DataWriter* response_writer_;
    DataReader* request_reader_;

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
    ServiceServer<GetControlValueReqTypeSupport, GetControlValueResTypeSupport> service_server(
        *participant,
        "d555_poc_get_control_value",
        "GenControlValueSrv_",
        [](const GetControlValueReq& req) -> GetControlValueRes
        {
            (void)req;
            GetControlValueRes res;
            res.value = 3000;

            return res;
        });

    // Enable entities
    bool enabled = true;
    enabled &= dds::ReturnCode::OK == participant->enable();

    if (!enabled)
    {
        std::cout << "Error enabling entities" << std::endl;
    }

    // Spin entities
    execution::ISpinnable* executor = factory.create_default_executor();

    while (1)
    {
        while (executor->has_pending_work())
        {
            executor->spin(execution::TIME_ZERO);
        }

        execution::TimePoint next_work_timepoint = executor->get_next_work_timepoint();

        // Block on transport until next work timepoint or data is received
        executor->spin(next_work_timepoint);
    }

    return 0;
}
