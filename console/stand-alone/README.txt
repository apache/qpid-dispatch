# dispatch-standalone
The stand-alone qpid dispatch console is an html web site that monitors and controls a qpid dispatch router

To install the console:
- The console files are normally installed under /usr/share/qpid-dispatch/console/stand-alone

To run the web console:
- Ensure one of the routers in your network is configured with a normal listener with http: true
listener {
    role: normal
    host: 0.0.0.0
    port: 5673
    http: true
    saslMechanisms: ANONYMOUS
}
- start the router
- in a browser, navigate to http://localhost:5673/

The router will serve the console's html/js/css from the install directory.
The cosole will automatically connect to the router at localhost:5673

Note: An internet connection is required on the machine that is running the console in order to retrieve the 3rd party javascript / css files.



