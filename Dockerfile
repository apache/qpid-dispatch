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

# Downloads, builds and installs apache proton and qpid dispatch on Centos and starts the dispatch router
# /main is the top level folder under which proton (/main/qpid-proton/) and dispatch (/main/qpid-dispatch) source code is downloaded from github
# /usr/lib64 is the folder in which the proton artifacts are installed.
# /usr/sbin is the folder in which dispatch executable installed


# Copy this docker file to your local folder. Build the docker image like this:
# > sudo docker build -t <username>/dispatch --file=Dockerfile .

# "<username>/dispatch" is a convention, you can call it whatever you want

# To launch a container running the new image:
# > sudo docker run -i -t <username>/dispatch

# To launch a container running the new image with an interactive shell prompt:
# > sudo docker run -i -t <username>/dispatch /bin/bash

################# Begin code #######

# Get the latest Centos version from dockerhub
FROM centos:latest

MAINTAINER "dev@qpid.apache.org"

# Install all the required packages. Some in this list were picked off from proton's INSTALL.md (https://github.com/apache/qpid-proton/blob/master/INSTALL.md) and the rest are from dispatch (https://github.com/apache/qpid-dispatch/blob/master/README)

# For centos, some packages are found in the epel repo, so first install access to it
RUN yum -y install epel-release

# now install the rest of the packages
RUN yum -y install gcc cmake libuuid-devel openssl-devel cyrus-sasl-devel cyrus-sasl-plain cyrus-sasl-gssapi cyrus-sasl-md5 swig python-devel java-1.8.0-openjdk-devel git make doxygen valgrind emacs libuv libuv-devel libwebsockets-devel python-unittest2 && yum clean all -y

# Create a main directory and clone the qpid-proton repo from github
RUN mkdir /main && cd /main && git clone https://github.com/apache/qpid-proton.git  && cd /main/qpid-proton && mkdir /main/qpid-proton/build

WORKDIR /main/qpid-proton/build

# make and install proton
RUN cmake .. -DSYSINSTALL_BINDINGS=ON -DCMAKE_INSTALL_PREFIX=/usr -DSYSINSTALL_PYTHON=ON && make install

# Clone the qpid-dispatch git repo
RUN cd /main && git clone https://github.com/apache/qpid-dispatch.git && mkdir /main/qpid-dispatch/build

WORKDIR /main/qpid-dispatch/build
RUN cmake .. -DCMAKE_INSTALL_PREFIX=/usr && make install

# Uncomment the following line if you would like to run all the dispatch unit tests and system tests. 
# RUN ctest -VV

# Start the dispatch router
ENTRYPOINT ["qdrouterd"]

