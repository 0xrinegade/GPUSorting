/******************************************************************************
 * GPUSorting
 *
 * SPDX-License-Identifier: MIT
 * Copyright Thomas Smith 2/13/2024
 * https://github.com/b0nes164/GPUSorting
 *
 ******************************************************************************/
#pragma once
#include "pch.h"
#include "OneSweep.h"

OneSweep::OneSweep(
    winrt::com_ptr<ID3D12Device> _device,
    GPUSorting::DeviceInfo _deviceInfo,
    GPUSorting::ORDER sortingOrder,
    GPUSorting::KEY_TYPE keyType) :
    SweepBase(
        _device,
        _deviceInfo,
        sortingOrder,
        keyType,
        "OneSweep ",
        4,
        256,
        1 << 13)
{
    m_device.copy_from(_device.get());
    SetCompileArguments();
    Initialize();
}

OneSweep::OneSweep(
    winrt::com_ptr<ID3D12Device> _device,
    GPUSorting::DeviceInfo _deviceInfo,
    GPUSorting::ORDER sortingOrder,
    GPUSorting::KEY_TYPE keyType,
    GPUSorting::PAYLOAD_TYPE payloadType) :
    SweepBase(
        _device,
        _deviceInfo,
        sortingOrder,
        keyType,
        payloadType,
        "OneSweep ",
        4,
        256,
        1 << 13)
{
    m_device.copy_from(_device.get());
    SetCompileArguments();
    Initialize();
}

OneSweep::~OneSweep()
{
}

void OneSweep::InitComputeShaders()
{
    const std::filesystem::path path = "Shaders/OneSweep.hlsl";
    m_initSweep = new SweepCommonKernels::InitSweep(m_device, m_devInfo, m_compileArguments, path);
    m_globalHist = new SweepCommonKernels::GlobalHist(m_device, m_devInfo, m_compileArguments, path);
    m_scan = new SweepCommonKernels::Scan(m_device, m_devInfo, m_compileArguments, path);
    m_digitBinningPass = new OneSweepKernels::DigitBinningPass(m_device, m_devInfo, m_compileArguments, path);
}

void OneSweep::PrepareSortCmdList()
{
    m_initSweep->Dispatch(
        m_cmdList,
        m_globalHistBuffer->GetGPUVirtualAddress(),
        m_passHistBuffer->GetGPUVirtualAddress(),
        m_indexBuffer->GetGPUVirtualAddress(),
        m_partitions);
    UAVBarrierSingle(m_cmdList, m_globalHistBuffer);

    m_globalHist->Dispatch(
        m_cmdList,
        m_sortBuffer->GetGPUVirtualAddress(),
        m_globalHistBuffer->GetGPUVirtualAddress(),
        m_numKeys,
        m_globalHistPartitions);
    UAVBarrierSingle(m_cmdList, m_globalHistBuffer);

    m_scan->Dispatch(
        m_cmdList,
        m_globalHistBuffer->GetGPUVirtualAddress(),
        m_passHistBuffer->GetGPUVirtualAddress(),
        m_partitions,
        k_radixPasses);
    UAVBarrierSingle(m_cmdList, m_passHistBuffer);

    for (uint32_t radixShift = 0; radixShift < 32; radixShift += 8)
    {
        m_digitBinningPass->Dispatch(
            m_cmdList,
            m_sortBuffer->GetGPUVirtualAddress(),
            m_altBuffer->GetGPUVirtualAddress(),
            m_sortPayloadBuffer->GetGPUVirtualAddress(),
            m_altPayloadBuffer->GetGPUVirtualAddress(),
            m_indexBuffer->GetGPUVirtualAddress(),
            m_passHistBuffer,
            m_numKeys,
            m_partitions,
            radixShift);
        UAVBarrierSingle(m_cmdList, m_sortBuffer);
        UAVBarrierSingle(m_cmdList, m_sortPayloadBuffer);
        UAVBarrierSingle(m_cmdList, m_altBuffer);
        UAVBarrierSingle(m_cmdList, m_altPayloadBuffer);

        swap(m_sortBuffer, m_altBuffer);
        swap(m_sortPayloadBuffer, m_altPayloadBuffer);
    }
}