/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "pysrf/segment.hpp"

#include "pysrf/node.hpp"  // IWYU pragma: keep
#include "pysrf/segment_modules.hpp"
#include "pysrf/types.hpp"
#include "pysrf/utils.hpp"

#include "srf/channel/status.hpp"
#include "srf/experimental/modules/segment_modules.hpp"
#include "srf/node/edge_connector.hpp"
#include "srf/segment/builder.hpp"
#include "srf/segment/definition.hpp"
#include "srf/segment/object.hpp"  // IWYU pragma: keep

#include <pybind11/cast.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

#include <cstdint>

// IWYU thinks the Segment.def calls need array and vector
// IWYU pragma: no_include <array>
// IWYU pragma: no_include <vector>
// IWYU pragma: no_include <pybind11/detail/common.h>
// IWYU pragma: no_include <pybind11/detail/descr.h>

namespace srf::pysrf {

namespace py = pybind11;

PYBIND11_MODULE(segment, m)
{
    m.doc() = R"pbdoc(
        Python bindings for SRF Segments
        -------------------------------
        .. currentmodule:: segment
        .. autosummary::
           :toctree: _generate
    )pbdoc";

    // Common must be first in every module
    pysrf::import(m, "srf.core.common");

    pysrf::import_module_object(m, "srf.core.node", "SegmentObject");
    pysrf::import(m, "srf.core.subscriber");

    // Register the converters for make_py2cxx_edge_adapter and make_cxx2py_edge_adapter
    // Type 'b'
    node::EdgeConnector<bool, PyHolder>::register_converter();
    node::EdgeConnector<PyHolder, bool>::register_converter();

    // Type 'i'
    node::EdgeConnector<int32_t, PyHolder>::register_converter();
    node::EdgeConnector<PyHolder, int32_t>::register_converter();

    node::EdgeConnector<int64_t, PyHolder>::register_converter();
    node::EdgeConnector<PyHolder, int64_t>::register_converter();

    // Type 'u'
    node::EdgeConnector<uint32_t, PyHolder>::register_converter();
    node::EdgeConnector<PyHolder, uint32_t>::register_converter();

    node::EdgeConnector<uint64_t, PyHolder>::register_converter();
    node::EdgeConnector<PyHolder, uint64_t>::register_converter();

    // Type 'f'
    node::EdgeConnector<float, PyHolder>::register_converter();
    node::EdgeConnector<PyHolder, float>::register_converter();

    node::EdgeConnector<double, PyHolder>::register_converter();
    node::EdgeConnector<PyHolder, double>::register_converter();

    // Type 'S' and 'U'
    node::EdgeConnector<std::string, PyHolder>::register_converter();
    node::EdgeConnector<PyHolder, std::string>::register_converter();

    auto Definition = py::class_<srf::segment::Definition>(m, "Definition");
    auto Builder    = py::class_<srf::segment::Builder>(m, "Builder");
    auto SegmentModule =
        py::class_<srf::modules::SegmentModule, std::shared_ptr<srf::modules::SegmentModule>>(m, "SegmentModule");

    /** Builder Interface Declarations **/
    /*
     * @brief Make a source node that generates py::object values
     */
    Builder.def("make_source",
                static_cast<std::shared_ptr<srf::segment::ObjectProperties> (*)(
                    srf::segment::Builder&, const std::string&, py::iterator)>(&BuilderProxy::make_source));

    Builder.def("make_source",
                static_cast<std::shared_ptr<srf::segment::ObjectProperties> (*)(
                    srf::segment::Builder&, const std::string&, py::iterable)>(&BuilderProxy::make_source),
                py::return_value_policy::reference_internal);

    Builder.def("make_source",
                static_cast<std::shared_ptr<srf::segment::ObjectProperties> (*)(
                    srf::segment::Builder&, const std::string&, py::function)>(&BuilderProxy::make_source));

    /**
     * Construct a new py::object sink.
     * Create and return a Segment node used to sink python objects following out of the Segment.
     *
     * (py) @param name: Unique name of the node that will be created in the SRF Segment.
     * (py) @param on_next: python/std function that will be called on a new data element.
     * (py) @param on_error: python/std function that will be called if an error occurs.
     * (py) @param on_completed: python/std function that will be called
     *  Python example.
     *  ```python
     *      def my_on_next(x):
     *          print(f"Sinking {x}")
     *      def my_on_error(err):
     *          print(f"An error occurred: {err}")
     *      def my_on_completed():
     *          print(f"Completed processing")
     *
     *      sink = segment.make_sink("test", my_on_next, my_on_error, my_on_completed)
     *  ```
     */
    Builder.def("make_sink", &BuilderProxy::make_sink, py::return_value_policy::reference_internal);

    /**
     * Construct a new 'pure' python::object -> python::object node
     *
     * This will create and return a new lambda function with the following signature:
     * (py) @param name : Unique name of the node that will be created in the SRF Segment.
     * (py) @param map_f : a std::function that takes a py::object and returns a py::object. This is your
     * python-function which will be called on each data element as it flows through the node.
     */
    Builder.def("make_node", &BuilderProxy::make_node, py::return_value_policy::reference_internal);

    Builder.def("get_egress", &BuilderProxy::get_egress);

    Builder.def("get_ingress", &BuilderProxy::get_ingress);

    Builder.def("make_cxx2py_edge_adapter", &BuilderProxy::make_cxx2py_edge_adapter);

    Builder.def("make_edge", &BuilderProxy::make_edge);

    Builder.def("make_edge", &BuilderProxy::make_edge, py::arg("source"), py::arg("sink"));

    Builder.def("make_module",
                &BuilderProxy::make_module,
                py::arg("module_name"),
                py::arg("module_id"),
                py::arg("module_config"),
                py::return_value_policy::reference_internal);

    Builder.def("make_node_full", &BuilderProxy::make_node_full, py::return_value_policy::reference_internal);

    Builder.def("make_py2cxx_edge_adapter", &BuilderProxy::make_py2cxx_edge_adapter);

    /** Segment Module Interface Declarations **/
    SegmentModule.def("input_port", &SegmentModuleProxy::input_port, py::arg("input_id"));

    SegmentModule.def("output_port", &SegmentModuleProxy::output_port, py::arg("output_id"));

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
}  // namespace srf::pysrf
