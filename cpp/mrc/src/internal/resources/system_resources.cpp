/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "internal/resources/system_resources.hpp"

#include "internal/control_plane/client.hpp"
#include "internal/control_plane/client/connections_manager.hpp"
#include "internal/control_plane/client/instance.hpp"
#include "internal/data_plane/data_plane_resources.hpp"  // IWYU pragma: keep
#include "internal/memory/device_resources.hpp"
#include "internal/network/network_resources.hpp"
#include "internal/resources/partition_resources_base.hpp"
#include "internal/runnable/runnable_resources.hpp"
#include "internal/system/engine_factory_cpu_sets.hpp"
#include "internal/system/host_partition.hpp"
#include "internal/system/partition.hpp"
#include "internal/system/partitions.hpp"
#include "internal/system/system.hpp"
#include "internal/system/threading_resources.hpp"
#include "internal/ucx/registation_callback_builder.hpp"
#include "internal/ucx/ucx_resources.hpp"
#include "internal/ucx/worker.hpp"
#include "internal/utils/contains.hpp"

#include "mrc/core/bitmap.hpp"
#include "mrc/core/task_queue.hpp"
#include "mrc/exceptions/runtime_error.hpp"
#include "mrc/options/options.hpp"
#include "mrc/options/placement.hpp"

#include <boost/fiber/future/future.hpp>
#include <glog/logging.h>

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <thread>
#include <utility>

namespace mrc::resources {

thread_local SystemResources* SystemResources::m_thread_resources{nullptr};
thread_local PartitionResources* SystemResources::m_thread_partition{nullptr};

SystemResources::SystemResources(const system::SystemProvider& system) :
  SystemResources(std::make_unique<system::ThreadingResources>(system))
{}

SystemResources::SystemResources(std::unique_ptr<system::ThreadingResources> resources) :
  SystemProvider(*resources),
  m_threading_resources(std::move(resources))
{
    // Create the system-wide runnable first
    m_sys_runnable = std::make_unique<runnable::RunnableResources>(*m_threading_resources,
                                                                   this->system().partitions().sys_host_partition());

    const auto& partitions      = system().partitions().flattened();
    const auto& host_partitions = system().partitions().host_partitions();
    const bool network_enabled  = system().options().enable_server();

    // // Initialization process:

    // // Optionally initialize the control plane server if needed

    // // Create the initial task queue for the control plane client to make connections using the lowest CpuSet
    // available

    // // Construct the control plane client and establish connection to server
    // auto client = control_plane::Client();

    // // Once connection is established, the client sends it's topology to the server and recieves the "cloud options"

    // // With the response, create a UCX resource object for each partition (generating a worker address)
    // std::vector<std::optional<ucx::UcxResources>> m_ucx;
    // std::vector<std::string> ucx_addresses;

    // for (int i = 0; i < partitions.size(); i++)
    // {
    //     const auto& partition = partitions.at(i);

    //     VLOG(1) << "building ucx resources for partition " << i;
    //     auto network_task_queue_cpuset = partition.host().engine_factory_cpu_sets().fiber_cpu_sets.at("mrc_network");
    //     auto& network_fiber_queue      = m_system->get_task_queue(network_task_queue_cpuset.first());
    //     std::optional<ucx::UcxResources> ucx;
    //     // ucx.emplace(base, network_fiber_queue);
    //     m_ucx.push_back(std::move(ucx));

    //     ucx_addresses.push_back(ucx->worker().address());
    // }

    // // Instruct the client to register the worker addresses with the server, returning a partition ID
    // auto instance_ids = client.register_ucx_addresses(ucx_addresses);

    // // At this point, we need to create the partitions. With the returned partition IDs, create one passing in the
    // UCX
    // // resources
    // for (size_t i = 0; i < partitions.size(); i++)
    // {
    //     m_partition_managers.emplace_back(i, m_ucx.at(i));
    // }

    // construct the runnable resources on each host_partition - launch control and main
    for (const auto& host_part : host_partitions)
    {
        // VLOG(1) << "building runnable/launch_control resources on host_partition: " << i;
        m_runnable.emplace_back(*m_threading_resources, host_part);
    }

    std::vector<PartitionResourceBase> base_partition_resources;
    for (int i = 0; i < partitions.size(); i++)
    {
        auto host_partition_id = partitions.at(i).host_partition_id();
        base_partition_resources.emplace_back(m_runnable.at(host_partition_id), i);
    }

    // construct ucx resources on each flattened partition
    // this provides a ucx context, ucx worker and registration cache per partition
    for (auto& base : base_partition_resources)
    {
        if (network_enabled)
        {
            VLOG(1) << "building ucx resources for partition " << base.partition_id();
            auto network_task_queue_cpuset = base.partition().host().engine_factory_cpu_sets().fiber_cpu_sets.at(
                "mrc_network");
            auto& network_fiber_queue = m_threading_resources->get_task_queue(network_task_queue_cpuset.first());
            std::optional<ucx::UcxResources> ucx;
            ucx.emplace(base, network_fiber_queue);
            m_ucx.push_back(std::move(ucx));
        }
        else
        {
            m_ucx.emplace_back(std::nullopt);
        }
    }

    // // create control plane and register worker addresses
    // std::map<InstanceID, std::unique_ptr<control_plane::client::Instance>> control_instances;
    // if (network_enabled)
    // {
    //     m_control_plane   = std::make_shared<control_plane::ControlPlaneResources>(base_partition_resources.at(0));
    //     control_instances = m_control_plane->client().register_ucx_addresses(m_ucx);
    //     CHECK_EQ(m_control_plane->client().connections().instance_ids().size(), m_ucx.size());
    // }

    // construct the host memory resources for each host_partition
    for (std::size_t i = 0; i < host_partitions.size(); ++i)
    {
        ucx::RegistrationCallbackBuilder builder;
        for (auto& ucx : m_ucx)
        {
            if (ucx)
            {
                if (ucx->partition().host_partition_id() == i)
                {
                    ucx->add_registration_cache_to_builder(builder);
                }
            }
        }
        VLOG(1) << "building host resources for host_partition: " << i;
        m_host.emplace_back(m_runnable.at(i), std::move(builder));
    }

    // devices resources
    for (auto& base : base_partition_resources)
    {
        VLOG(1) << "building device resources for partition: " << base.partition_id();
        if (base.partition().has_device())
        {
            DCHECK_LT(base.partition_id(), device_count());
            std::optional<memory::DeviceResources> device;
            device.emplace(base, m_ucx.at(base.partition_id()));
            m_device.emplace_back(std::move(device));
        }
        else
        {
            m_device.emplace_back(std::nullopt);
        }
    }

    // network resources
    for (auto& base : base_partition_resources)
    {
        if (network_enabled)
        {
            VLOG(1) << "building network resources for partition: " << base.partition_id();
            // CHECK(m_ucx.at(base.partition_id()));
            // auto instance_id = m_control_plane->client().connections().instance_ids().at(base.partition_id());
            // DCHECK(contains(control_instances, instance_id));  // todo(cpp20) contains
            // auto instance = std::move(control_instances.at(instance_id));
            // network::NetworkResources network(base,
            //                                   *m_ucx.at(base.partition_id()),
            //                                   m_host.at(base.partition().host_partition_id()),
            //                                   std::move(instance));
            // m_network.emplace_back(std::move(network));
            m_network.emplace_back(std::nullopt);
        }
        else
        {
            m_network.emplace_back(std::nullopt);
        }
    }

    // partition resources
    for (std::size_t i = 0; i < partition_count(); ++i)
    {
        VLOG(1) << "building partition_resources for partition: " << i;
        auto host_partition_id = partitions.at(i).host_partition_id();
        m_partitions.emplace_back(m_runnable.at(host_partition_id),
                                  i,
                                  m_host.at(host_partition_id),
                                  m_device.at(i),
                                  m_ucx.at(i));
    }

    // set thread local access to resources on all fiber task queues and any future thread created by the runtime
    for (auto& partition : m_partitions)
    {
        m_threading_resources->register_thread_local_initializer(
            partition.partition().host().cpu_set(),
            [this, &partition] {
                m_thread_resources = this;
                if (system().partitions().device_to_host_strategy() == PlacementResources::Dedicated)
                {
                    m_thread_partition = &partition;
                }
            });
    }

    VLOG(10) << "resources::Manager initialized";
}

SystemResources::~SystemResources()
{
    m_network.clear();
}

SystemResources& SystemResources::get_resources()
{
    if (m_thread_resources == nullptr)  // todo(cpp20) [[unlikely]]
    {
        LOG(ERROR) << "thread with id " << std::this_thread::get_id()
                   << " attempting to access the MRC runtime, but is not a MRC runtime thread";
        throw exceptions::MrcRuntimeError("can not access runtime resources from outside the runtime");
    }

    return *m_thread_resources;
}

PartitionResources& SystemResources::get_partition()
{
    {
        if (m_thread_partition == nullptr)  // todo(cpp20) [[unlikely]]
        {
            auto& resources = SystemResources::get_resources();

            if (resources.system().partitions().device_to_host_strategy() == PlacementResources::Shared)
            {
                LOG(ERROR) << "runtime partition query is disabed when PlacementResources::Shared is in use";
                throw exceptions::MrcRuntimeError("unable to access runtime parition info");
            }

            LOG(ERROR) << "thread with id " << std::this_thread::get_id()
                       << " attempting to access the MRC runtime, but is not a MRC runtime thread";
            throw exceptions::MrcRuntimeError("can not access runtime resources from outside the runtime");
        }

        return *m_thread_partition;
    }
}

std::size_t SystemResources::device_count() const
{
    return system().partitions().device_partitions().size();
};

std::size_t SystemResources::partition_count() const
{
    return system().partitions().flattened().size();
};

const std::vector<PartitionResources>& SystemResources::partitions() const
{
    return m_partitions;
}

PartitionResources& SystemResources::partition(std::size_t partition_id)
{
    CHECK_LT(partition_id, m_partitions.size());
    return m_partitions.at(partition_id);
}

void SystemResources::initialize() {}

// Future<void> SystemResources::shutdown()
// {
//     return m_runnable.at(0).main().enqueue([this] {
//         std::vector<Future<void>> futures;
//         futures.reserve(m_network.size());
//         for (auto& net : m_network)
//         {
//             if (net)
//             {
//                 futures.emplace_back(net->shutdown());
//             }
//         }
//         for (auto& f : futures)
//         {
//             f.get();
//         }
//     });
// }
}  // namespace mrc::resources
