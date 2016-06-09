// Copyright (c) 2015, Lawrence Livermore National Security, LLC.  
// Produced at the Lawrence Livermore National Laboratory.
//
// This file is part of Caliper.
// Written by David Boehme, boehme3@llnl.gov.
// LLNL-CODE-678900
// All rights reserved.
//
// For details, see https://github.com/scalability-llnl/Caliper.
// Please also see the LICENSE file for our additional BSD notice.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
//  * Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the disclaimer below.
//  * Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the disclaimer (as noted below) in the documentation and/or other materials
//    provided with the distribution.
//  * Neither the name of the LLNS/LLNL nor the names of its contributors may be used to endorse
//    or promote products derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
// OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// LAWRENCE LIVERMORE NATIONAL SECURITY, LLC, THE U.S. DEPARTMENT OF ENERGY OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

///@file  Papi.cpp
///@brief PAPI provider for caliper records

#include "../CaliperService.h"

#include <Caliper.h>
#include <EntryList.h>

#include <RuntimeConfig.h>
#include <ContextRecord.h>
#include <Log.h>

#include <util/split.hpp>

#include <pthread.h>

#include <vector>

using namespace cali;
using namespace std;

#include <papi.h>

namespace 
{

#define MAX_COUNTERS 16
    
struct PapiGlobalInfo {
    std::vector<cali_id_t> counter_attrbs;
    std::vector<int>       counter_events;
} global_info;

pthread_key_t counter_array_key;
size_t        num_failed = 0;
    
static const ConfigSet::Entry s_configdata[] = {
    { "counters", CALI_TYPE_STRING, "",
      "List of PAPI events to record",
      "List of PAPI events to record, separated by ':'" 
    },
    ConfigSet::Terminator
};

    
void destroy_counter_array(void* array)
{
    PAPI_unregister_thread();
    
    delete[] static_cast<long long*>(array);
}
    
long long* get_counter_array(bool alloc, size_t num_counters)
{
    long long* array = static_cast<long long*>(pthread_getspecific(counter_array_key));

    if (!array && alloc && num_counters > 0) {
        array = new long long[num_counters];
        
        pthread_setspecific(counter_array_key, array);

        // Register thread and start counters on new thread
        
        PAPI_register_thread();

        if (PAPI_start_counters(global_info.counter_events.data(), num_counters) != PAPI_OK)
            Log(0).stream() << "PAPI counters failed to initialize!" << endl;
    }
    
    return array;
}
    
void snapshot_cb(Caliper* c, int scope, const EntryList*, EntryList* snapshot) {
    auto num_counters = global_info.counter_events.size();
    
    if (num_counters < 1)
        return;

    long long* counter_values = get_counter_array(!c->is_signal(), num_counters);

    if (!counter_values)
        return;

    if (PAPI_accum_counters(counter_values, num_counters) != PAPI_OK) {
        ++num_failed;
        return;
    }

    Variant data[MAX_COUNTERS];

    int m = std::min(static_cast<int>(num_counters), MAX_COUNTERS);

    for (int i = 0; i < m; ++i)
        data[i] = Variant(static_cast<uint64_t>(counter_values[i]));

    snapshot->append(m, global_info.counter_attrbs.data(), data);
}

void papi_init(Caliper* c) {
    // start counters
    get_counter_array(true, global_info.counter_events.size());
}

void papi_finish(Caliper* c) {
    if (num_failed > 0)
        Log(1).stream() << "PAPI: Failed to read counters " << num_failed
                        << " times." << std::endl;
}

void setup_events(Caliper* c, const string& eventstring)
{
    vector<string> events;

    util::split(eventstring, ':', back_inserter(events));

    for (string& event : events) {
        int code;

        if (PAPI_event_name_to_code(const_cast<char*>(event.c_str()), &code) != PAPI_OK) {
            Log(0).stream() << "Unable to register PAPI counter \"" << event << '"' << endl;
            continue;
        }

        if (global_info.counter_events.size() < MAX_COUNTERS) {
            Attribute attr =
                c->create_attribute(string("papi.")+event, CALI_TYPE_UINT,
                                    CALI_ATTR_ASVALUE | CALI_ATTR_SCOPE_THREAD | CALI_ATTR_SKIP_EVENTS);

            global_info.counter_events.push_back(code);
            global_info.counter_attrbs.push_back(attr.id());
        } else
            Log(0).stream() << "Maximum number of PAPI counters exceeded; dropping \"" 
                            << event << '"' << endl;
    }
}

// Initialization handler
void papi_register(Caliper* c) {
    int ret = PAPI_library_init(PAPI_VER_CURRENT);
    
    if (ret != PAPI_VER_CURRENT && ret > 0) {
        Log(0).stream() << "PAPI version mismatch: found " 
                        << ret << ", expected " << PAPI_VER_CURRENT << endl;
        return;
    }        

    PAPI_thread_init(pthread_self);
    
    if (PAPI_is_initialized() == PAPI_NOT_INITED) {
        Log(0).stream() << "PAPI library is not initialized" << endl;
        return;
    }

    setup_events(c, RuntimeConfig::init("papi", s_configdata).get("counters").to_string());

    if (global_info.counter_events.size() < 1) {
        Log(1).stream() << "No PAPI counters registered, dropping PAPI service" << endl;
        return;
    }

    if (pthread_key_create(&counter_array_key, destroy_counter_array) != 0) {
        Log(0).stream() << "papi: error: pthread_key_create() failed" << std::endl;
        return;
    }
    
    // add callback for Caliper::get_context() event
    c->events().post_init_evt.connect(&papi_init);
    c->events().finish_evt.connect(&papi_finish);
    c->events().snapshot.connect(&snapshot_cb);

    Log(1).stream() << "Registered PAPI service" << endl;
}

} // namespace


namespace cali 
{
    CaliperService PapiService = { "papi", ::papi_register };
} // namespace cali
