####Docker 
Docker is an open-source project that automates the deployment of applications inside software
containers, by providing an additional layer of abstraction and automation of operating-system-level
virtualization on Linux.
<https://www.docker.com/what-docker>

####Dockerfiles for Fedora and Ubuntu
The two docker files, Dockerfile-fedora and Dockerfile-ubuntu provide quick start
docker containers with an running instance of qpid dispatch router on RHEL and Debian based systems respectively.

The dockerfiles follow these steps before launching the dispatch router

* Downloads all the fedora or ubuntu specific dependencies needed for building apache qpid-proton.
* Downloads the source code of qpid-proton to /main/qpid-proton
* Builds and installs qpid-proton using gcc. 
* Downloads the source code of qpid-dispatch to /main/qpid-dispatch.
* Builds and installs qpid-dispatch using gcc.  
* Launches qpid-dispatch router

####Building and running Dockerfiles
* To build the fedora docker file
 * sudo docker build -t docker-image-name --file=Dockerfile-fedora  .  (substitute docker-image-name with your own image name)
* To build the ubuntu docker file
 * sudo docker build -t  docker-image-name --file=Dockerfile-ubuntu  .  (substitute docker-image-name with your own image name)
* To run the fedora/ubuntu docker image
 * sudo docker run -i -t docker-image-name (substitute docker-image-name with your own image name)
* After the docker run command is invoked, execute the *sudo docker ps* command to check if your container name shows up in the list of docker processes.

####Running unit tests
Uncomment the line *RUN ctest -VV*  to run the dispatch unit and system tests.

####Note
These dockerfiles are provided for developers to quickly get started with the dispatch router and are not intended for use in production environments.

