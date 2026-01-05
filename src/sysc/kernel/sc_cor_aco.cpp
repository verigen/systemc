/*****************************************************************************

  Licensed to Accellera Systems Initiative Inc. (Accellera) under one or
  more contributor license agreements.  See the NOTICE file distributed
  with this work for additional information regarding copyright ownership.
  Accellera licenses this file to you under the Apache License, Version 2.0
  (the "License"); you may not use this file except in compliance with the
  License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
  implied.  See the License for the specific language governing
  permissions and limitations under the License.

 *****************************************************************************/

/*****************************************************************************

  sc_cor_aco.cpp -- Coroutine implementation with libaco.

  Original Author: Mateusz Maciąg, Verigen, 2026-01-05

 *****************************************************************************/

#if !defined(_WIN32) && !defined(WIN32) && defined(SC_USE_ACO)

#include <cstring>
#include <sstream>

#include "sysc/kernel/sc_cor_aco.h"
#include "sysc/kernel/sc_simcontext.h"
#include "sysc/utils/sc_report.h"

using namespace std;

namespace sc_core {

#define DEBUGF \
    if (0) std::cout << "sc_cor_aco.cpp(" << __LINE__ << ") "

// Default stack size for shared stack (2MB)
static const std::size_t DEFAULT_SHARE_STACK_SIZE = 2 * 1024 * 1024;

// Default save stack size (64 bytes, will grow automatically)
static const std::size_t DEFAULT_SAVE_STACK_SIZE = 64;

// ----------------------------------------------------------------------------
//  CLASS : sc_cor_aco
//
//  Coroutine class implemented with libaco.
// ----------------------------------------------------------------------------

// constructor
sc_cor_aco::sc_cor_aco()
    : m_pkg(nullptr)
    , m_aco(nullptr)
    , m_cor_fn(nullptr)
    , m_cor_fn_arg(nullptr)
    , m_stack_size(0)
    , m_is_main(false)
{
    DEBUGF << this << ": sc_cor_aco::sc_cor_aco()" << std::endl;
}

// destructor
sc_cor_aco::~sc_cor_aco()
{
    DEBUGF << this << ": sc_cor_aco::~sc_cor_aco()" << std::endl;

    // Destroy the libaco coroutine
    // Main coroutine should not call aco_destroy
    if (m_aco != nullptr && !m_is_main) {
        aco_destroy(m_aco);
        m_aco = nullptr;
    }
}

// switch stack protection on/off
void sc_cor_aco::stack_protect(bool enable)
{
    // libaco's aco_share_stack_new2 supports guard pages
    // Protection is set at share stack creation time
    // This function is provided for interface compatibility
    DEBUGF << this << ": stack_protect(" << enable
           << ") - controlled at share stack creation" << std::endl;
}

// ----------------------------------------------------------------------------
//  CLASS : sc_cor_pkg_aco
//
//  Coroutine package class implemented with libaco.
// ----------------------------------------------------------------------------

// Static last word handler for offending coroutines
void sc_cor_pkg_aco::last_word_handler()
{
    aco_t* offending_co = aco_get_co();

    std::cerr << "SystemC Error: Coroutine " << offending_co
              << " terminated without calling aco_exit()" << std::endl;
    std::cerr << "  This is a programming error in the coroutine function."
              << std::endl;
    std::cerr << "  All non-main coroutines must call aco_exit() before returning."
              << std::endl;

    // The process will be aborted by libaco after this function returns
}

// Wrapper function for SystemC coroutines to work with libaco
static void aco_wrapper_function()
{
    // Get the coroutine that's currently executing
    aco_t* current_aco = aco_get_co();

    // The SystemC coroutine info is stored in the arg field
    sc_cor_aco* cor = static_cast<sc_cor_aco*>(current_aco->arg);

    DEBUGF << cor << ": aco_wrapper_function() starting" << std::endl;

    // Set as current coroutine in the package
    cor->m_pkg->set_current(cor);

    // Invoke the actual SystemC coroutine function
    (cor->m_cor_fn)(cor->m_cor_fn_arg);

    DEBUGF << cor << ": aco_wrapper_function() completed, calling aco_exit()"
           << std::endl;

    // CRITICAL: Must call aco_exit() instead of return
    // This is a libaco requirement for non-main coroutines
    aco_exit();

    // Never reached
}

// constructor
sc_cor_pkg_aco::sc_cor_pkg_aco(sc_simcontext* simc)
    : sc_cor_pkg(simc)
    , m_main_cor()
    , m_curr_cor(&m_main_cor)
    , m_share_stack(nullptr)
    , m_initialized(false)
{
    DEBUGF << &m_main_cor << ": sc_cor_pkg_aco::sc_cor_pkg_aco()" << std::endl;

    // Initialize libaco for this thread
    aco_thread_init(last_word_handler);
    m_initialized = true;

    // Create main coroutine
    m_main_cor.m_pkg = this;
    m_main_cor.m_is_main = true;
    m_main_cor.m_aco = aco_create(nullptr, nullptr, 0, nullptr, nullptr);

    if (m_main_cor.m_aco == nullptr) {
        SC_REPORT_ERROR(SC_ID_COROUTINE_ERROR_,
                       "failed to create main coroutine with libaco");
        sc_abort();
    }

    // Create shared stack for non-main coroutines with guard page enabled
    m_share_stack = aco_share_stack_new2(DEFAULT_SHARE_STACK_SIZE, 1);

    if (m_share_stack == nullptr) {
        SC_REPORT_ERROR(SC_ID_COROUTINE_ERROR_,
                       "failed to create shared stack with libaco");
        sc_abort();
    }

    DEBUGF << &m_main_cor << ": is main co-routine, aco=" << m_main_cor.m_aco
           << std::endl;
}

// destructor
sc_cor_pkg_aco::~sc_cor_pkg_aco()
{
    DEBUGF << "sc_cor_pkg_aco::~sc_cor_pkg_aco()" << std::endl;

    // Destroy shared stack (all coroutines using it should be destroyed first)
    if (m_share_stack != nullptr) {
        aco_share_stack_destroy(m_share_stack);
        m_share_stack = nullptr;
    }

    // Destroy main coroutine
    if (m_main_cor.m_aco != nullptr) {
        aco_destroy(m_main_cor.m_aco);
        m_main_cor.m_aco = nullptr;
    }
}

// set current coroutine
sc_cor_aco* sc_cor_pkg_aco::set_current(sc_cor_aco* cor)
{
    sc_cor_aco* old_cor = m_curr_cor;
    m_curr_cor = cor;
    return old_cor;
}

// create a new coroutine
sc_cor* sc_cor_pkg_aco::create(std::size_t stack_size, sc_cor_fn* fn, void* arg)
{
    sc_cor_aco* cor = new sc_cor_aco;

    DEBUGF << &m_main_cor << ": sc_cor_pkg_aco::create(" << cor << ")"
           << std::endl;

    // Initialize coroutine fields
    cor->m_pkg = this;
    cor->m_cor_fn = fn;
    cor->m_cor_fn_arg = arg;
    cor->m_is_main = false;

    // Determine save stack size
    // Use the provided stack_size as hint for the save stack
    // libaco will automatically resize if needed
    std::size_t save_stack_sz = (stack_size > 0) ? stack_size : DEFAULT_SAVE_STACK_SIZE;
    cor->m_stack_size = save_stack_sz;

    // Create the libaco coroutine
    // The coroutine will use our shared stack and the wrapper function
    // We pass 'cor' as the arg so the wrapper can access the SystemC coroutine info
    cor->m_aco = aco_create(
        m_main_cor.m_aco,           // main_co: parent coroutine
        m_share_stack,              // share_stack: shared execution stack
        save_stack_sz,              // save_stack_sz: private save stack size
        aco_wrapper_function,       // co_fp: wrapper function
        cor                         // arg: pointer to sc_cor_aco
    );

    if (cor->m_aco == nullptr) {
        delete cor;
        SC_REPORT_ERROR(SC_ID_COROUTINE_ERROR_,
                       "failed to create coroutine with libaco");
        sc_abort();
        return nullptr; // Never reached
    }

    DEBUGF << &m_main_cor << ": created libaco coroutine, aco=" << cor->m_aco
           << std::endl;
    DEBUGF << &m_main_cor << ": exiting sc_cor_pkg_aco::create(" << cor << ")"
           << std::endl;

    return cor;
}

// yield to the next coroutine
void sc_cor_pkg_aco::yield(sc_cor* next_cor_p)
{
    sc_cor_aco* from_cor = m_curr_cor;
    sc_cor_aco* to_cor = static_cast<sc_cor_aco*>(next_cor_p);

    DEBUGF << from_cor << ": yield to " << to_cor << std::endl;

    if (to_cor == from_cor) {
        DEBUGF << from_cor << ": yielding to self, no-op" << std::endl;
        return;
    }

    // Set the target as current before switching
    // Note: libaco maintains its own current coroutine tracking via aco_gtls_co
    set_current(to_cor);

    if (from_cor->m_is_main) {
        // Yielding from main to a non-main coroutine
        // Use aco_resume to start/continue the target coroutine
        DEBUGF << from_cor << ": main resuming non-main " << to_cor
               << ", aco=" << to_cor->m_aco << std::endl;

        aco_resume(to_cor->m_aco);

        // When we return here, the non-main coroutine has yielded back to us
        DEBUGF << from_cor << ": main resumed after non-main yielded" << std::endl;

    } else {
        // Yielding from non-main coroutine back to main
        // Use aco_yield to return control to main coroutine
        DEBUGF << from_cor << ": non-main yielding to main" << std::endl;

        // Note: In libaco's asymmetric model, non-main coroutines can only
        // yield back to their main_co. Direct non-main to non-main switching
        // is not supported. SystemC's symmetric model needs to be adapted:
        // - Non-main yields to main
        // - Main then resumes the next non-main

        if (to_cor->m_is_main) {
            // Direct yield to main
            aco_yield();
        } else {
            // Need to go through main for non-main to non-main switch
            // First yield to main
            aco_yield();
            // Main will then resume the target (this is handled by the caller)
        }

        DEBUGF << from_cor << ": non-main resumed after yield" << std::endl;
    }

    // Restore current after context switch
    set_current(from_cor);

    DEBUGF << from_cor << ": restarting after yield to " << to_cor << std::endl;
}

// abort the current coroutine (and resume the next coroutine)
void sc_cor_pkg_aco::abort(sc_cor* next_cor_p)
{
    sc_cor_aco* from_cor = m_curr_cor;
    sc_cor_aco* to_cor = static_cast<sc_cor_aco*>(next_cor_p);

    DEBUGF << from_cor << ": aborting, switching to " << to_cor << std::endl;

    // Set next coroutine as current
    set_current(to_cor);

    // For abort, we exit the current coroutine and resume the next
    // This is similar to yield but the current coroutine won't be resumed

    if (from_cor->m_is_main) {
        // Main cannot be aborted in libaco's model
        // This should not happen in SystemC
        SC_REPORT_WARNING(SC_ID_COROUTINE_ERROR_,
                         "attempt to abort main coroutine");
    } else {
        // Non-main coroutine abort
        // Call aco_exit which will set is_end flag and yield to main
        DEBUGF << from_cor << ": calling aco_exit()" << std::endl;
        aco_exit();
        // Never returns
    }
}

// get the main coroutine
sc_cor* sc_cor_pkg_aco::get_main()
{
    return &m_main_cor;
}

} // namespace sc_core

#endif // !defined(_WIN32) && !defined(WIN32) && defined(SC_USE_ACO)

// Taf!
