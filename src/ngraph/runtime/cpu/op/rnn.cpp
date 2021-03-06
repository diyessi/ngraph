//*****************************************************************************
// Copyright 2017-2020 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#include "ngraph/runtime/cpu/op/rnn.hpp"
#include "ngraph/log.hpp"
#include "ngraph/util.hpp"

using namespace std;
using namespace ngraph;

constexpr NodeTypeInfo op::Rnn::type_info;

shared_ptr<Node> op::Rnn::clone_with_new_inputs(const OutputVector& new_args) const
{
    if (new_args.size() != 6 && new_args.size() != 5)
    {
        throw ngraph_error("Incorrect number of new arguments");
    }

    if (new_args.size() == 5)
    {
        return make_shared<Rnn>(new_args[0],
                                new_args[1],
                                new_args[2],
                                new_args[3],
                                new_args[4],
                                m_num_timesteps,
                                m_num_gates_per_cell,
                                m_src_sequence_length,
                                m_num_cell_states,
                                m_direction,
                                m_num_fused_layers,
                                m_rnntype);
    }
    else
    {
        return make_shared<Rnn>(new_args[0],
                                new_args[1],
                                new_args[2],
                                new_args[3],
                                new_args[4],
                                new_args[5],
                                m_num_timesteps,
                                m_num_gates_per_cell,
                                m_src_sequence_length,
                                m_num_cell_states,
                                m_direction,
                                m_num_fused_layers,
                                m_rnntype);
    }
}

op::Rnn::Rnn(const Output<Node>& src_layer,
             const Output<Node>& src_iter,
             const Output<Node>& weights_layer,
             const Output<Node>& weights_iter,
             const Output<Node>& bias,
             size_t num_timesteps,
             size_t num_gates_per_cell,
             size_t src_sequence_length,
             size_t num_cell_states,
             size_t direction,
             size_t num_fused_layers,
             ngraph::runtime::cpu::rnn_utils::rnntype rnn_type)
    : Op({src_layer, src_iter, weights_layer, weights_iter, bias})
    , m_num_timesteps(num_timesteps)
    , m_num_gates_per_cell(num_gates_per_cell)
    , m_src_sequence_length(src_sequence_length)
    , m_num_cell_states(num_cell_states)
    , m_direction(direction)
    , m_num_fused_layers(num_fused_layers)
    , m_rnntype(rnn_type)
{
    constructor_validate_and_infer_types();
    if (src_layer.get_shape().size() != weights_layer.get_shape().size())
    {
        throw ngraph_error("src_layer and i2h weights size dont match");
    }

    if (src_iter.get_shape().size() != weights_iter.get_shape().size())
    {
        throw ngraph_error("src_iter and h2h weights size dont match");
    }

    if (src_layer.get_shape().size() == 2)
    {
        m_batch_size = src_layer.get_shape()[0] / m_num_timesteps;
    }
    else
    {
        throw ngraph_error("src_layer doesnt have a rank 2");
    }

    m_dst_iter_feature_size = weights_iter.get_shape()[1] / (m_num_gates_per_cell);
    m_dst_layer_feature_size = weights_layer.get_shape()[1] / (m_num_gates_per_cell);
    m_src_iter_feature_size = weights_iter.get_shape()[0] / (m_direction * m_num_fused_layers);
    m_src_layer_feature_size = weights_layer.get_shape()[0] / (m_direction * m_num_fused_layers);

    if (shape_size(src_layer.get_shape()) !=
        m_src_sequence_length * m_batch_size * m_src_layer_feature_size)
    {
        throw ngraph_error("src_layer size is not equal t*n*c");
    }

    if ((bias.get_shape()[0] / (m_direction * m_num_fused_layers)) !=
            (weights_layer.get_shape()[1]) ||
        (bias.get_shape()[0] / (m_direction * m_num_fused_layers)) != (weights_iter.get_shape()[1]))
    {
        throw ngraph_error("bias and weights_shape are not compatible");
    }

    auto et = src_layer.get_element_type();
    for (const Input<Node>& rnn_input : inputs())
    {
        if (rnn_input.get_element_type() != et)
        {
            throw ngraph_error("all rnn inputs must have the same element type");
        }
    }

    set_output_size(2);
    set_output_type(0,
                    src_layer.get_element_type(),
                    Shape{(m_num_timesteps * m_batch_size), m_direction * m_src_iter_feature_size});
    set_output_type(1,
                    src_layer.get_element_type(),
                    Shape{(m_num_cell_states * m_direction * m_num_fused_layers * m_batch_size),
                          m_src_iter_feature_size});
}

op::Rnn::Rnn(const Output<Node>& src_layer,
             const Output<Node>& src_iter,
             const Output<Node>& src_iter_c,
             const Output<Node>& weights_layer,
             const Output<Node>& weights_iter,
             const Output<Node>& bias,
             size_t num_timesteps,
             size_t num_gates_per_cell,
             size_t src_sequence_length,
             size_t num_cell_states,
             size_t direction,
             size_t num_fused_layers,
             ngraph::runtime::cpu::rnn_utils::rnntype rnn_type)
    : Op({src_layer, src_iter, src_iter_c, weights_layer, weights_iter, bias})
    , m_num_timesteps(num_timesteps)
    , m_num_gates_per_cell(num_gates_per_cell)
    , m_src_sequence_length(src_sequence_length)
    , m_num_cell_states(num_cell_states)
    , m_direction(direction)
    , m_num_fused_layers(num_fused_layers)
    , m_rnntype(rnn_type)
{
    constructor_validate_and_infer_types();
    if (src_layer.get_shape().size() != weights_layer.get_shape().size())
    {
        throw ngraph_error("src_layer and i2h weights size dont match");
    }

    if (src_iter.get_shape().size() != weights_iter.get_shape().size())
    {
        throw ngraph_error("src_iter and h2h weights size dont match");
    }

    if (src_layer.get_shape().size() == 2)
    {
        m_batch_size = src_layer.get_shape()[0] / m_num_timesteps;
    }
    else
    {
        throw ngraph_error("src_layer doesnt have a rank 2");
    }

    m_dst_iter_feature_size = weights_iter.get_shape()[1] / (m_num_gates_per_cell);
    m_dst_layer_feature_size = weights_layer.get_shape()[1] / (m_num_gates_per_cell);
    m_src_iter_feature_size = weights_iter.get_shape()[0] / (m_direction * m_num_fused_layers);
    m_src_layer_feature_size = weights_layer.get_shape()[0] / (m_direction * m_num_fused_layers);

    if (shape_size(src_layer.get_shape()) !=
        m_src_sequence_length * m_batch_size * m_src_layer_feature_size)
    {
        throw ngraph_error("src_layer size is not equal t*n*c");
    }

    if ((bias.get_shape()[0] / (m_direction * m_num_fused_layers)) !=
            (weights_layer.get_shape()[1]) ||
        (bias.get_shape()[0] / (m_direction * m_num_fused_layers)) != (weights_iter.get_shape()[1]))
    {
        throw ngraph_error("bias and weights_shape are not compatible");
    }

    auto et = src_layer.get_element_type();
    for (auto& rnn_input : get_arguments())
    {
        if (rnn_input->get_output_element_type(0) != et)
        {
            throw ngraph_error("all rnn inputs must have the same element type");
        }
    }

    set_output_size(3);
    set_output_type(0,
                    src_layer.get_element_type(),
                    Shape{(m_num_timesteps * m_batch_size), m_direction * m_src_iter_feature_size});
    set_output_type(
        1,
        src_layer.get_element_type(),
        Shape{(m_direction * m_num_fused_layers * m_batch_size), m_src_iter_feature_size});
    set_output_type(
        2,
        src_layer.get_element_type(),
        Shape{(m_direction * m_num_fused_layers * m_batch_size), m_src_iter_feature_size});
}
