.. Licensed to the Apache Software Foundation (ASF) under one
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

Console operation
=================

Logging in to a router network
------------------------------

The console communicates to the router network using the proton javascript bindings. When run from a web page, the proton bindings use web sockets to send and receive commands. However, the dispatch router requires tcp. Therefore a web-sockets to tcp proxy is used. 

.. image:: console_login.png
   :height: 500px
   :width: 1000 px

Enter the address of a proxy that is connected to a router in the network.

User name and password are not used at this time.

The Autostart checkbox, when checked, will automatically log in with the previous host:port the next time you start the console.

Overview page
-------------

On the overview page, aggregate information about routers, addresses, and connections is displayed.

.. image:: console_overview.png
   :height: 500px
   :width: 1000 px

Topology page
-------------

This page displays the router network in a graphical form showing how the routers are connected and information about the individual routers and links.

.. image:: console_topology.png
   :height: 500px
   :width: 1000 px

Router entity details page
--------------------------

.. image:: console_entity.png
   :height: 500px
   :width: 1000 px

Displays detailed information about entities such as routers, links, addresses, memory.

Numeric attributes can be graphed by clicking on the graph icon.

Charts page
-----------

.. image:: console_charts.png
   :height: 500px
   :width: 1000 px

This page displays graphs of numeric values that are on the entity details page.

Schema page
-----------

.. image:: console_schema.png
   :height: 500px
   :width: 1000 px

This page displays the json schema that is used to manage the router network.

