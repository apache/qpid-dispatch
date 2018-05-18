<!---
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
--->
This utility is deprecated and has been moved to a separate npm repository.
This directory will be removed in a future release of interconnect.

To install from the repository, in a new directory:

```npm install dispatch-topology```


Qpid Dispatch config file read/update/write utility
=============

A utility to read, update, write, and deploy dispatch router config files.

Dependencies
============

- tested using python 2.7
- npm to install the 3rd party javascript libraries
  http://blog.npmjs.org/post/85484771375/how-to-install-npm

- ansible if you wish to deploy the routers
http://docs.ansible.com/ansible/latest/intro_installation.html

Running
====================

Install the 3rd party javascript libraries:
- in the console/config/ directory run
  npm install
This will create a node_modules/ directory and install the needed files

- run ./config.py
- in the address bar of a browser, enter localhost:8000



