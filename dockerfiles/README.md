####Docker 
Docker is an open-source project that automates the deployment of applications inside software
containers, by providing an additional layer of abstraction and automation of operating-system-level
virtualization on Linux.
<https://www.docker.com/what-docker>

####Dockerfiles for Fedora and Ubuntu
The two docker files, Dockerfile-fedora and Dockerfile-ubuntu provide quick start
docker containers with an running instance of qpid dispatch router.

The dockerfiles follow these steps before launching the dispatch router

* Downloads all the fedora or ubuntu specific dependencies needed for building apache qpid-proton.
* Downloads the source code of qpid-proton to /main/qpid-proton
* Builds and installs qpid-proton using gcc. /usr/local/lib64 is the folder in which the qpid-proton artifacts are installed.
* Downloads the source code of qpid-dispatch to /main/qpid-dispatch.
* Builds and installs qpid-dispatch using gcc.  /usr/local/sbin is the folder in which qpid-dispatch executable is installed
* Launch qpid-dispatch router

####Building and running Dockerfiles
* To build the fedora docker file
 * sudo docker build -t username/dispatch-fedora:latest --file=Dockerfile-fedora  .  (substitute username with your username e.g. johndoe)
* To build the ubuntu docker file
 * sudo docker build -t username/dispatch-ubuntu:latest --file=Dockerfile-ubuntu  .  (substitute username with your username e.g. johndoe)
* To run the fedora docker file
 * sudo docker run -i -t username/dispatch-fedora:latest (substitute username with your username e.g. johndoe)
* To run the ubuntu docker file
 * sudo docker run -i -t username/dispatch-ubuntu:latest (substitute username with your username e.g. johndoe)
* After the docker run command is invoked, execute the ___sudo docker ps___ command to check if your container name shows up in the list of docker processes.

####Customizing Docker files
* If you want to change the router configuration by modifying the contents of qdrouterd.conf (/etc/qpid-dispatch/qdrouterd.conf), replace the last line in the Dockerfile that launches the dispatch router (CMD ["qdrouterd"]) with RUN ["/bin/bash"] and build and run the container. This will put you inside the container from where you can modify the contents of qdrouterd.conf and relaunch the dispatch router to use the modified config.
* Uncomment the line RUN ctest -VV to run dispatch unit and system tests


