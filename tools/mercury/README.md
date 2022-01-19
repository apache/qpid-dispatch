# Mercury
##interactive Golang Testing System for Qpid Dispatch Router



<br/>


###Requirements

1. the go language on your machine
2. installed proton and dispatch router software



<br/>

###Mercury Audience

Mercury is a tool for developers. The user has one or more installed versions of the Dispatch Router + Proton code and wantsto easily set up a complex network including interior routers, edge routers, and clients with nontrivial addressing patterns. Currently the creation of nontrivial networks is difficult enough that it discourages extensive testing during development.

Especially Mercury provides a more interactive form of development testing, in which a developer easily creates a network, and easily iterates through a cycle of altering the network, running it, and seeing results from the run. At the end of such a 'session' all of the developer's actions have been captured and will be reproducible later if desired. The captured session is a runnable script, and can be edited and used as a standardized test.


<br/>



###The C Client

The client is written in C to the proactor interface and has been heavily adapted from an original by Alan Conway. It needs to be built before Mercury will be able to do anything useful. Look at the directory mercury/clients, look at the file in there called "m", adapt it for your system, and run it so you get an executable.

Having Mercury's own client allows it to do things like:

1. throttle send-speed with the "--throttle" argument
2. Tell the client to form multiple links with multiple "--address" arguments.
3. Tell it where to send its log files and so on.


<br/>


###Starting Mercury

The directory from which to run Mercury is also called mercury.  So it's mercury/mercury.  And an example of a simple run-script is 'example_run_script'.
In that script you will see that it sets an environment variable MERCURY\_ROOT. On your system, please change this to something appropriate to your installation.


<br/>


###Getting Help

When Mercury is running, type 'help', and you will see a list of commands with brief descriptions. If you then type "help COMMAND\_NAME" you will get detailed help for that command, plus its arguments.



<br/>


###Running the Test Files


There is a growing collection of tests scripts in the directory  mercury/mercury/tests.  They are designed to illustrate different aspects of Mercury. You can run them by using the 'r' script in mercury/mercury and editing it to have the test script you want on the command line, or you can just start Mercury and type "inc tests/05\_addresses" or whatever.

The test scripts includes one other file with the 'inc' command -- a file called 'versions' which defines two different versions of the router code.

You will also need to change that 'versions' file to point to one or more versions that you have installed on your system, and then change the 'test' file to only use your versions.  (If you only define one version, then it will be the default and will get used whenever you create a new router if you just don't use the 'version' arg in the 'routers' command.




<br/>


###Debugging Startup

When Mercury starts up a router, it saves all the information you need to reproduce the same startup by hand. The router config file, the environment variables that are set, and the command line that is used are all saved in MERCURY\_ROOT/mercury/sessions/session\_TIMESTAMP/config. Router config files have the same names as their routers.

Here is an example:

    /home/your_name/mercury/mercury/sessions/session\_2019\_03\_05\_2115/config/
        |-- A.conf
        |-- B.conf
        |-- command_line
        |-- environment_variables


If you have a router fail to start, or it starts up and is immediately defunct, use this information to reproduce the same startup by hand, and see what's happening.



<br/>


###Running a test 'By Hand'

One nice way to use Mercury is to use it to run a test for you and then see how it did that, so you can run the same setup 'by hand'.  You can look in the session/config directory and see all the environment variables it set and the command lines it used for the routers and the clients. 

The command lines for the routers will point to the config files that it created, and those config files have ports that were chosen because they were free at that moment. It is possible that they will *no longer* be free when you run the test 'by hand' if you have other stuff running on your system. But unlikely.



<br/>


###Versions

A 'version' represents a version of the dispatch router + proton code. The idea is that you have as many dispatch+proton installations as you like on your system, you define a Mercury version for each one of them, and then when you create a new router in Mercury you can tell it which version you want that router to use. Mercury will use the executable that corresponds to that version, and make it point to the correct libraries.

You can define a version in one of two different ways

1. You can provide the root directories for the proton and dispatch installation, and let Mercury calculate from them all the paths it needs, or

2. You can directly provide all the paths. This second option is meant for situations where your installation is different somehow from what Mercury expects.

To define a version with roots, use the 'version\_roots' command something like this:  (Here we define two different versions.)


    version_roots name latest dispatch /home/your_name/latest/install/dispatch proton /home/your_name/latest/install/proton
    version_roots name later  dispatch /home/your_name/later/install/dispatch  proton /home/your_name/later/install/proton


After defining those two versions, you can define a two-router network using those two different versions this way:

    routers 1
    routers 1 version later

The first 'routers' command does not specify a version, so it will get the default version, which is the first one that you defined.  (In this example, 'latest'. )

The second 'routers' command does specify a version 'later'.
Both of these commands created 1 router apiece.

Now connect them with the command 

    connect A B

And you have a heterogeneous network!



<br/>


### Sessions

Each time Mercury starts up, it defines a new session. The name of the session is  "session\_YEAR\_MONTH\_DAY\_HOURMINUTE", for example: session\_2019\_03\_08\_0336".  A directory is made with that name as a subdirectory of mercury/mercury/sessions, and all information from that session is stored in there.

To replay a session, you just use the mercury log file name on the command line as the script for Mercury to run.
For example (see example of whole startup script, above) :

    go run ./*.go  ~/mercury/mercury/sessions/session_2019_03_08_0659/mercury_log

And it will replay your session.  The only thing is, that sessin-recording will have a 'quit' command at the end that you might want to delete first.



<br/>


###Client Status Reporting

When the network creates clients it gives each one of them their own individual log file in the directory SESSION/logs .  When the network starts running, a ticker is started that expires every 10 seconds. Every time it expires, a goroutine in the network code checks each clients status as written in the log files. 

Right now the only notification you get in Mercury is when the client 'completes' -- i.e. it has sent or received all the messages it was expecting to send or receive.



<br/>


###Router Status Reporting

TODO


<br/>


###Creating many clients with different addresses

You can quickly create a large number of clients, each with its own address, with a command like this:

send A count 100 address my\_address\_%d

That will add 100 sender clients to router A, and each one will have its own address. Mercury will notice the "%d" in the address, and it will replace that string with numbers that start at 1 and count up. So you will get  "my\_address\_1, my\_address\_2, ... my\_address\_100".

Then you can do the same thing with receivers:

recv C count 100 address my\_address\_%d

So now you will have 100 sender-receiver pairs, each connected by a unique address.


If you want the address-numbers to start from some number other than 1, you can control that also:

send B count 100 address my\_address\_%d  start\_at 101



<br/>


###Distributing Clients over Routers

TODO



<br/>


###One-Command Networks

There are some network topologies that we tend to use a lot, and Mercury gives you a way to create each of these with a single command. Here are some examples to try:

    linear 3
    mesh   4
    ring   6
    random_network 6
    teds_diamond

<br/>

###Repeatable Randomness

TODO

<br/>


###Command Theories of Operation

Here are more detailed explanations for each command.
<br/>
But first, a word about the commands in general.
<br/>
Most of the commands process their command lines the same way. 

1. They look for named arguments, and take them all.
2. They look at whatever is left and assume that those must be 'unlabeled' arguments.
3. There can be at most two unlabeled args: one string and one int. Mercury will know which is which as long as your string doesn't look like an int. These are used for the (very common) cases where you almost always use one arg for a particular command, so you can just say, for example, "sleep 5" instead of "sleep seconds 5" and it will do what you want.
4. All commands that have unlabelable args will tell you what they are when you give the command:   help COMMAND
5. All arguments of commands can occur in any order. Order does not matter.

<br/>
OK. Now, here are more detailed explanations for each command.

<br/>

####connect                   
Connect two routers that you have already created. 
    Example: connect A B     
This will cause router A to initiate a connection with router B.
This command does not parse its arguments the way most of them do, so there are no named arguments. Just the two router names.

<br/>
typical usage : connect A B

<br/>

####console_ports 
Every router (even edge routers) has an HTTP-enabled listener for the console. The A-router (the first core router created) always uses the special-magic port 5673 for this (which will be a problem if you run two Mercury tests at once on one system) but all the other routers use whatever free ports they could find. This command lists all the console ports that are available, so you can attach Ernie's Console if you like.

<br/>
typical usage : console_ports

<br/>

####echo         
This command has its own way of processing its command line, and does not have any named arguments.
It just echoes the whole line (not including the word 'echo' to the console. 
You can use this as a way of documenting scripts, so that the user can see what is going on as the script runs.

<br/>
typical usage :   echo This line will be echoed to the console!

<br/>

<br/>

####echo_all
Once you issue this command, all further commands are echoed to the console. ( Until you issue "echo_all off" . )  This is handy if you are using a Mercury script to do a demo and you want people to be able to see the commands.  ( In that case, you should also look at the 'prompt' command. )

<br/>
typical usage :   echo_all


<br/>

<br/>

####edges  
Create edge routers attached to a core router. You can specify how many edge routers you want and which router they should be attached to with the unlabelable "count" and "router" args. You can also specify which version of the code they should use, with the "version" arg, if you have defined a version.

<br/>
typical usage : edges 10 A version VERSION_NAME

<br/>

<br/>

####failsafe
This command tells the system how many seconds to wait (after reporting on clients begins) until it should shut down with failure. There are ( or will be ) other ways to shut down, but you can use this to specify a shutdown time in case all else fails.

<br/>
typical usage : failsafe 60

<br/>

<br/>

####help   
Print the names of all commands and give a brief description of each, or print a detailed description of a single command, including descriptions of each argument.

<br/>
typical usage : help     ~OR~      help COMMAND_NAME

<br/>

<br/>

####inc   
Include another file at this point in your script or interactive session. If you are doing an interactive session, this is a nice way to avoid having to type 'boilerplate' commands that never change, for example the commands that define your various versions of the dispatch and router code to be tested.

<br/>
typical usage : inc FILENAME

<br/>

<br/>

####kill 
Kill a router!   Useful in stress testing. Just gives its name and BOOM. Dead router.

<br/>
typical usage :  kill ROUTER_NAME 

<br/>

<br/>

####kill_and_restart
Kill a router and immediately restart it. Useful in stress-testing.

<br/>
typical usage : kill_and_restart ROUTER_NAME

<br/>

<br/>

####linear         
This is a network-command. It takes an unlabelable arg 'count' to tell how large the network should be, and then makes a network of that size, whose topology is linear. You can also specify what version you would like to use for the routers with  'version VERSION_NAME'  or you can say 'version RANDOM' to tell Mercury to select randomly from your set of versions as it creates each router.

<br/>
typical usage :

<br/>

<br/>

####mesh          
Network command that create a full-connected graph of routers, as large as you wish. Don't go nuts. Fully-connected graphs gets lots of connections proportional to N^2.  

<br/>
typical usage :  mesh N

<br/>

<br/>

####prompt       
Turns prompting on or off (default on), which means that Mercury stops after each command and asks you to hit enter. If you are doing a demo from a prepared script, you should use this plus the echo_all command, so your audience will be able to see each command before it executes.

<br/>
typical usage : prompt

<br/>

<br/>

####quit        
Halts Mercury.

<br/>
typical usage : quit

<br/>

<br/>

####random_network 
This is a network-command. Tell it how many nodes you want, and it will create a randomly-connected network with that many nodes. It just starts making connections between random pairs of routers, and stops as soon as it has a connected graph. The only rule is that you cannot have more than one connection between any two routers.

<br/>
typical usage :  random_network  6

<br/>

<br/>

####recv  
Create one or more receivers on one or more routers, all using the same version of code, or all selecting randomly.  There are lots of arguments -- look at "help recv".  Below is an example in which we create 100 receivers distributed across all edge-routers connected to router A, using random versions of the code, and each expecting 1000 messages. And there 5 addresses per receiver, i.e. receiver_1 gets addr_1 to addr_5, receiver_2 gets addr_6 to addr_10, etc.

<br/>
typical usage : recv count 100 apc 5 n_messages 1000 edges A

<br/>

<br/>


####ring  
This is a network-command. Tell it how many nodes you want, and it will create a ring-shaped network of that size.

<br/>
typical usage : ring 5

<br/>

<br/>

####routers
Create some routers. You can specify how many, and what version of the code they should use. Version will default to the first one that was defined, if you don't say anything.

<br/>
typical usage :

<br/>

<br/>

####run 
No args. This is what starts things running. Sometimes it's best to issue this command more than once in the course of a test. For example, you make a network of a bunch of routers. Issue this command to start them going, so they will take care of their inter-router chatter. *Then* make the receivers, and issue 'run' again, so that the routers will learn about all the addresses. Then make the senders and call run one more time, to start them up. Each time, the clients or senders that were not already running get started when you call 'run'.

<br/>
typical usage : run

<br/>

<br/>

####seed

<br/>
typical usage :

<br/>

<br/>

####send 

<br/>
typical usage :

<br/>

<br/>

####sleep

<br/>
typical usage :

<br/>

<br/>

####start_client_status_check

<br/>
typical usage :

<br/>

<br/>

####teds_diamond      

<br/>
typical usage :

<br/>

<br/>

####verbose    

<br/>
typical usage :

<br/>

<br/>

####version_roots 

<br/>
typical usage :

<br/>



<br/>
<br/>
