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
# Copy this docker file to your local folder. Build the docker file like this - sudo docker build -t <username>/dispatch --file=Dockerfile-ubuntu . (don't miss the dot at the end)
# To run it - sudo docker run -i -t <username>/dispatch (this will launch the dispatch router)

# Gets the trusty Ubuntu from dockerhub
FROM ubuntu:latest

MAINTAINER "dev@qpid.apache.org"

# Install all the required packages. Some in this list were picked off from proton's INSTALL.md (https://github.com/apache/qpid-proton/blob/master/INSTALL.md) and the rest are from dispatch (https://github.com/apache/qpid-dispatch/blob/master/README)
RUN apt-get update && \
    apt-get install -y automake libtool zlib1g-dev cmake libsasl2-dev libssl-dev python python-dev libuv1-dev sasl2-bin swig maven git python-unittest2 && \
    apt-get -y clean

RUN git clone https://gitbox.apache.org/repos/asf/qpid-dispatch.git && cd /qpid-dispatch && git submodule add -b v2.1-stable https://github.com/warmcat/libwebsockets && git submodule add https://gitbox.apache.org/repos/asf/qpid-proton.git && git submodule update --init

WORKDIR /qpid-dispatch

RUN mkdir libwebsockets/build && cd /qpid-dispatch/libwebsockets/build && cmake .. -DCMAKE_INSTALL_PREFIX=/usr && make install

WORKDIR /qpid-dispatch

RUN mkdir qpid-proton/build && cd qpid-proton/build && cmake .. -DSYSINSTALL_BINDINGS=ON -DCMAKE_INSTALL_PREFIX=/usr -DSYSINSTALL_PYTHON=ON && make install

WORKDIR /qpid-dispatch

RUN mkdir build && cd build && cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DUSE_VALGRIND=NO && cmake --build . --target install

# Add site-packages to the PYTHONPATH environment variable. This is because Ubuntu does not list the site-packages folder in the sys.path
ENV PYTHONPATH=/usr/lib/python2.7/site-packages

# Uncomment the following line if you would like to run all the dispatch unit tests and system tests. 
#RUN cd /qpid-dispatch/build && ctest -VV

WORKDIR /qpid-dispatch

# Start the dispatch router
ENTRYPOINT ["qdrouterd"]
#CMD ["/bin/bash"]
