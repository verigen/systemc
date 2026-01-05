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

  sc_cor_aco.h -- Coroutine implementation with libaco.

  Original Author: Mateusz Maciąg, Verigen, 2026-01-05

 *****************************************************************************/

#ifndef SC_COR_ACO_H
#define SC_COR_ACO_H

#if !defined(_WIN32) && !defined(WIN32) && defined(SC_USE_ACO)

#include "sysc/kernel/sc_cor.h"
#include "sysc/kernel/sc_cmnhdr.h"

// Include libaco headers
extern "C" {
#include "aco.h"
#include "aco_assert_override.h"
}

namespace sc_core {

class sc_cor_pkg_aco;

// ----------------------------------------------------------------------------
//  CLASS : sc_cor_aco
//
//  Coroutine class implemented with libaco.
// ----------------------------------------------------------------------------

class sc_cor_aco : public sc_cor
{
public:
    // constructor
    sc_cor_aco();

    // destructor
    virtual ~sc_cor_aco();

    // switch stack protection on/off
    void stack_protect(bool enable);

public:
    sc_cor_pkg_aco*     m_pkg;          // the creating coroutine package
    aco_t*              m_aco;          // libaco coroutine handle
    sc_cor_fn*          m_cor_fn;       // the coroutine function
    void*               m_cor_fn_arg;   // the coroutine function argument
    std::size_t         m_stack_size;   // requested stack size
    bool                m_is_main;      // is this the main coroutine

private:
    // disabled
    sc_cor_aco(const sc_cor_aco&);
    sc_cor_aco& operator=(const sc_cor_aco&);
};


// ----------------------------------------------------------------------------
//  CLASS : sc_cor_pkg_aco
//
//  Coroutine package class implemented with libaco.
// ----------------------------------------------------------------------------

class sc_cor_pkg_aco : public sc_cor_pkg
{
public:
    // constructor
    explicit sc_cor_pkg_aco(sc_simcontext* simc);

    // destructor
    virtual ~sc_cor_pkg_aco();

    // create a new coroutine
    virtual sc_cor* create(std::size_t stack_size, sc_cor_fn* fn, void* arg);

    // yield to the next coroutine
    virtual void yield(sc_cor* next_cor);

    // abort the current coroutine (and resume the next coroutine)
    virtual void abort(sc_cor* next_cor);

    // get the main coroutine
    virtual sc_cor* get_main();

    // get current coroutine
    sc_cor_aco* get_current() { return m_curr_cor; }

    // set current coroutine
    sc_cor_aco* set_current(sc_cor_aco* cor);

private:
    sc_cor_aco          m_main_cor;         // main coroutine
    sc_cor_aco*         m_curr_cor;         // current coroutine
    aco_share_stack_t*  m_share_stack;      // shared stack for non-main coroutines
    bool                m_initialized;      // libaco initialization flag

    // last word handler for offending coroutines
    static void last_word_handler();

private:
    // disabled
    sc_cor_pkg_aco();
    sc_cor_pkg_aco(const sc_cor_pkg_aco&);
    sc_cor_pkg_aco& operator=(const sc_cor_pkg_aco&);
};

} // namespace sc_core

#endif // !defined(_WIN32) && !defined(WIN32) && defined(SC_USE_ACO)

#endif // SC_COR_ACO_H

// Taf!
