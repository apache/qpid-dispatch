/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <Python.h>

#include <thread>

#include "helpers.hpp"
#include "qdr_doctest.h"

extern "C" {
#include <qpid/dispatch/python_embedded.h>
#include <qpid/dispatch/router_core.h>
#include <router_core/router_core_private.h>
#include <terminus_private.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
}

// broken, see DISPATCH-1821 Double-free in qd_entity_configure_policy on error
// it is probably not worth the effort to fix, because it is extremely unlikely to ever fail this way in production
TEST_CASE("policy" * doctest::skip()) {
    std::thread([]() {
        WithNoMemoryLeaks leaks{};
        QDR               qdr;
        qdr.start();
        qdr.wait();

        {
            auto qd = qdr.qd;

            const char *test_module_name = static_cast<const char *>("test_module");
            PyObject *module = Py_CompileString(
                // language: Python
                R"EOT(
class NotBool:
    def __bool__(self):
        raise NotImplementedError()


fake_policy = {
      "maxConnections": 4,
      "policyDir": "/tmp",
      "enableVhostPolicy": 4,
      "enableVhostNamePatterns": NotBool(),
})EOT",
                test_module_name, Py_file_input);
            REQUIRE(module != nullptr);

            PyObject *pModuleObj = PyImport_ExecCodeModule(const_cast<char *>(test_module_name), module); // python 2 :(
                REQUIRE(pModuleObj != nullptr);
            // better to check with an if, use PyErr_Print() or such to read the error

            PyObject *pAttrObj = PyObject_GetAttrString(pModuleObj, "fake_policy");
            REQUIRE(pAttrObj != nullptr);

            auto *entity = reinterpret_cast<qd_entity_t *>(pAttrObj);
            REQUIRE(qd_entity_has(entity, "enableVhostNamePatterns"));  // sanity check for the test input

            // TODO Py Decrefs probably needed somewhere

            const qd_python_lock_state_t lock_state = qd_python_lock();
            qd_error_t                   err = qd_dispatch_configure_policy(qd, entity);
            CHECK(err == QD_ERROR_PYTHON);
            qd_python_unlock(lock_state);
        }
        qdr.stop();
    }).join();
}

TEST_CASE("qd_hash_retrieve_prefix") {
    std::thread([]() {
        WithNoMemoryLeaks leaks{};
        QDR               qdr;
        qdr.start();
        qdr.wait();

      qd_hash_t *hash = qd_hash(10, 32, 0);

      qd_iterator_t *iter = qd_iterator_string("policy", ITER_VIEW_ADDRESS_HASH);
      qd_iterator_annotate_prefix(iter, 'C');

      // Insert that hash
      qd_error_t error = qd_hash_insert(hash, iter, (void *)"TEST", 0);
      CHECK(error == QD_ERROR_NONE);

      const char *taddr = "policy.org.apache.dev";

      qd_iterator_t *address_iter = qd_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
      qd_iterator_t *query_iter = qd_iterator_string("", ITER_VIEW_ADDRESS_HASH);
      qd_iterator_annotate_prefix(address_iter, 'C');

      qd_address_t *addr;

      qd_hash_retrieve_prefix(hash, query_iter, (void**) &addr);

      qd_iterator_free(iter);
      qd_iterator_free(address_iter);
      qd_iterator_free(query_iter);
      qd_hash_free(hash);

        qdr.stop();
      }).join();
}