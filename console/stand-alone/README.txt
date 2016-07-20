# dispatch-standalone
The stand-alone qpid dispatch console is an html web site that monitors and controls a qpid dispatch router

To install the console:
- install and setup a web server (such as apache tomcat)
- under the web servers's webapps dir, create a dispatch dir
- copy the contents of this directory to the web server's dispatch dir

To run the web console:
- start the web server
- in a browser, navigate to http://localhost:<htmlport>/dispatch/

To connect to a qpid dispatch router from the console, you'll need to setup a websockets to tcp proxy.

To setup and run a websockets proxy:
- dnf install websockify
- websockify 0.0.0.0:5673 0.0.0.0:<router's listener port>

On the console's connect page you can then use the address of your web server and port 5673 to connect.


