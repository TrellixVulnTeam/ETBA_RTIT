/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef TENSORRT_BUFFERS_H
#define TENSORRT_BUFFERS_H

#include "NvInfer.h"
#include "common.h"
#include "half.h"
#include <cassert>
#include <cuda_runtime_api.h>
#include <iostream>
#include <iterator>
#include <memory>
#include <new>
#include <numeric>
#include <string>
#include <vector>
#include "../cuda_func/buffer_copy.cuh"

namespace samplesCommon
{

//!
//! \brief  The GenericBuffer class is a templated class for buffers.
//!
//! \details This templated RAII (Resource Acquisition Is Initialization) class handles the allocation,
//!          deallocation, querying of buffers on both the device and the host.
//!          It can handle data of arbitrary types because it stores byte buffers.
//!          The template parameters AllocFunc and FreeFunc are used for the
//!          allocation and deallocation of the buffer.
//!          AllocFunc must be a functor that takes in (void** ptr, size_t size)
//!          and returns bool. ptr is a pointer to where the allocated buffer address should be stored.
//!          size is the amount of memory in bytes to allocate.
//!          The boolean indicates whether or not the memory allocation was successful.
//!          FreeFunc must be a functor that takes in (void* ptr) and returns void.
//!          ptr is the allocated buffer address. It must work with nullptr input.
//!
template <typename AllocFunc, typename FreeFunc>
class GenericBuffer
{
public:
    //!
    //! \brief Construct an empty buffer.
    //!
    GenericBuffer(nvinfer1::DataType type = nvinfer1::DataType::kFLOAT)
        : mSize(0)
        , mCapacity(0)
        , mType(type)
        , mBuffer(nullptr)
    {
    }

    //!
    //! \brief Construct a buffer with the specified allocation size in bytes.
    //!
    GenericBuffer(size_t size, nvinfer1::DataType type)
        : mSize(size)
        , mCapacity(size)
        , mType(type)
    {
        if (!allocFn(&mBuffer, this->nbBytes()))
        {
            throw std::bad_alloc();
        }
    }

    
    GenericBuffer(GenericBuffer&& buf)
        : mSize(buf.mSize)
        , mCapacity(buf.mCapacity)
        , mType(buf.mType)
        , mBuffer(buf.mBuffer)
    {
        std::cout << "move construction" << std::endl;
        buf.mSize = 0;
        buf.mCapacity = 0;
        buf.mType = nvinfer1::DataType::kFLOAT;
        buf.mBuffer = nullptr;
    }

    GenericBuffer& operator=(GenericBuffer&& buf)
    {
        if (this != &buf)
        {
            freeFn(mBuffer);
            mSize = buf.mSize;
            mCapacity = buf.mCapacity;
            mType = buf.mType;
            mBuffer = buf.mBuffer;
            // Reset buf.
            buf.mSize = 0;
            buf.mCapacity = 0;
            buf.mBuffer = nullptr;
        }
        return *this;
    }

    GenericBuffer(const GenericBuffer& buf)
        : mSize(buf.mSize)
        , mCapacity(buf.mCapacity)
        , mType(buf.mType)
        , mBuffer(buf.mBuffer)
    {
        std::cout << "move construction" << std::endl;
        // buf.mSize = 0;
        // buf.mCapacity = 0;
        // buf.mType = nvinfer1::DataType::kFLOAT;
        // buf.mBuffer = nullptr;
    }

    GenericBuffer& operator=(const GenericBuffer& buf)
    {
        if (this != &buf)
        {
            freeFn(mBuffer);
            mSize = buf.mSize;
            mCapacity = buf.mCapacity;
            mType = buf.mType;
            mBuffer = buf.mBuffer;
            // Reset buf.
            // buf.mSize = 0;
            // buf.mCapacity = 0;
            // buf.mBuffer = nullptr;
        }
        return *this;
    }
    

    //!
    //! \brief Returns pointer to underlying array.
    //!
    void* data()
    {
        return mBuffer;
    }

    //!
    //! \brief Returns pointer to underlying array.
    //!
    const void* data() const
    {
        return mBuffer;
    }

    //!
    //! \brief Returns the size (in number of elements) of the buffer.
    //!
    size_t size() const
    {
        return mSize;
    }

    //!
    //! \brief Returns the size (in bytes) of the buffer.
    //!
    size_t nbBytes() const
    {
        return this->size() * samplesCommon::getElementSize(mType);
    }

    //!
    //! \brief Resizes the buffer. This is a no-op if the new size is smaller than or equal to the current capacity.
    //!
    void resize(size_t newSize)
    {
        mSize = newSize;
        if (mCapacity < newSize)
        {
            freeFn(mBuffer);
            if (!allocFn(&mBuffer, this->nbBytes()))
            {
                throw std::bad_alloc{};
            }
            mCapacity = newSize;
        }
    }

    void renavigation(const void* srcPtr)
    {
        mBuffer = srcPtr;
    }

    //!
    //! \brief Overload of resize that accepts Dims
    //!
    void resize(const nvinfer1::Dims& dims)
    {
        return this->resize(samplesCommon::volume(dims));
    }

    ~GenericBuffer()
    {
        freeFn(mBuffer);
    }

private:
    size_t mSize{0}, mCapacity{0};
    nvinfer1::DataType mType;
    void* mBuffer;
    AllocFunc allocFn;
    FreeFunc freeFn;
};

class DeviceAllocator
{
public:
    bool operator()(void** ptr, size_t size) const
    {
        return cudaMalloc(ptr, size) == cudaSuccess;
    }
};

class DeviceFree
{
public:
    void operator()(void* ptr) const
    {
        cudaFree(ptr);
    }
};

class HostAllocator
{
public:
    bool operator()(void** ptr, size_t size) const
    {
        *ptr = malloc(size);
        return *ptr != nullptr;
    }
};

class HostFree
{
public:
    void operator()(void* ptr) const
    {
        free(ptr);
    }
};

using DeviceBuffer = GenericBuffer<DeviceAllocator, DeviceFree>;
using HostBuffer = GenericBuffer<HostAllocator, HostFree>;

//!
//! \brief  The ManagedBuffer class groups together a pair of corresponding device and host buffers.
//!
class ManagedBuffer
{
public:
    DeviceBuffer deviceBuffer;
    HostBuffer hostBuffer;

    ManagedBuffer() {}

    ManagedBuffer(size_t vol_, nvinfer1::DataType type_)
    {
        //std::cout << "constructor" <<std::endl;
        deviceBuffer = DeviceBuffer(vol_, type_);
        hostBuffer = HostBuffer(vol_, type_);
    }

    
    ManagedBuffer(const ManagedBuffer &buf)
    {
        //std::cout << "copy constructor" << std::endl;
        std::cout << buf.deviceBuffer.nbBytes() << std::endl;
        std::cout << buf.hostBuffer.nbBytes() << std::endl;
        deviceBuffer = buf.deviceBuffer;
        hostBuffer = buf.hostBuffer;
    }

    ManagedBuffer(ManagedBuffer&& buf): deviceBuffer(buf.deviceBuffer), hostBuffer(buf.hostBuffer) {std::cout << "move constructor" <<std::endl;}

    
    ManagedBuffer& operator=(ManagedBuffer&& buf)
    {
        std::cout << "move assignment" <<std::endl;
        if (this != &buf)
        {
            deviceBuffer = buf.deviceBuffer;
            hostBuffer = buf.hostBuffer;
        }
        return *this;
    }

    ManagedBuffer& operator=(const ManagedBuffer& buf)
    {
        std::cout << "copy assignment" <<std::endl;
        if (this != &buf)
        {
            deviceBuffer = buf.deviceBuffer;
            hostBuffer = buf.hostBuffer;
        }
        return *this;
    }
    
    ~ManagedBuffer() = default; 
};

//!
//! \brief  The BufferManager class handles host and device buffer allocation and deallocation.
//!
//! \details This RAII class handles host and device buffer allocation and deallocation,
//!          memcpy between host and device buffers to aid with inference,
//!          and debugging dumps to validate inference. The BufferManager class is meant to be
//!          used to simplify buffer management and any interactions between buffers and the engine.
//!
class BufferManager
{
public:
    static const size_t kINVALID_SIZE_VALUE = ~size_t(0);

    //!
    //! \brief Create a BufferManager for handling buffer interactions with engine.
    //!
    BufferManager() {}
    BufferManager(BufferManager&& buf)
    : mEngine(std::move(buf.mEngine))
    , mBatchSize(std::move(buf.mBatchSize))
    , mManagedBuffers(std::move(buf.mManagedBuffers))
    , mDeviceBindings(std::move(buf.mDeviceBindings)){}

    BufferManager(std::shared_ptr<nvinfer1::ICudaEngine> engine, const int batchSize = 0,
        const std::shared_ptr<ManagedBuffer> srcPtr = nullptr, const std::vector<int>* copyListPtr = nullptr, 
        int copyMethod = 0, const nvinfer1::IExecutionContext* context = nullptr)
        : mEngine(engine)
        , mBatchSize(batchSize)
        , mCopyMethod(copyMethod)
    {
        // Full Dims implies no batch size.
        // assert(engine->hasImplicitBatchDimension() || mBatchSize == 0);
        // Create host and device buffers
        for (int i = 0; i < mEngine->getNbBindings(); i++)
        {            
            if (copyListPtr && srcPtr && i == 0) {
                // Two sources from the last ee module and the last sub module.
                // std::vector<bool> indicator = *indicatorPtr;
                // int cur_batch_size = indicator.size();
                // bool *indicator_host = new bool[cur_batch_size], *indicator_device = nullptr;
                // for (int j = 0; j < cur_batch_size; j++) {
                //     indicator_host[j] = indicator[j];
                // }
                // size_t next_batch_size = std::accumulate(indicator.begin(), indicator.end(), 0);

                std::vector<int> tmpCopyList = *copyListPtr;
                size_t next_batch_size = tmpCopyList.size();
                size_t cur_batch_size = batchSize;

                std::shared_ptr<ManagedBuffer> manBuf_ptr = srcPtr;

                nvinfer1::DataType type = mEngine->getBindingDataType(i);
                auto dims = context ? context->getBindingDimensions(i) : mEngine->getBindingDimensions(i);
                dims.d[0] = 1;
                size_t singleVol = samplesCommon::volume(dims);
                size_t vol = singleVol * next_batch_size;
                std::shared_ptr<ManagedBuffer> new_manBuf{new ManagedBuffer(vol, type)};
                dims.d[0] = next_batch_size;
                // std::cout << "Two sources. Input size: " << dims << std::endl;

                float* dstPtr_ = static_cast<float*>(new_manBuf->deviceBuffer.data());
                float* srcPtr_ = static_cast<float*>(manBuf_ptr->deviceBuffer.data());
                int* copyList = new int[next_batch_size], *copyList_device = nullptr;

                for (int idx = 0; idx < next_batch_size; idx++) {
                    copyList[idx] = tmpCopyList[idx];
                }

                if (mCopyMethod == 0) {
                    // useCUDA();
                    CUDACHECK(cudaMalloc(&copyList_device, next_batch_size * sizeof(int)));
                    CUDACHECK(cudaMemcpy(copyList_device, copyList, next_batch_size * sizeof(int), cudaMemcpyHostToDevice));
                    buffercopy(dstPtr_, srcPtr_, singleVol*next_batch_size, copyList_device, singleVol);
                }
                else {
                    size_t tSize = samplesCommon::getElementSize(type);
                    for (int stepOveridx = 0; stepOveridx < next_batch_size; stepOveridx++) {
                        //std::cout << "Copying: " << stepOveridx << std::endl;
                        const cudaMemcpyKind memcpyType = cudaMemcpyDeviceToDevice;
                        cudaMemcpy(dstPtr_ + stepOveridx*singleVol, 
                                    srcPtr_ + copyList[stepOveridx]*singleVol, 
                                    singleVol*tSize, memcpyType);
                    }
                }

                mManagedBuffers.emplace_back(std::move(new_manBuf));
                mDeviceBindings.emplace_back(mManagedBuffers.back()->deviceBuffer.data());
                continue;
            }
            else if (!copyListPtr && srcPtr && i == 0) {
                // One source from the last sub module.
                auto dims = mEngine->getBindingDimensions(i);
                dims.d[0] = batchSize;
                // std::cout << "One source. Input size: " << dims << std::endl;
                std::shared_ptr<ManagedBuffer> manBuf_ptr = srcPtr;
                mManagedBuffers.emplace_back(std::move(manBuf_ptr));
                mDeviceBindings.emplace_back(mManagedBuffers.back()->deviceBuffer.data());
                continue;
            }
            else if (i == 0) {
                auto dims = mEngine->getBindingDimensions(i);
                dims.d[0] = batchSize;
                // std::cout << "No source. Input size: " << dims << std::endl;
            }

            auto dims = context ? context->getBindingDimensions(i) : mEngine->getBindingDimensions(i);
            //size_t vol = context || !mBatchSize ? 1 : static_cast<size_t>(mBatchSize);
            nvinfer1::DataType type = mEngine->getBindingDataType(i);
            /*
            int vecDim = mEngine->getBindingVectorizedDim(i);
            std::cout << "VecDim: " << vecDim << std::endl;
            if (-1 != vecDim) // i.e., 0 != lgScalarsPerVector
            {
                std::cout << "-1 != vecDim" << std::endl;
                int scalarsPerVec = mEngine->getBindingComponentsPerElement(i);
                std::cout << "scalarsPerVec: " << scalarsPerVec << std::endl;
                dims.d[vecDim] = divUp(dims.d[vecDim], scalarsPerVec);
                std::cout << "NewDims: " << dims << std::endl;
                vol *= scalarsPerVec;
            }
            */
            dims.d[0] = mBatchSize;
            size_t vol = samplesCommon::volume(dims);
            std::shared_ptr<ManagedBuffer> manBuf{new ManagedBuffer(vol, type)};
            //mDeviceBindings.emplace_back(manBuf->deviceBuffer.data());
            mManagedBuffers.emplace_back(std::move(manBuf));
            //std::cout << mManagedBuffers.back()->deviceBuffer.data() <<std::endl;
            mDeviceBindings.emplace_back(mManagedBuffers.back()->deviceBuffer.data());

            if (i != 0) {
                auto dims = mEngine->getBindingDimensions(i);
                dims.d[0] = -1;
                // std::cout << "Output size: " << dims << std::endl;
                // std::cout << "\n" << std::endl;
            }
        }
    }

    //!
    //! \brief Returns a vector of device buffers that you can use directly as
    //!        bindings for the execute and enqueue methods of IExecutionContext.
    //!
    std::vector<void*>& getDeviceBindings()
    {
        return mDeviceBindings;
    }

    //!
    //! \brief Returns a vector of device buffers.
    //!
    const std::vector<void*>& getDeviceBindings() const
    {
        return mDeviceBindings;
    }

    std::shared_ptr<ManagedBuffer> getInputBuffer()
    {
        return mManagedBuffers[0];
    }

    std::shared_ptr<ManagedBuffer> getOutputBuffer()
    {
        return mManagedBuffers[1];
    }

    std::shared_ptr<ManagedBuffer> getImmediateBuffer(const int idx)
    {
        return mManagedBuffers[idx];
    }

    //!
    //! \brief Returns the device buffer corresponding to tensorName.
    //!        Returns nullptr if no such tensor can be found.
    //!
    void* getDeviceBuffer(const std::string& tensorName) const
    {
        return getBuffer(false, tensorName);
    }

    //!
    //! \brief Returns the host buffer corresponding to tensorName.
    //!        Returns nullptr if no such tensor can be found.
    //!
    void* getHostBuffer(const std::string& tensorName) const
    {
        return getBuffer(true, tensorName);
    }

    void* getInputHostBuffer() const
    {
        return mManagedBuffers[0]->hostBuffer.data();
    }

    void* getOutputHostBuffer() const
    {
        return mManagedBuffers[1]->hostBuffer.data();
    }


    //!
    //! \brief Returns the size of the host and device buffers that correspond to tensorName.
    //!        Returns kINVALID_SIZE_VALUE if no such tensor can be found.
    //!
    size_t size(const std::string& tensorName) const
    {
        int index = mEngine->getBindingIndex(tensorName.c_str());
        if (index == -1)
            return kINVALID_SIZE_VALUE;
        return mManagedBuffers[index]->hostBuffer.nbBytes();
    }

    //!
    //! \brief Dump host buffer with specified tensorName to ostream.
    //!        Prints error message to std::ostream if no such tensor can be found.
    //!
    void dumpBuffer(std::ostream& os, const std::string& tensorName)
    {
        int index = mEngine->getBindingIndex(tensorName.c_str());
        if (index == -1)
        {
            os << "Invalid tensor name" << std::endl;
            return;
        }
        void* buf = mManagedBuffers[index]->hostBuffer.data();
        size_t bufSize = mManagedBuffers[index]->hostBuffer.nbBytes();
        nvinfer1::Dims bufDims = mEngine->getBindingDimensions(index);
        size_t rowCount = static_cast<size_t>(bufDims.nbDims > 0 ? bufDims.d[bufDims.nbDims - 1] : mBatchSize);
        int leadDim = mBatchSize;
        int* trailDims = bufDims.d;
        int nbDims = bufDims.nbDims;

        // Fix explicit Dimension networks
        if (!leadDim && nbDims > 0)
        {
            leadDim = bufDims.d[0];
            ++trailDims;
            --nbDims;
        }

        os << "[" << leadDim;
        for (int i = 0; i < nbDims; i++)
            os << ", " << trailDims[i];
        os << "]" << std::endl;
        switch (mEngine->getBindingDataType(index))
        {
        case nvinfer1::DataType::kINT32: print<int32_t>(os, buf, bufSize, rowCount); break;
        case nvinfer1::DataType::kFLOAT: print<float>(os, buf, bufSize, rowCount); break;
        case nvinfer1::DataType::kHALF: print<half_float::half>(os, buf, bufSize, rowCount); break;
        case nvinfer1::DataType::kINT8: assert(0 && "Int8 network-level input and output is not supported"); break;
        case nvinfer1::DataType::kBOOL: assert(0 && "Bool network-level input and output are not supported"); break;
        }
    }

    //!
    //! \brief Templated print function that dumps buffers of arbitrary type to std::ostream.
    //!        rowCount parameter controls how many elements are on each line.
    //!        A rowCount of 1 means that there is only 1 element on each line.
    //!
    template <typename T>
    void print(std::ostream& os, void* buf, size_t bufSize, size_t rowCount)
    {
        assert(rowCount != 0);
        assert(bufSize % sizeof(T) == 0);
        T* typedBuf = static_cast<T*>(buf);
        size_t numItems = bufSize / sizeof(T);
        for (int i = 0; i < static_cast<int>(numItems); i++)
        {
            // Handle rowCount == 1 case
            if (rowCount == 1 && i != static_cast<int>(numItems) - 1)
                os << typedBuf[i] << std::endl;
            else if (rowCount == 1)
                os << typedBuf[i];
            // Handle rowCount > 1 case
            else if (i % rowCount == 0)
                os << typedBuf[i];
            else if (i % rowCount == rowCount - 1)
                os << " " << typedBuf[i] << std::endl;
            else
                os << " " << typedBuf[i];
        }
    }

    //!
    //! \brief Copy the contents of input host buffers to input device buffers synchronously.
    //!
    void copyInputToDevice()
    {
        memcpyBuffers(true, false, false);
    }

    //!
    //! \brief Copy the contents of output device buffers to output host buffers synchronously.
    //!
    void copyOutputToHost()
    {
        memcpyBuffers(false, true, false);
    }

    //!
    //! \brief Copy the contents of input host buffers to input device buffers asynchronously.
    //!
    void copyInputToDeviceAsync(const cudaStream_t& stream = 0)
    {
        memcpyBuffers(true, false, true, stream);
    }

    //!
    //! \brief Copy the contents of output device buffers to output host buffers asynchronously.
    //!
    void copyOutputToHostAsync(const cudaStream_t& stream = 0)
    {
        memcpyBuffers(false, true, true, stream);
    }

    ~BufferManager() = default;

private:
    void* getBuffer(const bool isHost, const std::string& tensorName) const
    {
        int index = mEngine->getBindingIndex(tensorName.c_str());
        if (index == -1)
            return nullptr;
        return (isHost ? mManagedBuffers[index]->hostBuffer.data() : mManagedBuffers[index]->deviceBuffer.data());
    }

    void memcpyBuffers(const bool copyInput, const bool deviceToHost, const bool async, const cudaStream_t& stream = 0)
    {
        for (int i = 0; i < mEngine->getNbBindings(); i++)
        {
            void* dstPtr
                = deviceToHost ? mManagedBuffers[i]->hostBuffer.data() : mManagedBuffers[i]->deviceBuffer.data();
            const void* srcPtr
                = deviceToHost ? mManagedBuffers[i]->deviceBuffer.data() : mManagedBuffers[i]->hostBuffer.data();
            const size_t byteSize = mManagedBuffers[i]->hostBuffer.nbBytes();
            const cudaMemcpyKind memcpyType = deviceToHost ? cudaMemcpyDeviceToHost : cudaMemcpyHostToDevice;
            if ((copyInput && mEngine->bindingIsInput(i)) || (!copyInput && !mEngine->bindingIsInput(i)))
            {
                if (async)
                    CUDACHECK(cudaMemcpyAsync(dstPtr, srcPtr, byteSize, memcpyType, stream));
                else
                    CUDACHECK(cudaMemcpy(dstPtr, srcPtr, byteSize, memcpyType));
            }
        }
    }

    std::shared_ptr<nvinfer1::ICudaEngine> mEngine;              //!< The pointer to the engine
    int mBatchSize;                                              //!< The batch size for legacy networks, 0 otherwise.
    int mCopyMethod;
    std::vector<std::shared_ptr<ManagedBuffer>> mManagedBuffers; //!< The vector of pointers to managed buffers
    std::vector<void*> mDeviceBindings;                          //!< The vector of device buffers needed for engine execution
};


//!
//! \brief  The ManagedBuffer class groups together a pair of corresponding device and host buffers.
//!
class BertManagedBuffer
{
public:
    DeviceBuffer deviceBuffer;
    HostBuffer hostBuffer;

    BertManagedBuffer() {}

    BertManagedBuffer(size_t vol_, nvinfer1::DataType type_)
    {
        //std::cout << "constructor" <<std::endl;
        deviceBuffer = DeviceBuffer(vol_, type_);
        hostBuffer = HostBuffer(vol_, type_);
    }

    
    BertManagedBuffer(const BertManagedBuffer &buf)
    {
        //std::cout << "copy constructor" << std::endl;
        std::cout << buf.deviceBuffer.nbBytes() << std::endl;
        std::cout << buf.hostBuffer.nbBytes() << std::endl;
        deviceBuffer = buf.deviceBuffer;
        hostBuffer = buf.hostBuffer;
    }

    BertManagedBuffer(BertManagedBuffer&& buf): deviceBuffer(buf.deviceBuffer), hostBuffer(buf.hostBuffer) {std::cout << "move constructor" <<std::endl;}

    
    BertManagedBuffer& operator=(BertManagedBuffer&& buf)
    {
        std::cout << "move assignment" <<std::endl;
        if (this != &buf)
        {
            deviceBuffer = buf.deviceBuffer;
            hostBuffer = buf.hostBuffer;
        }
        return *this;
    }

    BertManagedBuffer& operator=(const BertManagedBuffer& buf)
    {
        std::cout << "copy assignment" <<std::endl;
        if (this != &buf)
        {
            deviceBuffer = buf.deviceBuffer;
            hostBuffer = buf.hostBuffer;
        }
        return *this;
    }
    
    ~BertManagedBuffer() = default; 
};

//!
//! \brief  The BertBufferManager class handles host and device buffer allocation and deallocation.
//!
//! \details This RAII class handles host and device buffer allocation and deallocation,
//!          memcpy between host and device buffers to aid with inference,
//!          and debugging dumps to validate inference. The BertBufferManager class is meant to be
//!          used to simplify buffer management and any interactions between buffers and the engine.
//!
class BertBufferManager
{
public:
    static const size_t kINVALID_SIZE_VALUE = ~size_t(0);

    //!
    //! \brief Create a BertBufferManager for handling buffer interactions with engine.
    //!
    BertBufferManager() {}
    BertBufferManager(BertBufferManager&& buf)
    : mEngine(std::move(buf.mEngine))
    , mBatchSize(std::move(buf.mBatchSize))
    , mManagedBuffers(std::move(buf.mManagedBuffers))
    , mDeviceBindings(std::move(buf.mDeviceBindings)){}

    BertBufferManager(std::shared_ptr<nvinfer1::ICudaEngine> engine, const int batchSize = 0,
        const std::shared_ptr<BertManagedBuffer> srcPtr = nullptr, const std::vector<int>* copyListPtr = nullptr, 
        int copyMethod = 0, const nvinfer1::IExecutionContext* context = nullptr)
        : mEngine(engine)
        , mBatchSize(batchSize)
        , mCopyMethod(copyMethod)
    {
        // Full Dims implies no batch size.
        // assert(engine->hasImplicitBatchDimension() || mBatchSize == 0);
        // Create host and device buffers
        for (int i = 0; i < mEngine->getNbBindings(); i++)
        {            
            if (copyListPtr && srcPtr && i == 0) {
                // Two sources from the last ee module and the last sub module.
                // std::vector<bool> indicator = *indicatorPtr;
                // int cur_batch_size = indicator.size();
                // bool *indicator_host = new bool[cur_batch_size], *indicator_device = nullptr;
                // for (int j = 0; j < cur_batch_size; j++) {
                //     indicator_host[j] = indicator[j];
                // }
                // size_t next_batch_size = std::accumulate(indicator.begin(), indicator.end(), 0);

                std::vector<int> tmpCopyList = *copyListPtr;
                size_t next_batch_size = tmpCopyList.size();
                size_t cur_batch_size = batchSize;

                std::shared_ptr<BertManagedBuffer> manBuf_ptr = srcPtr;

                nvinfer1::DataType type = mEngine->getBindingDataType(i);
                auto dims = context ? context->getBindingDimensions(i) : mEngine->getBindingDimensions(i);
                dims.d[0] = 1;
                dims.d[1] = 7;
                size_t singleVol = samplesCommon::volume(dims);
                size_t vol = singleVol * next_batch_size;
                std::shared_ptr<BertManagedBuffer> new_manBuf{new BertManagedBuffer(vol, type)};
                dims.d[0] = next_batch_size;
                // std::cout << "Two sources. Input size: " << dims << std::endl;

                float* dstPtr_ = static_cast<float*>(new_manBuf->deviceBuffer.data());
                float* srcPtr_ = static_cast<float*>(manBuf_ptr->deviceBuffer.data());
                int* copyList = new int[next_batch_size], *copyList_device = nullptr;

                for (int idx = 0; idx < next_batch_size; idx++) {
                    copyList[idx] = tmpCopyList[idx];
                }

                if (mCopyMethod == 0) {
                    // useCUDA();
                    CUDACHECK(cudaMalloc(&copyList_device, next_batch_size * sizeof(int)));
                    CUDACHECK(cudaMemcpy(copyList_device, copyList, next_batch_size * sizeof(int), cudaMemcpyHostToDevice));
                    buffercopy(dstPtr_, srcPtr_, singleVol*next_batch_size, copyList_device, singleVol);
                }
                else {
                    size_t tSize = samplesCommon::getElementSize(type);
                    for (int stepOveridx = 0; stepOveridx < next_batch_size; stepOveridx++) {
                        //std::cout << "Copying: " << stepOveridx << std::endl;
                        const cudaMemcpyKind memcpyType = cudaMemcpyDeviceToDevice;
                        cudaMemcpy(dstPtr_ + stepOveridx*singleVol, 
                                    srcPtr_ + copyList[stepOveridx]*singleVol, 
                                    singleVol*tSize, memcpyType);
                    }
                }

                mManagedBuffers.emplace_back(std::move(new_manBuf));
                mDeviceBindings.emplace_back(mManagedBuffers.back()->deviceBuffer.data());
                continue;
            }
            else if (!copyListPtr && srcPtr && i == 0) {
                // One source from the last sub module.
                auto dims = mEngine->getBindingDimensions(i);
                dims.d[0] = batchSize;
                dims.d[1] = 7;
                // std::cout << "One source. Input size: " << dims << std::endl;
                std::shared_ptr<BertManagedBuffer> manBuf_ptr = srcPtr;
                mManagedBuffers.emplace_back(std::move(manBuf_ptr));
                mDeviceBindings.emplace_back(mManagedBuffers.back()->deviceBuffer.data());
                continue;
            }
            else if (i == 0) {
                auto dims = mEngine->getBindingDimensions(i);
                dims.d[0] = batchSize;
                dims.d[1] = 7;
                // std::cout << "No source. Input size: " << dims << std::endl;
            }

            auto dims = context ? context->getBindingDimensions(i) : mEngine->getBindingDimensions(i);
            //size_t vol = context || !mBatchSize ? 1 : static_cast<size_t>(mBatchSize);
            nvinfer1::DataType type = mEngine->getBindingDataType(i);
            /*
            int vecDim = mEngine->getBindingVectorizedDim(i);
            std::cout << "VecDim: " << vecDim << std::endl;
            if (-1 != vecDim) // i.e., 0 != lgScalarsPerVector
            {
                std::cout << "-1 != vecDim" << std::endl;
                int scalarsPerVec = mEngine->getBindingComponentsPerElement(i);
                std::cout << "scalarsPerVec: " << scalarsPerVec << std::endl;
                dims.d[vecDim] = divUp(dims.d[vecDim], scalarsPerVec);
                std::cout << "NewDims: " << dims << std::endl;
                vol *= scalarsPerVec;
            }
            */
            dims.d[0] = mBatchSize;
            dims.d[1] = 7;
            if (i==4){dims.d[1] = 2;}
            size_t vol = samplesCommon::volume(dims);
            std::shared_ptr<BertManagedBuffer> manBuf{new BertManagedBuffer(vol, type)};
            //mDeviceBindings.emplace_back(manBuf->deviceBuffer.data());
            mManagedBuffers.emplace_back(std::move(manBuf));
            //std::cout << mManagedBuffers.back()->deviceBuffer.data() <<std::endl;
            mDeviceBindings.emplace_back(mManagedBuffers.back()->deviceBuffer.data());
            std::cout << "dims: " << dims << std::endl;

            // if (i != 0) {
            //     auto dims = mEngine->getBindingDimensions(i);
            //     dims.d[0] = -1;
            //     // std::cout << "Output size: " << dims << std::endl;
            //     // std::cout << "\n" << std::endl;
            // }
        }
    }

    //!
    //! \brief Returns a vector of device buffers that you can use directly as
    //!        bindings for the execute and enqueue methods of IExecutionContext.
    //!
    std::vector<void*>& getDeviceBindings()
    {
        return mDeviceBindings;
    }

    //!
    //! \brief Returns a vector of device buffers.
    //!
    const std::vector<void*>& getDeviceBindings() const
    {
        return mDeviceBindings;
    }

    std::shared_ptr<BertManagedBuffer> getInputBuffer()
    {
        return mManagedBuffers[0];
    }

    std::shared_ptr<BertManagedBuffer> getOutputBuffer()
    {
        return mManagedBuffers[1];
    }

    std::shared_ptr<BertManagedBuffer> getImmediateBuffer(const int idx)
    {
        return mManagedBuffers[idx];
    }

    //!
    //! \brief Returns the device buffer corresponding to tensorName.
    //!        Returns nullptr if no such tensor can be found.
    //!
    void* getDeviceBuffer(const std::string& tensorName) const
    {
        return getBuffer(false, tensorName);
    }

    //!
    //! \brief Returns the host buffer corresponding to tensorName.
    //!        Returns nullptr if no such tensor can be found.
    //!
    void* getHostBuffer(const std::string& tensorName) const
    {
        return getBuffer(true, tensorName);
    }

    void* getInputHostBuffer() const
    {
        return mManagedBuffers[0]->hostBuffer.data();
    }

    void* getOutputHostBuffer() const
    {
        return mManagedBuffers[1]->hostBuffer.data();
    }


    //!
    //! \brief Returns the size of the host and device buffers that correspond to tensorName.
    //!        Returns kINVALID_SIZE_VALUE if no such tensor can be found.
    //!
    size_t size(const std::string& tensorName) const
    {
        int index = mEngine->getBindingIndex(tensorName.c_str());
        if (index == -1)
            return kINVALID_SIZE_VALUE;
        return mManagedBuffers[index]->hostBuffer.nbBytes();
    }

    //!
    //! \brief Dump host buffer with specified tensorName to ostream.
    //!        Prints error message to std::ostream if no such tensor can be found.
    //!
    void dumpBuffer(std::ostream& os, const std::string& tensorName)
    {
        int index = mEngine->getBindingIndex(tensorName.c_str());
        if (index == -1)
        {
            os << "Invalid tensor name" << std::endl;
            return;
        }
        void* buf = mManagedBuffers[index]->hostBuffer.data();
        size_t bufSize = mManagedBuffers[index]->hostBuffer.nbBytes();
        nvinfer1::Dims bufDims = mEngine->getBindingDimensions(index);
        size_t rowCount = static_cast<size_t>(bufDims.nbDims > 0 ? bufDims.d[bufDims.nbDims - 1] : mBatchSize);
        int leadDim = mBatchSize;
        int* trailDims = bufDims.d;
        int nbDims = bufDims.nbDims;

        // Fix explicit Dimension networks
        if (!leadDim && nbDims > 0)
        {
            leadDim = bufDims.d[0];
            ++trailDims;
            --nbDims;
        }

        os << "[" << leadDim;
        for (int i = 0; i < nbDims; i++)
            os << ", " << trailDims[i];
        os << "]" << std::endl;
        switch (mEngine->getBindingDataType(index))
        {
        case nvinfer1::DataType::kINT32: print<int32_t>(os, buf, bufSize, rowCount); break;
        case nvinfer1::DataType::kFLOAT: print<float>(os, buf, bufSize, rowCount); break;
        case nvinfer1::DataType::kHALF: print<half_float::half>(os, buf, bufSize, rowCount); break;
        case nvinfer1::DataType::kINT8: assert(0 && "Int8 network-level input and output is not supported"); break;
        case nvinfer1::DataType::kBOOL: assert(0 && "Bool network-level input and output are not supported"); break;
        }
    }

    //!
    //! \brief Templated print function that dumps buffers of arbitrary type to std::ostream.
    //!        rowCount parameter controls how many elements are on each line.
    //!        A rowCount of 1 means that there is only 1 element on each line.
    //!
    template <typename T>
    void print(std::ostream& os, void* buf, size_t bufSize, size_t rowCount)
    {
        assert(rowCount != 0);
        assert(bufSize % sizeof(T) == 0);
        T* typedBuf = static_cast<T*>(buf);
        size_t numItems = bufSize / sizeof(T);
        for (int i = 0; i < static_cast<int>(numItems); i++)
        {
            // Handle rowCount == 1 case
            if (rowCount == 1 && i != static_cast<int>(numItems) - 1)
                os << typedBuf[i] << std::endl;
            else if (rowCount == 1)
                os << typedBuf[i];
            // Handle rowCount > 1 case
            else if (i % rowCount == 0)
                os << typedBuf[i];
            else if (i % rowCount == rowCount - 1)
                os << " " << typedBuf[i] << std::endl;
            else
                os << " " << typedBuf[i];
        }
    }

    //!
    //! \brief Copy the contents of input host buffers to input device buffers synchronously.
    //!
    void copyInputToDevice()
    {
        memcpyBuffers(true, false, false);
    }

    //!
    //! \brief Copy the contents of output device buffers to output host buffers synchronously.
    //!
    void copyOutputToHost()
    {
        memcpyBuffers(false, true, false);
    }

    //!
    //! \brief Copy the contents of input host buffers to input device buffers asynchronously.
    //!
    void copyInputToDeviceAsync(const cudaStream_t& stream = 0)
    {
        memcpyBuffers(true, false, true, stream);
    }

    //!
    //! \brief Copy the contents of output device buffers to output host buffers asynchronously.
    //!
    void copyOutputToHostAsync(const cudaStream_t& stream = 0)
    {
        memcpyBuffers(false, true, true, stream);
    }

    ~BertBufferManager() = default;

private:
    void* getBuffer(const bool isHost, const std::string& tensorName) const
    {
        int index = mEngine->getBindingIndex(tensorName.c_str());
        if (index == -1)
            return nullptr;
        return (isHost ? mManagedBuffers[index]->hostBuffer.data() : mManagedBuffers[index]->deviceBuffer.data());
    }

    void memcpyBuffers(const bool copyInput, const bool deviceToHost, const bool async, const cudaStream_t& stream = 0)
    {
        for (int i = 0; i < mEngine->getNbBindings(); i++)
        {
            void* dstPtr
                = deviceToHost ? mManagedBuffers[i]->hostBuffer.data() : mManagedBuffers[i]->deviceBuffer.data();
            const void* srcPtr
                = deviceToHost ? mManagedBuffers[i]->deviceBuffer.data() : mManagedBuffers[i]->hostBuffer.data();
            const size_t byteSize = mManagedBuffers[i]->hostBuffer.nbBytes();
            const cudaMemcpyKind memcpyType = deviceToHost ? cudaMemcpyDeviceToHost : cudaMemcpyHostToDevice;
            if ((copyInput && mEngine->bindingIsInput(i)) || (!copyInput && !mEngine->bindingIsInput(i)))
            {
                if (async)
                    CUDACHECK(cudaMemcpyAsync(dstPtr, srcPtr, byteSize, memcpyType, stream));
                else
                    CUDACHECK(cudaMemcpy(dstPtr, srcPtr, byteSize, memcpyType));
            }
        }
    }

    std::shared_ptr<nvinfer1::ICudaEngine> mEngine;              //!< The pointer to the engine
    int mBatchSize;                                              //!< The batch size for legacy networks, 0 otherwise.
    int mCopyMethod;
    std::vector<std::shared_ptr<BertManagedBuffer>> mManagedBuffers; //!< The vector of pointers to managed buffers
    std::vector<void*> mDeviceBindings;                          //!< The vector of device buffers needed for engine execution
};


} // namespace samplesCommon

#endif // TENSORRT_BUFFERS_H
