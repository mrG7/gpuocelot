/*! \file BasicBlockInstrumentor.cpp
	\date Monday November 15, 2010
	\author Naila Farooqui <naila@cc.gatech.edu>
	\brief The source file for the BasicBlockInstrumentor class.
*/

#ifndef BASIC_BLOCK_INSTRUMENTOR_CPP_INCLUDED
#define BASIC_BLOCK_INSTRUMENTOR_CPP_INCLUDED

#include <ocelot/analysis/interface/BasicBlockInstrumentor.h>

#include <ocelot/cuda/interface/cuda_runtime.h>

#include <ocelot/analysis/interface/MemoryIntensityPass.h>
#include <ocelot/analysis/interface/DynamicInstructionCountPass.h>
#include <ocelot/analysis/interface/BasicBlockExecutionCountPass.h>
#include <ocelot/analysis/interface/BasicBlockInstrumentationPass.h>
#include <ocelot/ir/interface/Module.h>

#include <hydrazine/implementation/ArgumentParser.h>
#include <hydrazine/implementation/string.h>
#include <hydrazine/implementation/debug.h>
#include <hydrazine/implementation/Exception.h>

#include <fstream>

using namespace hydrazine;

namespace analysis
{

    void BasicBlockInstrumentor::checkConditions() {
        conditionsMet = true;
    }

    void BasicBlockInstrumentor::analyze(ir::Module &module) {
        basicBlocks = 0;
        if(!kernelName.empty()){
            basicBlocks = module.kernels().find(kernelName)->second->dfg()->size() - 2;
        }
        else {
            for (ir::Module::KernelMap::const_iterator kernel = module.kernels().begin(); 
		        kernel != module.kernels().end(); ++kernel) {
                basicBlocks += (kernel->second)->dfg()->size();
            }
        }


        ir::Kernel *kernel = module.kernels().find(kernelName)->second;
        for(ir::ControlFlowGraph::const_iterator basicBlock = kernel->cfg()->begin();
            basicBlock != kernel->cfg()->end(); ++basicBlock) {
            if(basicBlock->label == "entry" || basicBlock->label == "exit")
                continue;
            labels.push_back(basicBlock->label);
        }

        
    }

    void BasicBlockInstrumentor::initialize() {
        
        counter = 0;

        if(cudaMalloc((void **) &counter, entries * basicBlocks * threadBlocks * threads * sizeof(size_t)) != cudaSuccess){
            throw hydrazine::Exception( "Could not allocate sufficient memory on device (cudaMalloc failed)!" );
        }
        if(cudaMemset( counter, 0, entries * basicBlocks * threadBlocks * threads * sizeof( size_t )) != cudaSuccess){
            throw hydrazine::Exception( "cudaMemset failed!" );
        }
        
        if(cudaMemcpyToSymbol(((BasicBlockInstrumentationPass *)pass)->basicBlockCounterBase().c_str(), &counter, sizeof(*counter), 0, cudaMemcpyHostToDevice) != cudaSuccess) {
            throw hydrazine::Exception( "cudaMemcpyToSymbol failed!");
        }
    }

    analysis::Pass *BasicBlockInstrumentor::createPass() {
        
        analysis::BasicBlockInstrumentationPass *basicBlockPass;
        entries = 1;        

        switch(type) {
            case executionCount:
                basicBlockPass = new analysis::BasicBlockExecutionCountPass;
                break;
            case instructionCount:
                basicBlockPass = new analysis::DynamicInstructionCountPass;
                break;
            case memoryIntensity:
                basicBlockPass = new analysis::MemoryIntensityPass;
                entries = 2;
                break;
            default:
                throw hydrazine::Exception( "No basic block instrumentation pass specified!" );
        }

        basicBlockPass->entries = entries;
        return basicBlockPass;          
    }

    size_t* BasicBlockInstrumentor::extractResults(std::ostream *out) {

        size_t *info = new size_t[entries * basicBlocks * threads * threadBlocks];
        if(counter) {
            cudaMemcpy(info, counter, entries * basicBlocks * threads * threadBlocks * sizeof( size_t ), cudaMemcpyDeviceToHost);
            cudaFree(counter);
        }

        *out << "{\n\"kernel\": " << kernelName << ",\n";
        *out << "\n\"threadBlocks\": " << threadBlocks << ",\n";
        *out << "\n\"threads\": " << threads << ",\n";
        *out << "\n\"counters\": {\n";

        _kernelProfile.basicBlockExecutionCountMap.clear();
        _kernelProfile.memoryOperationsMap.clear();

        size_t i = 0;
        size_t j = 0;
        size_t k = 0;
        
        for(k = 0; k < threadBlocks; k++) {
            for(i = 0; i < basicBlocks; i++) {
                for(j = 0; j < (threads * entries); j = j + entries) {
                    _kernelProfile.basicBlockExecutionCountMap[i] += info[(i * entries * threads) + (k * basicBlocks * threads * entries) + j];
                    if(type == memoryIntensity)
                        _kernelProfile.memoryOperationsMap[i] += info[(i * entries * threads) + (k * basicBlocks * threads * entries) + (j + 1)];
                    
                }
            }   
        }

        for(j = 0; j < basicBlocks; j++) {
            *out << "\"" << labels[j] << "\": " << _kernelProfile.basicBlockExecutionCountMap[j] << ", ";
            if(type == memoryIntensity)
                 *out << _kernelProfile.memoryOperationsMap[j];
        
            *out << "\n";
        }
        
        *out << "\n}\n}\n";

        return info;
    }

    BasicBlockInstrumentor::BasicBlockInstrumentor() : description("Basic Block Execution Count Per Thread") {
    }
    

}

#endif