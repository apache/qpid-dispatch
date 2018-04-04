#! /usr/bin/python

import os
import subprocess
import time
import datetime
import socket
import contextlib
import string
import signal




def get_open_port ( ):
    with contextlib.closing ( socket.socket ( socket.AF_INET,
                                              socket.SOCK_STREAM
                                            )
                            ) as sock:
        sock.bind ( ('localhost', 0) )
        return sock.getsockname()[1]




class Client ( object ) :
  
    def __init__ ( self,
                   action     = None,
                   name       = None,
                   port       = None,
                   addr       = None,
                   n_messages = None,
                   body_size  = None
                 ) :
        self.action     = action
        self.name       = name
        self.port       = port
        self.addr       = addr
        self.n_messages = n_messages
        self.body_size  = body_size

        self.process    = None



class Listener ( object ) :
    
    def __init__ ( self, role ) :
        self.role = role
        self.port = get_open_port ( )
        



class Connector ( object ) :
    
    def __init__ ( self, target_port ) :
        self.target_port = target_port




class Router ( object ) :

    def __init__ ( self, 
                   name          = 'router', 
                   mode          = 'interior', 
                   workerThreads = '4',
                   path          = None,
                   config_path   = None
                 ) :
        self.name          = name
        self.mode          = mode
        self.workerThreads = workerThreads
        self.path          = path
        self.config_path   = config_path

        self.process       = None
        self.stdout        = None
        self.stderr        = None
        self.listeners     = list()
        self.connectors    = list()


    def write_config_script ( self ) :
        with open ( self.config_path, 'w+') as config_file :
            print >> config_file , 'router {'
            print >> config_file , '   mode: %s' % self.mode
            print >> config_file , '   id:   %s' % self.name
            print >> config_file , '   workerThreads: %s' % self.workerThreads
            print >> config_file , '}'

            print >> config_file , 'address {'
            print >> config_file , '    prefix: closest'
            print >> config_file , '    distribution: closest'
            print >> config_file , '}'

            for l in self.listeners :
                print >> config_file , 'listener {'
                print >> config_file , '    role:', l.role
                print >> config_file , '    port:', l.port
                print >> config_file , '    saslMechanisms: ANONYMOUS'
                print >> config_file , '}'

            for c in self.connectors :
                print >> config_file , 'connector {'
                print >> config_file , '    role: inter-router'
                print >> config_file , '    host: 0.0.0.0'
                print >> config_file , '    port: ', c.target_port
                print >> config_file , '    saslMechanisms: ANONYMOUS'
                print >> config_file , '}'



    def run ( self ) :
        if not self.path :
            # print "Router::run error: no path"
            return
        
        self.write_config_script ( )

        self.process = subprocess.Popen ( [ self.path,
                                            '--config',
                                            self.config_path
                                          ],
                                          stdout = subprocess.PIPE,
                                          stderr = subprocess.PIPE
                                        )
        # print "router", self.name, "has started as process ", self.process.pid
        


    def add_inter_router_listener ( self ) :
        for l in self.listeners :
            if l.role == 'inter-router' :
                # print "router ", self.name, "already has an inter-router listener."
                return
        self.add_listener ( 'inter-router' )



    def get_inter_router_port ( self ) :
        for l in self.listeners :
            if l.role == 'inter-router' :
                return l.port
        return None



    def get_client_port ( self ) :
        for l in self.listeners :
            if l.role == 'normal' :
                return l.port
        return None



    def add_normal_listener ( self ) :
        for l in self.listeners :
            if l.role == 'normal' :
                # Only need one.
                return
        self.add_listener ( 'normal' )



    def add_listener ( self, role ) :
        if not ( (role == 'normal') or (role == 'inter-router') ) :
            # print "add_inter_router_listener error: bad role: |%s|" % role
            return
        self.listeners.append ( Listener ( role ) )
        # print "added", role, "listener to router", self.name



    def add_connector ( self, target ) :
        target_port = target.get_inter_router_port()
        self.connectors.append ( Connector ( target_port ) )
        


    def check ( self ) :
        if self.process.poll() is None :
            return "running"
        else :
            (self.stdout, self.stderr) = self.process.communicate ( )
            return "stopped"

    

    def halt ( self ) :
        os.kill ( self.process.pid, signal.SIGTERM )
        #(self.stdout, self.stderr) = self.process.communicate ( )
        #print "router ", self.name, "============================================="
        #print "stdout -----------------------"
        #print self.stdout
        #print "stderr -----------------------"
        #print self.stderr

    



class RouterNetwork ( object ) :
      
    def __init__ ( self,
                   n_nodes = 1,
                   router_path = None,
                   test_path   = None,
                   client_path = None,
                 ) :

        self.router_path = router_path
        self.test_path   = test_path
        self.client_path = client_path

        self.routers     = dict()
        self.clients     = list()

        config_dir_path = test_path + "/config/"

        if not os.path.exists ( config_dir_path ) :
            os.makedirs ( config_dir_path )

        for i in xrange ( n_nodes ) :
            router_name = string.ascii_uppercase [ i ]

            router_config_path = config_dir_path + router_name + '.conf'
            self.routers [ router_name ] = Router ( name=router_name, 
                                                    path=self.router_path ,
                                                    config_path = router_config_path
                                                  )


     
    def connect ( self, from_name, to_name ) :
        self.routers[to_name].add_inter_router_listener()
        to_router = self.routers[to_name]
        self.routers[from_name].add_connector(to_router)



    def add_client ( self,
                     router_name = None,   # Name line 'A', 'B'
                     action      = None,
                     name        = None,
                     addr        = None,
                     n_messages  = None,
                     body_size   = None
                   ) :
        target_router = self.routers[router_name]
        port   = target_router.get_client_port ( )
        self.clients.append ( Client ( action     = action,
                                       name       = name,
                                       port       = port,
                                       addr       = addr,
                                       n_messages = n_messages,
                                       body_size  = body_size
                                     )
                            )



    def add_client_listener ( self, router_name ) :
        target_router = self.routers [ router_name ]
        target_router.add_listener ( 'normal' )



    def halt ( self ) :
        self.halt_clients ( )
        for r in self.routers.values() :
            r.halt ( )



    def check_clients ( self ) :
        clients_still_running = list()
        for c in self.clients :
            if c.process.poll() is None :
                clients_still_running.append ( c.name )
        return clients_still_running



    def get_client_output ( self, target ) :
        for c in self.clients :
            if c.name == target :
                (stdout, stderr) = c.process.communicate ( )
                return stdout



    def halt_clients ( self ) :
        for c in self.clients :
            proc = c.process
            try :
                os.kill ( proc.pid, signal.SIGTERM )
                #(stdout, stderr) = proc.communicate ( )
                #print "client", c.name, "-------------------------------"
                #print "STDOUT  "
                #print stdout
                #print "\nSTDERR"
                #print stderr
            except OSError :
                pass # The client has already terminated.

            

    def run ( self ) :
        for r in self.routers.keys() : 
            self.routers[r].run ( )
        time.sleep ( 5 )

        body_size     = '100'
        credit_window = '100'
        tx_size       = '0'
        flags         = 'none'

        for c in self.clients :
            args = [ self.client_path,
                     'client',
                     'active',
                     c.action,
                     c.name,
                     '0.0.0.0',
                     str(c.port),
                     c.addr,
                     str(c.n_messages),
                     body_size,
                     credit_window,
                     tx_size,
                     flags
                   ]
            process = subprocess.Popen ( args,
                                         stdout = subprocess.PIPE,
                                         stderr = subprocess.PIPE
                                       )
            c.process = process






