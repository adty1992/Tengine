#include <iostream>
#include <math.h>

#include "logger.hpp"
#include "node_ops.hpp"
#include "tensor_mem.hpp"
#include "kernel_registry.hpp"
#include "tengine_errno.hpp"
#include "graph.hpp"
#include "operator/spacetodepth.hpp"

#include "kernel/spacetodepth/ref_spacetodepth_kernel.h"

namespace TEngine{

namespace RefSpaceToDepth{
struct RefSpaceToDepth : public MTNodeOps
{
    bool Prerun(Node* node) override;
    bool Run(Node* node) override;
    void InitRegistry(void);

    RefSpaceToDepth()
    {
        kernel_run = nullptr;
        InitRegistry();
    }

    struct spacetodepth_param op_param;
    ref_spacetodepth_t kernel_run;
    KernelRegistry<ref_spacetodepth_t> kernel_registry;
};

void RefSpaceToDepth::InitRegistry(void)
{
#ifdef CONFIG_KERNEL_FP32
    kernel_registry.Register(( ref_spacetodepth_t )ref_spacetodepth_fp32, TENGINE_LAYOUT_NCHW, TENGINE_DT_FP32);
    kernel_registry.Register(( ref_spacetodepth_t )ref_spacetodepth_fp32, TENGINE_LAYOUT_NHWC, TENGINE_DT_FP32);
#endif

}

bool RefSpaceToDepth::Prerun(Node* node)
{
    int layout = exec_attr->graph_layout;

    Tensor* input_tensor = node->GetInputTensor(0);
    const TShape& in_shape = input_tensor->GetShape();
    op_param.type = in_shape.GetDataLayout();
    op_param.size = in_shape.GetSize();
    if(!kernel_registry.GetKernel(kernel_run, layout, input_tensor->GetDataType()))
    {
        set_tengine_errno(ENOENT);
        return false;
    }

    return true;
}

bool RefSpaceToDepth::Run(Node* node)
{
    Tensor* output_tensor = node->GetOutputTensor(0);
    void* output = get_tensor_mem(output_tensor);
    int data_type = -1;

    Tensor* input_tensor = node->GetInputTensor(0);
    data_type = input_tensor->GetDataType();
    auto* in_quant = input_tensor->GetQuantParam();
    if((*in_quant).size() != 0)
    {
        op_param.in_scale = (*in_quant)[0].scale;
        op_param.in_zero = (*in_quant)[0].zero_point;
    }
    else
    {
        op_param.in_scale = 1;
        op_param.in_zero = 0;
    }

    const void* input_data = get_tensor_mem(input_tensor);

    auto* o_quant = output_tensor->GetQuantParam();
    if((*o_quant).size() != 0)
    {
        op_param.out_scale = (*o_quant)[0].scale;
        op_param.out_zero = (*o_quant)[0].zero_point;
    }
    else
    {
        op_param.out_scale = 1;
        op_param.out_zero = 0;
    }
    int ret = kernel_run(input_data, output, &op_param);
    if(ret < 0)
        return false;

    if(data_type == TENGINE_DT_INT8)
    {
        auto* o_quant = output_tensor->GetQuantParam();
        QuantParam q_param;
        q_param.scale = op_param.out_scale;
        o_quant->resize(0);
        o_quant->push_back(q_param);
    }

    return true;
}

NodeOps* SelectFunc(const CPUInfo* info, Node* node)
{
    RefSpaceToDepth* ops = new RefSpaceToDepth();

    LOG_DEBUG() << "RefSpaceToDepth is selected\n";

    return ops;
}

} // namespace RefSpaceToDepth

void RegisterRefSpaceToDepth(void)
{
    NodeOpsRegistryManager::RegisterOPImplementor(REF_REGISTRY_NAME, "SpaceToDepth", RefSpaceToDepth::SelectFunc,1000);
}

} // namespace TEngine
