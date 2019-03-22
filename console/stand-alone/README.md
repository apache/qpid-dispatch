#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# dispatch-standalone
The stand-alone qpid dispatch console is an html web site that monitors and controls a qpid dispatch router

## To install the console:

  The console files are installed during a dispatch `make install`. By default, the console files are installed in /usr/share/qpid-dispatch/console

## To run the web console:
- Ensure one of the routers in your network is configured with a normal listener with `http: true`

```
listener {
    role: normal
    host: 0.0.0.0
    port: 5673
    http: true
    saslMechanisms: ANONYMOUS
}
```
- start the router
- in a browser, navigate to http://localhost:5673/

The router will serve the console's html/js/css from the install directory.
The console will automatically connect to the router at localhost:5673

## To disable the console and still allow the router to respond to other http traffic

- in the listener portion of the config file, add an httpRoot: None

```
listener {
    role: normal
    host: 0.0.0.0
    port: 5673
    http: true
    httpRoot: None
    saslMechanisms: ANONYMOUS
}
```


